/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <doca_log.h>

#include "common_common.h"

DOCA_LOG_REGISTER(SYNC_EVENT::MAIN);

/*
 * Sample's logic
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t sync_event_run(struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs);

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
	doca_error_t result = DOCA_SUCCESS;
	struct sync_event_config se_cfg;
	struct sync_event_runtime_objects se_rt_objs;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;

	memset(&se_cfg, 0, sizeof(struct sync_event_config));
	memset(&se_rt_objs, 0, sizeof(struct sync_event_runtime_objects));
	se_rt_objs.se_task_result = DOCA_ERROR_UNKNOWN;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

#ifndef DOCA_ARCH_HOST
	DOCA_LOG_ERR("Sample can run only on the Host");
	goto sample_exit;
#endif

	result = doca_argp_init("doca_sync_event_host", &se_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init argp: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	result = sync_event_params_register();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register sample parameters: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = sync_event_run(&se_cfg, &se_rt_objs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("sync_event_run() encountered an error: %s", doca_error_get_descr(result));
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
