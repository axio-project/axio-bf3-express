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
#include <string.h>

#include <doca_argp.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>

#include "eth_common.h"

#define HEX_BASE 16

DOCA_LOG_REGISTER(ETH_TXQ_LSO_SEND_ETHERNET_FRAMES::MAIN);

/* Configuration struct */
struct eth_txq_cfg {
	char ib_dev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];		/* DOCA IB device name */
	uint8_t dest_mac_address[DOCA_DEVINFO_MAC_ADDR_SIZE];	/* destination MAC address */
};

/* Sample's Logic */
doca_error_t eth_txq_lso_send_ethernet_frames(const char *ib_dev_name, uint8_t *dest_mac_addr);

/*
 * ARGP Callback - Handle IB device name parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
device_address_callback(void *param, void *config)
{
	struct eth_txq_cfg *eth_txq_cfg = (struct eth_txq_cfg *)config;

	return extract_ibdev_name((char *)param, eth_txq_cfg->ib_dev_name);
}

/*
 * ARGP Callback - Handle MAC address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
mac_addr_callback(void *param, void *config)
{
	struct eth_txq_cfg *eth_txq_cfg = (struct eth_txq_cfg *)config;

	return extract_mac_addr((char *)param, eth_txq_cfg->dest_mac_address);
}

/*
 * Register the command line parameters for the sample.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
register_eth_txq_params(void)
{
	doca_error_t result;
	struct doca_argp_param *dev_ib_name_param, *mac_param;

	result = doca_argp_param_create(&dev_ib_name_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	doca_argp_param_set_short_name(dev_ib_name_param, "d");
	doca_argp_param_set_long_name(dev_ib_name_param, "device");
	doca_argp_param_set_description(dev_ib_name_param, "IB device name - default: mlx5_0");
	doca_argp_param_set_callback(dev_ib_name_param, device_address_callback);
	doca_argp_param_set_type(dev_ib_name_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(dev_ib_name_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_argp_param_create(&mac_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(mac_param, "m");
	doca_argp_param_set_long_name(mac_param, "mac-addr");
	doca_argp_param_set_description(mac_param, "Destination MAC address to associate with the ethernet frames - default: FF:FF:FF:FF:FF:FF");
	doca_argp_param_set_callback(mac_param, mac_addr_callback);
	doca_argp_param_set_type(mac_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(mac_param);
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
	struct eth_txq_cfg eth_txq_cfg;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;
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

	strcpy(eth_txq_cfg.ib_dev_name, "mlx5_0");
	for (i = 0; i < DOCA_DEVINFO_MAC_ADDR_SIZE; i++)
		eth_txq_cfg.dest_mac_address[i] = 0xff;

	result = doca_argp_init("doca_eth_txq_lso_send_ethernet_frames", &eth_txq_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	result = register_eth_txq_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register ARGP params: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = eth_txq_lso_send_ethernet_frames(eth_txq_cfg.ib_dev_name, eth_txq_cfg.dest_mac_address);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("eth_txq_lso_send_ethernet_frames() encountered an error: %s", doca_error_get_descr(result));
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
