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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <rte_sft.h>
#include <rte_malloc.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <sig_db.h>
#include <samples/common.h>
#include <utils.h>

#include "application_recognition_core.h"

DOCA_LOG_REGISTER(AR::Core);

static struct doca_dpi_worker_ctx *dpi_ctx;	/* Shared DPI context across all workers */

/*
 * Callback function for DPI Worker thread, called when a packet is matched.
 * The action to take is determined by the DPI rule, DROP OR ALLOW.
 *
 * @dpi_result [in]: Result of the DPI match
 * @fid [in]: Flow ID of the packet
 * @user_data [in]: User data passed to the callback function
 * @action [out]: Action to take after the callback function returns
 * @return: 0 on success and negative value otherwise
 */
static int
set_sig_db_on_match(const struct doca_dpi_result *dpi_result, uint32_t fid,
			void *user_data, enum dpi_worker_action *action)
{
	uint32_t sig_id = dpi_result->info.sig_id;
	struct doca_dpi_sig_data sig_data;
	struct ar_config *ar = (struct ar_config *) user_data;
	struct sig_info *data;
	bool print_on_match = ar->print_on_match;
	bool blocked = false;
	doca_error_t result;

	result = doca_dpi_get_signature(dpi_ctx->dpi, dpi_result->info.sig_id, &sig_data);
	if (result != DOCA_SUCCESS)
		return -1;
	if (sig_db_sig_info_get(sig_id, &data) != DOCA_SUCCESS)
		result = sig_db_sig_info_create(sig_id, sig_data.name, dpi_result->info.action == DOCA_DPI_SIG_ACTION_DROP);
	else
		result = sig_db_sig_info_set(sig_id, sig_data.name);

	if (result != DOCA_SUCCESS)
		return -1;

	result = sig_db_sig_info_fids_inc(sig_id);
	if (result != DOCA_SUCCESS)
		return -1;

	result = sig_db_sig_info_get_block_status(sig_id, &blocked);
	if (result != DOCA_SUCCESS)
		return -1;

	if (print_on_match)
		printf_signature(dpi_ctx, sig_id, fid, blocked);

	*action = blocked ? DPI_WORKER_DROP : DPI_WORKER_ALLOW;
	return 0;
}

int
ar_init(struct ar_config *ar_config, struct dpi_worker_attr *dpi_worker)
{
	doca_error_t result;
	struct doca_dev *dev;
	uint64_t max_dpi_depth = (1 << 14); /* Max search depth */

	/* Init signature database */
	result = sig_db_init();
	if (result != DOCA_SUCCESS)
		return -1;

	/* Open DOCA device */
	result = open_doca_device_with_pci(ar_config->pci_address, &dpi_job_is_supported, &dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open device with pci address: %s", doca_get_error_string(result));
		return -1;
	}

	/* Configure RegEx device and queues */
	result = doca_dpi_worker_ctx_create(&dpi_ctx,
				/* Maximum job size in bytes for regex scan match */
				5000,
				/* Max packets per DPI workq
				 * max packets is X percentage of the amount of packets dpdk has per queue in order to avoid case of
				 * underrun in buffers because buffers are kept in dpi workq
				 */
				NUM_MBUFS * MBUFS_WATERMARK,
				/* doca device opened with pci_address */
				dev,
				/* signature files */
				ar_config->cdo_filename);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DPI init failed: %s", doca_get_error_string(result));
		sig_db_destroy();
		return -1;
	}

	/* Configure the attributes for the DPI worker */
	dpi_worker->dpi_on_match = set_sig_db_on_match;
	dpi_worker->user_data = (void *)ar_config;
	dpi_worker->max_dpi_depth = max_dpi_depth;
	dpi_worker->dpi_ctx = dpi_ctx;
	dpi_worker->dpdk_config = ar_config->dpdk_config;

	/* Init DOCA Telemetry netflow plugin */
	if (ar_config->netflow_source_id) {
		if (init_netflow_schema_and_source(ar_config->netflow_source_id, "AR_netflow_metric") != DOCA_SUCCESS) {
			DOCA_LOG_ERR("DOCA Telemetry Netflow init failed");
			doca_dpi_worker_ctx_destroy(dpi_ctx);
			dpi_ctx = NULL;
			sig_db_destroy();
			return -1;
		}
		dpi_worker->send_netflow_record = enqueue_netflow_record_to_ring;
	}
	return 0;
}

void
ar_destroy(struct ar_config *ar)
{
	dpi_worker_lcores_stop(dpi_ctx);

	sig_db_destroy();

	if (ar->netflow_source_id)
		destroy_netflow_schema_and_source();

	doca_dpi_worker_ctx_destroy(dpi_ctx);
	dpi_ctx = NULL;
}

