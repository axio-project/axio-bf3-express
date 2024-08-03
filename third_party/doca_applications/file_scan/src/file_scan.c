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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <doca_argp.h>
#include <doca_log.h>
#include <doca_regex.h>
#include <doca_regex_mempool.h>

#include "file_scan_core.h"

DOCA_LOG_REGISTER(FILE_SCAN);

/*
 * File Scan application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
	struct file_scan_config app_cfg = {0};

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_file_scan", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	result = register_file_scan_params();
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

	/* Initialize app resources and doca RegEx */
	result = file_scan_init(&app_cfg);
	if (result != DOCA_SUCCESS) {
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	if (file_scan_run(&app_cfg) != 0) {
		DOCA_LOG_ERR("File scan failed to run");
		exit_status = EXIT_FAILURE;
	}

	file_scan_cleanup(&app_cfg);

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
