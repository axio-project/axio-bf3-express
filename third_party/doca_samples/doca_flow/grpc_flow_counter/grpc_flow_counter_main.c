/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <stdlib.h>

#include <doca_argp.h>
#include <doca_flow_grpc_client.h>
#include <doca_log.h>

DOCA_LOG_REGISTER(GRPC_FLOW_COUNTER::MAIN);

/* Sample's Logic */
int grpc_flow_counter(const char *grpc_address);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	const char *grpc_address;
	int exit_status = EXIT_FAILURE;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_grpc_flow_counter", NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}
	doca_argp_set_grpc_program();
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = doca_argp_get_grpc_addr(&grpc_address);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get grpc address");
		goto argp_cleanup;
	}

	/* run sample */
	result = grpc_flow_counter(grpc_address);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("grpc_flow_counter() encountered an error: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	exit_status = EXIT_SUCCESS;

argp_cleanup:
	doca_argp_destroy();

sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