/*
 * ARGP Callback - Handle CDO file path parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
cdo_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;
	char *cdo_path = (char *) param;

	if (strnlen(cdo_path, MAX_FILE_NAME) == MAX_FILE_NAME) {
		DOCA_LOG_ERR("CDO file name is too long - MAX=%d", MAX_FILE_NAME - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (access(cdo_path, F_OK) == -1) {
		DOCA_LOG_ERR("CDO file not found %s", cdo_path);
		return DOCA_ERROR_NOT_FOUND;
	}
	strlcpy(ar->cdo_filename, cdo_path, MAX_FILE_NAME);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle output CSV path parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
csv_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;
	char *csv_path = (char *) param;

	if (strlen(csv_path) == 0)
		return DOCA_SUCCESS;
	if (strnlen(csv_path, MAX_FILE_NAME) == MAX_FILE_NAME) {
		DOCA_LOG_ERR("CSV file name is too long - MAX=%d", MAX_FILE_NAME - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strlcpy(ar->csv_filename, csv_path, MAX_FILE_NAME);
	ar->create_csv = true;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle print on match parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
print_match_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;

	ar->print_on_match = *(bool *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle interactive mode parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
interactive_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;

	ar->interactive_mode = *(bool *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle fragmented packet parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
frag_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;

	ar->dpdk_config->sft_config.enable_frag = *(bool *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Netflow source ID parameter.
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
netflow_callback(void *param, void *config)
{
	struct ar_config *ar = (struct ar_config *) config;

	ar->netflow_source_id =  *(int *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle pci address of the doca_dpi device
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
pci_callback(void *param, void *config)
{
	struct ar_config *conf = (struct ar_config *) config;
	const char *addr = (char *)param;
	int addr_len = strnlen(addr, DOCA_DEVINFO_PCI_ADDR_SIZE);

	if (addr_len == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	strlcpy(conf->pci_address, addr, DOCA_DEVINFO_PCI_ADDR_SIZE);

	return DOCA_SUCCESS;
}

doca_error_t
register_ar_params(void)
{
	doca_error_t result;
	struct doca_argp_param *print_on_match_param, *netflow_param, *interactive_param, *csv_param, *cdo_param, *frag_param;
	struct doca_argp_param *pci_address_param;

	/* Create and register print on match param */
	result = doca_argp_param_create(&print_on_match_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(print_on_match_param, "p");
	doca_argp_param_set_long_name(print_on_match_param, "print-match");
	doca_argp_param_set_description(print_on_match_param, "Prints FID when matched in DPI engine");
	doca_argp_param_set_callback(print_on_match_param, print_match_callback);
	doca_argp_param_set_type(print_on_match_param, DOCA_ARGP_TYPE_BOOLEAN);
	result = doca_argp_register_param(print_on_match_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register Netflow param */
	result = doca_argp_param_create(&netflow_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(netflow_param, "n");
	doca_argp_param_set_long_name(netflow_param, "netflow");
	doca_argp_param_set_arguments(netflow_param, "<source_id>");
	doca_argp_param_set_description(netflow_param, "Collect netflow statistics and set source_id if value is set");
	doca_argp_param_set_callback(netflow_param, netflow_callback);
	doca_argp_param_set_type(netflow_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(netflow_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register interactive mode param */
	result = doca_argp_param_create(&interactive_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(interactive_param, "i");
	doca_argp_param_set_long_name(interactive_param, "interactive");
	doca_argp_param_set_description(interactive_param, "Adds interactive mode for blocking signatures");
	doca_argp_param_set_callback(interactive_param, interactive_callback);
	doca_argp_param_set_type(interactive_param, DOCA_ARGP_TYPE_BOOLEAN);
	result = doca_argp_register_param(interactive_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register CSV output param */
	result = doca_argp_param_create(&csv_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(csv_param, "o");
	doca_argp_param_set_long_name(csv_param, "output-csv");
	doca_argp_param_set_arguments(csv_param, "<path>");
	doca_argp_param_set_description(csv_param, "Path to the output of the CSV file");
	doca_argp_param_set_callback(csv_param, csv_callback);
	doca_argp_param_set_type(csv_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(csv_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register CDO file param */
	result = doca_argp_param_create(&cdo_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(cdo_param, "c");
	doca_argp_param_set_long_name(cdo_param, "cdo");
	doca_argp_param_set_arguments(cdo_param, "<path>");
	doca_argp_param_set_description(cdo_param, "Path to CDO file compiled from a valid PDD");
	doca_argp_param_set_callback(cdo_param, cdo_callback);
	doca_argp_param_set_type(cdo_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(cdo_param);
	result = doca_argp_register_param(cdo_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register fragmented param */
	result = doca_argp_param_create(&frag_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(frag_param, "f");
	doca_argp_param_set_long_name(frag_param, "fragmented");
	doca_argp_param_set_description(frag_param, "Enables processing fragmented packets");
	doca_argp_param_set_callback(frag_param, frag_callback);
	doca_argp_param_set_type(frag_param, DOCA_ARGP_TYPE_BOOLEAN);
	result = doca_argp_register_param(frag_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register PCI address param */
	result = doca_argp_param_create(&pci_address_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(pci_address_param, "a");
	doca_argp_param_set_long_name(pci_address_param, "pci-addr");
	doca_argp_param_set_description(pci_address_param, "DOCA DPI device PCI address");
	doca_argp_param_set_callback(pci_address_param, pci_callback);
	doca_argp_param_set_type(pci_address_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(pci_address_param);
	result = doca_argp_register_param(pci_address_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_get_error_string(result));
		return result;
	}
	return DOCA_SUCCESS;
}
