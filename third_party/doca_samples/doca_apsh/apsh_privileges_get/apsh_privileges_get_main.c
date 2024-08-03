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
#include <doca_log.h>

#include "apsh_common.h"

DOCA_LOG_REGISTER(PRIVILEGES_GET::MAIN);

/* Sample's Logic */
doca_error_t privileges_get(const char *dma_device_name, const char *pci_vuid, DOCA_APSH_PROCESS_PID_TYPE pid);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char *argv[])
{
	doca_error_t result;
	struct apsh_config apsh_conf = {0};
	bool os_enabled = false;
	bool pid_enabled = true;
	int exit_status = EXIT_FAILURE;

	/* Set the default configuration values (Example values) */
	strcpy(apsh_conf.dma_dev_name, "mlx5_0");
	strcpy(apsh_conf.system_vuid, "MT2125X03335MLNXS0D0F0");
	apsh_conf.pid = 1;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_apsh_privileges_get", &apsh_conf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}

	result = register_apsh_params(os_enabled, pid_enabled);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register sample parameters: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	/* Invoke the sample's logic */
	result = privileges_get(apsh_conf.dma_dev_name, apsh_conf.system_vuid, apsh_conf.pid);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("privileges_get() encountered an error: %s", doca_get_error_string(result));
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
