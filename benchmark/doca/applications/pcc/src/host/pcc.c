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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

#include <doca_argp.h>
#include <doca_dev.h>
#include <doca_pcc.h>

#include "pcc_core.h"

/* Default PCC threads */
static const uint32_t default_pcc_threads_list[PCC_THREADS_NUM_DEFAULT_VALUE] = {
					176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187,
					192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203,
					208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
					224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235,
					240};
static const char *status_str[DOCA_PCC_PS_ERROR + 1] = {"Active", "Standby", "Deactivated", "Error"};
static bool host_stop;
int log_level;

/*
 * Signal sigint handler
 *
 * @dummy [in]: Dummy parameter because this handler must accept parameter of type int
 */
static void
sigint_handler(int dummy)
{
	(void)dummy;
	host_stop = true;
	signal(SIGINT, SIG_DFL);
}

/*
 * Application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	struct pcc_config cfg = {0};
	struct pcc_resources resources = {0};
	doca_pcc_process_state_t process_status;
	doca_error_t result, tmp_result;
	int exit_status = EXIT_FAILURE;

	/* Set the default configuration values (Example values) */
	cfg.wait_time = -1;
	memcpy(cfg.pcc_threads_list, default_pcc_threads_list, sizeof(default_pcc_threads_list));
	cfg.pcc_threads_num = PCC_THREADS_NUM_DEFAULT_VALUE;
	strcpy(cfg.pcc_coredump_file, PCC_COREDUMP_FILE_DEFAULT_PATH);
	log_level = LOG_LEVEL_INFO;

	/* Add SIGINT signal handler for graceful exit */
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		PRINT_ERROR("Error: SIGINT error\n");
		return DOCA_ERROR_OPERATING_SYSTEM;
	}

	/* Initialize argparser */
	result = doca_argp_init("doca_pcc", &cfg);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to init ARGP resources: %s\n", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	/* Register DOCA PCC application params */
	result = register_pcc_params();
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to register parameters: %s\n", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Start argparser */
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to parse input: %s\n", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Get the log level */
	result = doca_argp_get_log_level(&log_level);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to get log level: %s\n", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Initialize DOCA PCC application resources */
	result = pcc_init(&cfg, &resources);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to initialize PCC resources: %s\n", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	PRINT_INFO("Info: Welcome to DOCA Programable Congestion Control (PCC) application\n");
	PRINT_INFO("Info: Starting DOCA PCC\n");

	/* Start DOCA PCC */
	result = doca_pcc_start(resources.doca_pcc);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to start PCC\n");
		goto destroy_pcc;
	}

	host_stop = false;
	PRINT_INFO("Info: Press ctrl + C to exit\n");
	while (!host_stop) {
		result = doca_pcc_get_process_state(resources.doca_pcc, &process_status);
		if (result != DOCA_SUCCESS) {
			PRINT_ERROR("Error: Failed to query PCC\n");
			goto destroy_pcc;
		}

		PRINT_INFO("Info: PCC host status %s\n", status_str[process_status]);

		if (process_status == DOCA_PCC_PS_DEACTIVATED || process_status == DOCA_PCC_PS_ERROR)
			break;

		PRINT_INFO("Info: Waiting on DOCA PCC\n");
		result = doca_pcc_wait(resources.doca_pcc, cfg.wait_time);
		if (result != DOCA_SUCCESS) {
			PRINT_ERROR("Error: Failed to wait PCC\n");
			goto destroy_pcc;
		}
	}

	PRINT_INFO("Info: Finished waiting on DOCA PCC\n");

	exit_status = EXIT_SUCCESS;

destroy_pcc:
	tmp_result = pcc_destroy(&resources);
	if (tmp_result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to destroy DOCA PCC application resources: %s\n",
				doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
argp_cleanup:
	tmp_result = doca_argp_destroy();
	if (tmp_result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to destroy ARGP: %s\n", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return exit_status;
}
