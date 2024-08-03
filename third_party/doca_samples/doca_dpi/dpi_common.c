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

#include <string.h>
#include <unistd.h>

#include <doca_argp.h>
#include <doca_log.h>

#include "dpi_common.h"

DOCA_LOG_REGISTER(DPI_COMMON);

/*
 * ARGP Callback - Handle signatures file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
sig_file_callback(void *param, void *config)
{
	struct dpi_scan_config *conf = (struct dpi_scan_config *)config;
	const char *sig_file = (char *)param;
	int len;

	if (access(sig_file, F_OK | R_OK) != 0) {
		DOCA_LOG_ERR("Failed to find file path pointed by: %s", sig_file);
		return DOCA_ERROR_INVALID_VALUE;
	}

	len = strnlen(sig_file, MAX_FILE_PATH_SIZE);
	/* Check using >= to make static code analysis satisfied */
	if (len >= MAX_FILE_PATH_SIZE) {
		DOCA_LOG_ERR("Entered file path %s exceeded buffer size of: %d", sig_file,
			     MAX_USER_FILE_PATH_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(conf->sig_file_path, sig_file, len + 1);

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
	struct dpi_scan_config *conf = (struct dpi_scan_config *)config;
	const char *addr = (char *)param;
	int addr_len = strnlen(addr, DOCA_DEVINFO_PCI_ADDR_SIZE);

	/* Check using >= to make static code analysis satisfied */
	if (addr_len >= DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(conf->pci_address, addr, addr_len + 1);

	return DOCA_SUCCESS;
}

doca_error_t
register_dpi_scan_params(void)
{
	doca_error_t result;
	struct doca_argp_param *sig_file_param;
	struct doca_argp_param *pci_address_param;

	/* Create and register signatures file param */
	result = doca_argp_param_create(&sig_file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(sig_file_param, "s");
	doca_argp_param_set_long_name(sig_file_param, "sig-file");
	doca_argp_param_set_description(sig_file_param, "Signatures file path on DPU");
	doca_argp_param_set_callback(sig_file_param, sig_file_callback);
	doca_argp_param_set_type(sig_file_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(sig_file_param);
	result = doca_argp_register_param(sig_file_param);
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

	return DOCA_SUCCESS;
}
