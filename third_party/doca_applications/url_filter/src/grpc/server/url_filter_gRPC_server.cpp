/*
 * Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/wait.h>
#include <condition_variable>
#include <string>

#include <rte_compat.h>
#include <rte_sft.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <grpc/log_forwarder.hpp>

#include <dpdk_utils.h>
#include <utils.h>

#include "url_filter_core.h"
#include "orchestration.hpp"
#include "server.hpp"

DOCA_LOG_REGISTER(URL_FILTER::GRPC);

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

static std::condition_variable server_lock;		/* Boolean for ending the server */

static struct synchronized_queue log_records_queue;	/* Logs queue */
static struct clients_pool subscribed_clients;		/* Clients thread pool */

/*
 * Function used for inserting the log messages to the messages queue.
 *
 * @buffer [in]: null terminated string to enqueue
 */
static void
flush_buffer(char *buffer)
{
	/* Insert log message to queue */
	synchronized_queue_enqueue(&log_records_queue, std::string(buffer));
}

/*
 * Function used for destroying the server
 */
static void
server_teardown(void)
{
	teardown_server_sessions(&log_records_queue, &subscribed_clients);
	/* Signal the sleeping thread to wake-up */
	server_lock.notify_one();
}

Status
UrlFilterImpl::Subscribe(ServerContext *context, const SubscribeReq *request,
			 ServerWriter<LogRecord> *writer)
{
	(void)context;
	(void)request;

	if (!subscribe_client(&subscribed_clients, writer))
		return Status::CANCELLED;

	return Status::OK;
}

Status
UrlFilterImpl::Create(ServerContext* context, const CreateReq *request,
		      CreateResp *response)
{
	(void)context;
	(void)request;
	(void)response;

	create_database(DEFAULT_TXT_INPUT);
	return Status::OK;
}

Status
UrlFilterImpl::Add(ServerContext *context, const FilterRule *request,
		   AddResp *response)
{
	(void)context;
	(void)response;

	create_url_signature(DEFAULT_TXT_INPUT, request->msg().c_str(), request->pcre().c_str());
	return Status::OK;
}

Status
UrlFilterImpl::Commit(ServerContext *context, const CommitReq *request,
		      CommitResp *response)
{
	(void)context;
	(void)request;
	(void)response;

	compile_and_load_signatures(DEFAULT_TXT_INPUT, DEFAULT_CDO_OUTPUT);
	return Status::OK;
}

Status
UrlFilterImpl::Quit(ServerContext *context, const QuitReq *request,
		    QuitResp *response)
{
	(void)context;
	(void)request;
	(void)response;

	server_teardown();
	return Status::OK;
}

Status
DocaOrchestrationImpl::HealthCheck(ServerContext *context, const HealthCheckReq *request,
				   HealthCheckResp *response)
{
	(void)context;
	(void)request;
	(void)response;

	/* Show the service that we are responsive */
	return Status::OK;
}

Status
DocaOrchestrationImpl::Destroy(ServerContext *context, const DestroyReq *request,
			       DestroyResp *response)
{
	(void)context;
	(void)request;
	(void)response;

	server_teardown();
	return Status::OK;
}

/*
 * Starts the server.
 *
 * @raw_server_address [in]: String representing the server IP, i.e. "127.0.0.1" or "192.168.100.3:5050"
 *            If no port is provided, it will use the server's default port
 */
static void
run_server(const char *raw_server_address)
{
	/* Check if we got a port or if we are using the default one */
	std::string server_address(raw_server_address);
	if (server_address.find(':') == std::string::npos)
		server_address += ":" + std::to_string(eNetworkPort::k_UrlFilter);

	/* Make sure the stream won't close on us and shorten delays */
	grpc::EnableDefaultHealthCheckService(true);

	/* Config the gRPC server */
	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

	/* Add the services */
	UrlFilterImpl app_service;
	DocaOrchestrationImpl orchestration_service;
	builder.RegisterService(&app_service);
	builder.RegisterService(&orchestration_service);

	/* Start the logger thread */
	std::thread logger_thread([]{forward_log_records(&log_records_queue, &subscribed_clients);});

	std::unique_ptr<Server> server(builder.BuildAndStart());
	DOCA_LOG_INFO("gRPC server started");

	/* Wait for the Quit / Destroy command */
	std::mutex mutex;
	std::unique_lock<std::mutex> lock(mutex);
	server_lock.wait(lock);

	/* Officially shut down the server */
	server->Shutdown();
	logger_thread.join();
}

/*
 * URL Filter gRPC application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char *argv[])
{
	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
	const char *grpc_address;
	struct url_filter_config url_filter_config = {0};
	struct application_dpdk_config dpdk_config = {0};
	char log_buffer[1024] = {};
	struct doca_logger_backend *logger;
	struct dpi_worker_attr dpi_worker = {0};

	/* Init the DPDK configuration struct */
	dpdk_config.port_config.nb_ports = 2;
	dpdk_config.port_config.nb_queues = 2;
	dpdk_config.port_config.nb_hairpin_q = 4;
	dpdk_config.sft_config = {1, 1, 1, 1};
	dpdk_config.reserve_main_thread = true;

	url_filter_config.dpdk_config = &dpdk_config;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_url_filter_grpc", &url_filter_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	doca_argp_set_grpc_program();
	result = register_url_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register application params: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Initialize the DPDK settings */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update application ports and queues: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_destroy;
	}

	/* Allocate a logging backend that will forward the logs to the gRPC client (host) */
	result = doca_log_create_buffer_backend(log_buffer, sizeof(log_buffer), flush_buffer, &logger);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate the logger");
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	/* All needed preparations - Check for required files, init the DPI, etc */
	result = url_filter_init(&dpdk_config, &url_filter_config, &dpi_worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init url filter application: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	/* Start the DPI processing */
	result = dpi_worker_lcores_run(dpdk_config.port_config.nb_queues, dpi_worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start the DPI worker: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto url_filter_cleanup;
	}

	if (doca_argp_get_grpc_addr(&grpc_address) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get grpc address");
		exit_status = EXIT_FAILURE;
		goto url_filter_cleanup;
	}

	/* Start the server */
	run_server(grpc_address);

url_filter_cleanup:
	/* Remove used files and free resources */
	url_filter_destroy();

dpdk_cleanup:
	/* DPDK cleanup */
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_destroy:
	dpdk_fini();

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
