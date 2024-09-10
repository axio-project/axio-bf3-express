/*
 * Copyright (c) 2023-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <mpi.h>

#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>
#include <doca_urom.h>

#include <worker_sandbox.h>

#include "common.h"
#include "urom_common.h"

DOCA_LOG_REGISTER(UROM_PING_PONG::SAMPLE);

/* ping pong result type */
enum pp_res_type_t {
	PP_RES_TYPE_UNKNOWN = 0,
	PP_RES_TYPE_SEND,
	PP_RES_TYPE_RECV,
};

#define PING_TAG 0x1234	     /* UCX data for ping tag */
#define PONG_TAG 0x5678	     /* UCX data for pong tag */
#define RECV_MAX_LEN 100     /* Receive message maximum size */
#define PING_RECV_CTX 0xdead /* Ping recv context */
#define PONG_RECV_CTX 0xbeef /* Pong recv context */

/*
 * PP OOB allgather handler
 *
 * @sbuf [in]: local buffer to send to other processes
 * @rbuf [in]: global buffer to include other processes source buffer
 * @msglen [in]: source buffer length
 * @coll_info [in]: collection info
 * @req [in]: allgather request data
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t oob_allgather(void *sbuf, void *rbuf, size_t msglen, void *coll_info, void **req)
{
	MPI_Request *request;
	MPI_Comm comm = (MPI_Comm)(uintptr_t)coll_info;

	request = calloc(1, sizeof(*request));
	if (request == NULL)
		return DOCA_ERROR_NO_MEMORY;

	MPI_Iallgather(sbuf, msglen, MPI_BYTE, rbuf, msglen, MPI_BYTE, comm, request);

	*req = request;
	return DOCA_SUCCESS;
}

/*
 * PP OOB allgather test
 *
 * @req [in]: allgather request data
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t oob_allgather_test(void *req)
{
	int is_done = 0;
	MPI_Request *request = (MPI_Request *)req;

	MPI_Test(request, &is_done, MPI_STATUS_IGNORE);

	return is_done ? DOCA_SUCCESS : DOCA_ERROR_IN_PROGRESS;
}

/*
 * PP OOB allgather free
 *
 * @req [in]: allgather request data
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t oob_allgather_free(void *req)
{
	free(req);
	return DOCA_SUCCESS;
}

/**
 * Recv task result structure
 */
struct recv_result {
	enum pp_res_type_t type;   /* Result type */
	uint64_t context;	   /* Task context */
	char buffer[RECV_MAX_LEN]; /* Message buffer */
	uint64_t count;		   /* Message size */
	uint64_t sender_tag;	   /* Sender tag */
	ucs_status_t status;	   /* Sandbox recv task status */
	doca_error_t result;	   /* Worker task result */
};

/**
 * Send task result structure
 */
struct send_result {
	enum pp_res_type_t type; /* Result type */
	uint64_t context;	 /* Task context */
	ucs_status_t status;	 /* Sandbox send task status */
	doca_error_t result;	 /* Worker task result */
};

/*
 * Recv task callback
 *
 * @result [in]: task result
 * @cookie [in]: user cookie
 * @context [in]: user context
 * @buffer [in]: inline receive data, NULL if RDMA
 * @buf_len [in]: buffer length
 * @sender_tag [in]: sender tag
 * @status [in]: UCX status
 */
static void recv_finished_cb(doca_error_t result,
			     union doca_data cookie,
			     union doca_data context,
			     void *buffer,
			     uint64_t buf_len,
			     uint64_t sender_tag,
			     ucs_status_t status)
{
	struct recv_result *res = (struct recv_result *)cookie.ptr;

	if (res == NULL)
		return;

	res->type = PP_RES_TYPE_RECV;
	res->result = result;
	res->context = context.u64;
	if (result != DOCA_SUCCESS)
		return;
	res->status = status;
	snprintf(res->buffer, RECV_MAX_LEN, "%s", (char *)buffer);
	res->count = buf_len;
	res->sender_tag = sender_tag;
}

/*
 * Send task callback
 *
 * @result [in]: task result
 * @cookie [in]: user cookie
 * @context [in]: user context
 * @status [in]: UCX status
 */
static void send_finished_cb(doca_error_t result, union doca_data cookie, union doca_data context, ucs_status_t status)
{
	struct send_result *res = (struct send_result *)cookie.ptr;

	if (res == NULL)
		return;

	res->type = PP_RES_TYPE_SEND;
	res->context = context.u64;
	res->result = result;
	res->status = status;
}

