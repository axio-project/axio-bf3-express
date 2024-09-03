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

#include <assert.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <float.h>
#include <stdlib.h>

#include "utils.h"

#include "allreduce_ucx.h"
#include "allreduce_core.h"
#include "allreduce_daemon.h"
#include "allreduce_client.h"

DOCA_LOG_REGISTER(ALLREDUCE);

/*
 * Wrapper for "atexit" that destroys argp
 */
static void
argp_destroy_wrapper(void)
{
	doca_argp_destroy();
}

/*
 * Wrapper for "atexit" that cleanup Allreduce resources
 */
static void
allreduce_destroy_wrapper(void)
{
	allreduce_destroy(allreduce_config.dest_addresses.num);
}

/*
 * Allreduce application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	int num_connections;
	struct doca_log_backend *sdk_log;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
#ifndef GPU_SUPPORT
	result = doca_argp_init("doca_allreduce", &allreduce_config);
#else
	result = doca_argp_init("doca_allreduce_gpu", &allreduce_config);
#endif
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}
	result = register_allreduce_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register the program parameters: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	result = allreduce_init(&num_connections);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init UCX or failed to connect to a given address");
		doca_argp_destroy();
		return EXIT_FAILURE;
	}
	DOCA_LOG_INFO("Successfully connected to all given addresses");

	if (allreduce_config.role == ALLREDUCE_CLIENT) {
		if (num_connections == 0) {
			/* Nothing to do */
			return EXIT_SUCCESS;
		} else if ((num_connections > 1) && (allreduce_config.allreduce_mode == ALLREDUCE_OFFLOAD_MODE)) {
			DOCA_LOG_ERR("Number of client's peers in offloaded mode should be 1 instead of %d",
					num_connections);
			allreduce_destroy(num_connections);
			doca_argp_destroy();
			return EXIT_FAILURE;
		}
	}

	/* Register destroy function for cleanup in case of unexpected error in a event-driven functionality */
	atexit(argp_destroy_wrapper);
	atexit(allreduce_destroy_wrapper);

	/* Run required code depending on the type of the process */
	if (allreduce_config.role == ALLREDUCE_DAEMON)
		daemon_run();
	else
		client_run();

	/*
	 * No need to call destroy functions since destroy routines are called after return, due to atexit registration
	 */
	return EXIT_SUCCESS;
}
