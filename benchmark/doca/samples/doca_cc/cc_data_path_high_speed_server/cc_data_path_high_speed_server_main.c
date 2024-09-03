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
#include <doca_dev.h>
#include <doca_log.h>

#include "cc_ctrl_path_common.h"

#define DEFAULT_PCI_ADDR "03:00.0"
#define DEFAULT_REP_PCI_ADDR "b1:00.0"
#define DEFAULT_MESSAGE "Message from the client"

DOCA_LOG_REGISTER(CC_DATA_PATH_HIGH_SPEED_SERVER:MAIN);

/* Sample's Logic */
doca_error_t start_cc_data_path_server_sample(const char *server_name, const char *dev_pci_addr,
					      const char *rep_pci_addr, const char *text);

/*
 * Sample main function
 *
 * @argc [in]: Command line arguments size
 * @argv [in]: Array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	struct cc_config cfg;
	const char *server_name = "cc_data_path_sample_server";
	doca_error_t result;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;

	/* Set the default configuration values */
	strcpy(cfg.cc_dev_pci_addr, DEFAULT_PCI_ADDR);
	strcpy(cfg.cc_dev_rep_pci_addr, DEFAULT_REP_PCI_ADDR);
	strcpy(cfg.text, DEFAULT_MESSAGE);

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

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_cc_data_path_server", &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	result = register_cc_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register Comm Channel server sample parameters: %s",
			     doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Start the server */
	result = start_cc_data_path_server_sample(server_name, cfg.cc_dev_pci_addr, cfg.cc_dev_rep_pci_addr, cfg.text);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to run the sample: %s", doca_error_get_descr(result));
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
