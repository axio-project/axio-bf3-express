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

	res = doca_devinfo_list_create(&dev_list, &nb_devs);
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load doca devices list. Doca_error value: %d", res);
		return res;
	}

	/* Search */
	for (i = 0; i < nb_devs; i++) {
		res = doca_devinfo_get_is_pci_addr_equal(dev_list[i], pci_addr, &is_addr_equal);
		if (res == DOCA_SUCCESS && is_addr_equal) {

			/* if device can be opened */
			res = doca_dev_open(dev_list[i], retval);
			if (res == DOCA_SUCCESS) {
				doca_devinfo_list_destroy(dev_list);
				return res;
			}
		}
	}

	DOCA_LOG_WARN("Matching device not found");
	res = DOCA_ERROR_NOT_FOUND;

	doca_devinfo_list_destroy(dev_list);
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
	int exit_status = EXIT_FAILURE;
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = 3,
		.port_config.nb_queues = 1,
		.sft_config = {0},
	};
	struct doca_dev *doca_dev;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_flow_switch_to_wire", NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	/* Probe dpdk dev by doca_dev */
	result = open_doca_device_with_pci_mirror("0000:03:00.0", &doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_dpdk_port_probe(doca_dev, "dv_flow_en=2,fdb_def_rule_en=0,representor=pf0vf[0-1]");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_get_error_string(result));
		return result;
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
		DOCA_LOG_ERR("flow_switch_to_wire() encountered an error: %s", doca_get_error_string(result));
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
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
