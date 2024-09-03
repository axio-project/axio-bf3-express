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

#include <doca_argp.h>
#include <doca_flow.h>
#include <doca_log.h>

#include <dpdk_utils.h>

DOCA_LOG_REGISTER(FLOW_LOOPBACK::MAIN);

#define NB_PORTS 2
#define MAC_ADDR_LEN 6

/* Sample's Logic */
doca_error_t flow_loopback(int nb_queues, uint8_t mac_addresses[2][6]);

/*
 * ARGP Callback - Handle MAC addresses parameter
 *
 * @param [in]: Input parameter
 * @opaque [in/out]: MAC address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
mac_addresses_callback(void *param, void *opaque)
{
	char mac1[18];
	char mac2[18];
	uint8_t (*mac_addresses)[MAC_ADDR_LEN] = (uint8_t (*)[MAC_ADDR_LEN])opaque;
	char *mac_addresses_param = (char *)param;

	/* Split the input into two MAC addresses */
	if (sscanf(mac_addresses_param, "%17s %17s", mac1, mac2) != 2) {
		DOCA_LOG_ERR("Invalid input format");
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* Parse the first MAC address */
	if (sscanf(mac1, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		&mac_addresses[0][0], &mac_addresses[0][1], &mac_addresses[0][2],
		&mac_addresses[0][3], &mac_addresses[0][4], &mac_addresses[0][5]) != MAC_ADDR_LEN) {
		DOCA_LOG_ERR("Invalid MAC address format: %s", mac1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/*  Parse the second MAC address */
	if (sscanf(mac2, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		&mac_addresses[1][0], &mac_addresses[1][1], &mac_addresses[1][2], &mac_addresses[1][3],
		&mac_addresses[1][4], &mac_addresses[1][5]) != MAC_ADDR_LEN) {
		DOCA_LOG_ERR("Invalid MAC address format: %s", mac2);
		return DOCA_ERROR_INVALID_VALUE;

	}
	return DOCA_SUCCESS;
}

/*
 * Register the command line parameter for the sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
register_flow_loopback_params(void)
{
	doca_error_t result;
	struct doca_argp_param *mac_addresses_param;

	result = doca_argp_param_create(&mac_addresses_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(mac_addresses_param, "m");
	doca_argp_param_set_long_name(mac_addresses_param, "mac-addresses");
	doca_argp_param_set_description(mac_addresses_param, "The MAC addresses of the ports, used for encapsulation");
	doca_argp_param_set_callback(mac_addresses_param, mac_addresses_callback);
	doca_argp_param_set_type(mac_addresses_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(mac_addresses_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
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
	uint8_t mac_addresses[NB_PORTS][MAC_ADDR_LEN];
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = NB_PORTS,
		.port_config.nb_queues = 1,
		.port_config.nb_hairpin_q = 1,
		.port_config.enable_mbuf_metadata = 1,
		.port_config.lpbk_support = 1,
	};

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

	result = doca_argp_init("doca_flow_loopback", &mac_addresses);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = register_flow_loopback_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register samples params: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* update queues and ports */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update ports and queues");
		goto dpdk_cleanup;
	}

	/* run sample */
	result = flow_loopback(dpdk_config.port_config.nb_queues, mac_addresses);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("flow_loopback() encountered an error: %s", doca_error_get_descr(result));
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
