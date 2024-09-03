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
#include <doca_flow.h>
#include <doca_log.h>
#include <doca_ctx.h>
#include <doca_dpdk.h>

#include <dpdk_utils.h>

DOCA_LOG_REGISTER(FLOW_SWITCH_TO_WIRE::MAIN);

#define SWITCH_TO_WIRE_DEF_PCI_ID "0000:00:00.0"

/* Sample's Logic */
doca_error_t flow_switch_to_wire(int nb_queues, int nb_ports, struct doca_dev *doca_dev);

/*
 * Sample open doca device
 *
 * @pci_addr [in]: PCI device address
 * @retval [in]: the opened device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
open_doca_device_with_pci_mirror(const char *pci_addr, struct doca_dev **retval)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs;
	uint8_t is_addr_equal = 0;
	int res;
	size_t i;

	/* Set default return value */
	*retval = NULL;

	res = doca_devinfo_create_list(&dev_list, &nb_devs);
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load doca devices list. Doca_error value: %d", res);
		return res;
	}

	/* Search */
	for (i = 0; i < nb_devs; i++) {
		res = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
		if (res == DOCA_SUCCESS && is_addr_equal) {

			/* if device can be opened */
			res = doca_dev_open(dev_list[i], retval);
			if (res == DOCA_SUCCESS) {
				doca_devinfo_destroy_list(dev_list);
				return res;
			}
		}
	}

	DOCA_LOG_WARN("Matching device not found");
	res = DOCA_ERROR_NOT_FOUND;

	doca_devinfo_destroy_list(dev_list);
	return res;
}

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
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = 3,
		.port_config.nb_queues = 1,
	};
	struct doca_dev *doca_dev;
	char *new_argv[argc];
	char *allow_arg = NULL;
	char *dev_arg = NULL;
	int i;

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

	memset(new_argv, 0, sizeof(new_argv));
	for (i = 0; i < argc; i++) {
		new_argv[i] = argv[i];
		if (!strcmp(argv[i], "-a")) {
			if (allow_arg)
				free(allow_arg);

			/*
			 * Replace doca_flow arg to empty ID as it will be probed
			 * by doca_dev.
			 */
			allow_arg = strdup(argv[i + 1]);
			if (!allow_arg)
				goto sample_exit;
			new_argv[i + 1] = SWITCH_TO_WIRE_DEF_PCI_ID;

			/* Split orignal dev_arg with ID and arg */
			dev_arg = strchr(allow_arg, ',');
			if (!dev_arg)
				goto sample_exit;
			*dev_arg = '\0';
			dev_arg++;

			/* Skip dev_arg as it is replaced */
			i++;
			continue;
		}
	}

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_flow_switch_to_wire", NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = doca_argp_start(argc, new_argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Probe dpdk dev by doca_dev */
	result = open_doca_device_with_pci_mirror(allow_arg, &doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_dpdk_port_probe(doca_dev, dev_arg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* update queues and ports */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update ports and queues");
		goto dpdk_cleanup;
	}

	/* run sample */
	result = flow_switch_to_wire(dpdk_config.port_config.nb_queues, dpdk_config.port_config.nb_ports, doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("flow_switch_to_wire() encountered an error: %s", doca_error_get_descr(result));
		goto dpdk_ports_queues_cleanup;
	}

	exit_status = EXIT_SUCCESS;

dpdk_ports_queues_cleanup:
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
	dpdk_fini();
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (allow_arg)
		free(allow_arg);
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
