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
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include <doca_error.h>
#include <doca_log.h>

#include "dpa_common.h"

DOCA_LOG_REGISTER(DIAMOND_TREE::MAIN);

/* Sample's logic */
doca_error_t diamond_tree(struct dpa_resources *resources);

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
	struct dpa_config cfg = {{0}};
	struct dpa_resources resources = {0};
	doca_error_t result;
	int exit_status = EXIT_FAILURE;

	/* Set default value for device name */
	strcpy(cfg.device_name, IB_DEVICE_DEFAULT_NAME);

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_dpa_diamond_tree", &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}

	/* Register DPA params */
	result = register_dpa_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register sample parameters: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	/* Allocating resources */
	result = allocate_dpa_resources(&resources, &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to Allocate DPA Resources: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	/* Running sample */
	result = diamond_tree(&resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("diamond_tree() encountered an error: %s", doca_get_error_string(result));
		goto dpa_cleanup;
	}

	exit_status = EXIT_SUCCESS;

dpa_cleanup:
	/* Destroying DPA resources */
	result = destroy_dpa_resources(&resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA DPA resources: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
	}
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