/*
 * Do a ping pong between MPI processes, the rank points who is the server or client
 *
 * @pe [in]: progress engine context
 * @worker [in]: UROM worker context
 * @msg [in]: ping pong message
 * @my_rank [in]: Current MPI process rank
 * @size [in]: MPI world size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ping_pong(struct doca_pe *pe, struct doca_urom_worker *worker, const char *msg, int my_rank, int size)
{
	int ret;
	bool is_client = false;
	doca_error_t result;
	union doca_data context;
	union doca_data send_data;
	union doca_data recv_data;
	int msg_len = strlen(msg) + 1;
	int dest = (my_rank + 1) % size;
	struct send_result *send_res;
	struct recv_result *recv_res;

	recv_res = calloc(1, sizeof(*recv_res));
	if (recv_res == NULL)
		return DOCA_ERROR_NO_MEMORY;

	send_res = calloc(1, sizeof(*send_res));
	if (send_res == NULL) {
		free(recv_res);
		return DOCA_ERROR_NO_MEMORY;
	}

	recv_data.ptr = recv_res;
	send_data.ptr = send_res;

	if (my_rank != 0)
		is_client = true;

	if (is_client) {
		context.u64 = PONG_RECV_CTX;
		result = urom_sandbox_tag_task_recv(worker,
						    recv_data,
						    context,
						    0,
						    RECV_MAX_LEN,
						    PONG_TAG,
						    0xffff,
						    0,
						    recv_finished_cb);
		if (result != DOCA_SUCCESS)
			goto error_exit;

		DOCA_LOG_INFO("Client posted PING recv");
	} else {
		context.u64 = PING_RECV_CTX;
		result = urom_sandbox_tag_task_recv(worker,
						    recv_data,
						    context,
						    0,
						    RECV_MAX_LEN,
						    PING_TAG,
						    0xffff,
						    0,
						    recv_finished_cb);
		if (result != DOCA_SUCCESS)
			goto error_exit;

		DOCA_LOG_INFO("Server posted PONG recv");
	}

	if (is_client) {
		context.u64 = 0;
		result = urom_sandbox_tag_task_send(worker,
						    send_data,
						    context,
						    dest,
						    (uint64_t)msg,
						    msg_len,
						    PING_TAG,
						    0,
						    send_finished_cb);
		if (result != DOCA_SUCCESS)
			goto error_exit;

		DOCA_LOG_INFO("Client posted PING send");

		send_res->type = 0;
		send_res->result = DOCA_SUCCESS;
		do {
			ret = doca_pe_progress(pe);
		} while (ret == 0 || send_res->type == 0);

		if (send_res->result != DOCA_SUCCESS || send_res->type != PP_RES_TYPE_SEND ||
		    send_res->status != UCS_OK) {
			DOCA_LOG_ERR("Client ping send failed");
			result = send_res->result;
			goto error_exit;
		}
	} else {
		/* Wait to receive ping */
		recv_res->type = 0;
		recv_res->result = DOCA_SUCCESS;
		do {
			ret = doca_pe_progress(pe);
		} while (ret == 0 || recv_res->type == 0);

		if (recv_res->context != PING_RECV_CTX || recv_res->sender_tag != PING_TAG ||
		    recv_res->status != UCS_OK || recv_res->result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Server receive ping failed");
			result = recv_res->result;
			goto error_exit;
		}

		DOCA_LOG_INFO("Server completed PING recv: %s", (char *)recv_res->buffer);

		/* Do pong */
		context.u64 = 0;
		result = urom_sandbox_tag_task_send(worker,
						    send_data,
						    context,
						    dest,
						    (uint64_t)msg,
						    msg_len,
						    PONG_TAG,
						    0,
						    send_finished_cb);
		if (result != DOCA_SUCCESS)
			goto error_exit;

		DOCA_LOG_INFO("Server posted PONG send");

		/* Pop tag send notification */
		send_res->type = 0;
		send_res->result = DOCA_SUCCESS;
		do {
			ret = doca_pe_progress(pe);
		} while (ret == 0 || send_res->type == 0);

		if (send_res->result != DOCA_SUCCESS || send_res->type != PP_RES_TYPE_SEND ||
		    send_res->status != UCS_OK) {
			DOCA_LOG_ERR("Server ping send failed");
			result = send_res->result;
			goto error_exit;
		}
	}

	if (is_client) {
		/* Wait to receive pong */
		recv_res->type = 0;
		recv_res->result = DOCA_SUCCESS;
		do {
			ret = doca_pe_progress(pe);
		} while (ret == 0 || recv_res->type == 0);

		if (recv_res->context != PONG_RECV_CTX || recv_res->sender_tag != PONG_TAG ||
		    recv_res->status != UCS_OK || recv_res->result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Server receive ping failed");
			result = recv_res->result;
			goto error_exit;
		}

		DOCA_LOG_INFO("Client received PONG recv: %s", (char *)recv_res->buffer);
	}

	return DOCA_SUCCESS;

error_exit:
	free(recv_res);
	free(send_res);
	return result;
}

/*
 * Ping Pong sample based on Sandbox plugin
 *
 * @message [in]: ping pong message
 * @device_name [in]: IB device name to open
 * @rank [in]: Current MPI process rank
 * @size [in]: MPI world size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t urom_ping_pong(const char *message, const char *device_name, uint32_t rank, uint32_t size)
{
	int idx;
	size_t i, plugins_count = 0;
	char *plugin_name = "worker_sandbox";
	struct doca_pe *pe;
	struct doca_dev *dev;
	doca_cpu_set_t cpuset;
	enum doca_ctx_states state;
	doca_error_t tmp_result, result;
	struct doca_urom_domain *domain;
	struct doca_urom_worker *worker;
	struct doca_urom_service *service;
	uint64_t worker_id = (uint64_t)rank;
	struct doca_urom_domain_oob_coll oob_coll = {
		.allgather = oob_allgather,
		.req_test = oob_allgather_test,
		.req_free = oob_allgather_free,
		.coll_info = (void *)(uintptr_t)MPI_COMM_WORLD,
		.n_oob_indexes = size,
		.oob_index = rank,
	};
	const struct doca_urom_service_plugin_info *plugins, *sandbox_info = NULL;

	/* Open IB device */
	result = open_doca_device_with_ibdev_name((uint8_t *)device_name, strlen(device_name), NULL, &dev);
	if (result != DOCA_SUCCESS)
		return result;

	/* Create PE context */
	result = doca_pe_create(&pe);
	if (result != DOCA_SUCCESS)
		goto close_dev;

	/* Create and start service context */
	result = start_urom_service(pe, dev, 32, &service);
	if (result != DOCA_SUCCESS)
		goto pe_cleanup;

	result = doca_urom_service_get_plugins_list(service, &plugins, &plugins_count);
	if (result != DOCA_SUCCESS || plugins_count == 0)
		goto service_stop;

	for (i = 0; i < plugins_count; i++) {
		if (strcmp(plugin_name, plugins[i].plugin_name) == 0) {
			sandbox_info = &plugins[i];
			break;
		}
	}

	if (sandbox_info == NULL) {
		DOCA_LOG_ERR("Failed to match sandbox plugin");
		result = DOCA_ERROR_INVALID_VALUE;
		goto service_stop;
	}

	result = urom_sandbox_init(sandbox_info->id, sandbox_info->version);
	if (result != DOCA_SUCCESS)
		goto service_stop;

	result = doca_urom_service_get_cpuset(service, &cpuset);
	if (result != DOCA_SUCCESS)
		goto service_stop;

	for (idx = 0; idx < 8; idx++) {
		if (!doca_cpu_is_set(idx, &cpuset))
			goto service_stop;
	}

	/* Create and start worker context */
	result = start_urom_worker(pe, service, worker_id, NULL, 16, NULL, NULL, 0, sandbox_info->id, &worker);
	if (result != DOCA_SUCCESS)
		goto service_stop;

	/* Loop till worker state changes to running */
	do {
		doca_pe_progress(pe);
		result = doca_ctx_get_state(doca_urom_worker_as_ctx(worker), &state);
	} while (state == DOCA_CTX_STATE_STARTING && result == DOCA_SUCCESS);

	/* Create domain context */
	result = start_urom_domain(pe, &oob_coll, &worker_id, &worker, 1, NULL, 0, &domain);
	if (result != DOCA_SUCCESS)
		goto worker_stop;

	/* Loop till domain state changes to running */
	do {
		doca_pe_progress(pe);
		result = doca_ctx_get_state(doca_urom_domain_as_ctx(domain), &state);
	} while (state == DOCA_CTX_STATE_STARTING && result == DOCA_SUCCESS);

	/* Run ping pong flow */
	result = ping_pong(pe, worker, message, rank, size);
	if (result != DOCA_SUCCESS)
		MPI_Abort(MPI_COMM_WORLD, 1);

	tmp_result = doca_ctx_stop(doca_urom_domain_as_ctx(domain));
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop UROM domain");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_urom_domain_destroy(domain);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy UROM domain");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

worker_stop:
	tmp_result = doca_ctx_stop(doca_urom_worker_as_ctx(worker));
	if (tmp_result != DOCA_SUCCESS && tmp_result != DOCA_ERROR_IN_PROGRESS) {
		DOCA_LOG_ERR("Failed to request stop UROM worker");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	do {
		doca_pe_progress(pe);
		doca_ctx_get_state(doca_urom_worker_as_ctx(worker), &state);
	} while (state != DOCA_CTX_STATE_IDLE);

	tmp_result = doca_urom_worker_destroy(worker);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy UROM worker");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

service_stop:
	tmp_result = doca_ctx_stop(doca_urom_service_as_ctx(service));
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop UROM service");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_urom_service_destroy(service);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy UROM service");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
pe_cleanup:
	tmp_result = doca_pe_destroy(pe);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy PE");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

close_dev:
	tmp_result = doca_dev_close(dev);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close device");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
