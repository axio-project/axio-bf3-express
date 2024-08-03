/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/wait.h>

#include <rte_compat.h>
#include <rte_malloc.h>
#include <rte_hash.h>
#include <rte_jhash.h>
#include <rte_sft.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <samples/common.h>
#include <utils.h>

#include "url_filter_core.h"

#define COMPILER_PATH "/opt/mellanox/doca/tools/doca_dpi_compiler"	/* DOCA DPI compiler path */
#define MAX_COMMAND_LENGTH 255						/* Maximum length of each command */

DOCA_LOG_REGISTER(UFLTR::Core);

static uint32_t global_sig_id;		/* Global signature ID */
static struct doca_dpi_worker_ctx *dpi_ctx;	/* Shared DPI context across all workers */

void
create_database(const char *signature_filename)
{
	FILE *url_signature_file;
	int errno_output;

	if (remove(signature_filename) != 0) {
		errno_output = errno;
		DOCA_LOG_DBG("File removal failed : error %d", errno_output);
	}
	url_signature_file = fopen(signature_filename, "w");
	if (url_signature_file == NULL) {
		DOCA_LOG_ERR("Failed to open signature file");
		return;
	}
	fclose(url_signature_file);
	global_sig_id = 1;
}

void
compile_and_load_signatures(const char *signature_filename, const char *cdo_filename)
{
	int status, errno_output;
	char command_buffer[MAX_COMMAND_LENGTH];

	if (access(signature_filename, F_OK) != 0) {
		DOCA_LOG_ERR("Signature file is missing - check PATH=%s\n or \"create database\"",
		signature_filename);
		return;
	}
	status = snprintf(command_buffer, MAX_COMMAND_LENGTH, "%s -i %s -o %s -f suricata",
		COMPILER_PATH, signature_filename, cdo_filename);
	if (status == MAX_COMMAND_LENGTH) {
		DOCA_LOG_ERR("File path too long, please shorten and try again");
		return;
	}
	status = system(command_buffer);
	if (status != 0) {
		errno_output = errno;
		DOCA_LOG_ERR("Signature file compilation failed: error %d", errno_output);
		return;
	}
	if (doca_dpi_set_signatures(dpi_ctx->dpi, cdo_filename) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Loading DPI signature failed");
}

void
create_url_signature(const char *signature_filename, const char *msg, const char *pcre)
{
	FILE *url_signature_file;
	uint32_t sig_id = global_sig_id;

	url_signature_file = fopen(signature_filename, "a");
	if (url_signature_file == NULL) {
		DOCA_LOG_ERR("Failed to open signature file");
		return;
	}

	fprintf(url_signature_file, "drop tcp any any -> any any (msg:\"%s\"; flow:to_server; ",
	msg);
	fprintf(url_signature_file, "pcre:\"/%s/I\"; sid:%d;)\n", pcre, sig_id);
	fprintf(url_signature_file, "drop tcp any any -> any any (msg:\"%s\"; flow:to_server; ",
	msg);
	fprintf(url_signature_file, "tls.sni; pcre:\"/%s/\"; sid:%d;)\n", pcre, sig_id + 1);
	fclose(url_signature_file);

	DOCA_LOG_DBG("Created sig_id %d and %d", sig_id, sig_id + 1);

	global_sig_id += 2;
}

/*
 * Callback function for DPI Worker thread, called when a packet is matched.
 * The action to take is determined by the DPI rule (signature), DROP OR ALLOW.
 *
 * @res [in]: Result of the DPI match
 * @fid [in]: Flow ID of the packet
 * @user_data [in]: User data passed to the callback function
 * @action [out]: Action to take after the callback function returns
 * @return: 0 on success and negative on failure
 */
static int
drop_on_match(const struct doca_dpi_result *res, uint32_t fid,
	      void *user_data, enum dpi_worker_action *action)
{
	doca_error_t result;
	struct doca_dpi_sig_data sig_data;
	uint32_t sig_id = res->info.sig_id;
	bool print_on_match = ((struct url_filter_config *)user_data)->print_on_match;

	if (print_on_match) {
		result = doca_dpi_get_signature(dpi_ctx->dpi, sig_id, &sig_data);
		if (result != DOCA_SUCCESS)
			return -1;
		DOCA_LOG_INFO("SIG ID: %u, URL MSG: %s, SFT_FID: %u", sig_id, sig_data.name, fid);
	}
	*action = res->info.action == DOCA_DPI_SIG_ACTION_DROP ? DPI_WORKER_DROP : DPI_WORKER_ALLOW;
	return 0;
}

doca_error_t
url_filter_init(const struct application_dpdk_config *app_dpdk_config,
		struct url_filter_config *url_filter_config, struct dpi_worker_attr *dpi_worker)
{
	doca_error_t result;
	struct doca_dev *dev;
	uint64_t max_dpi_depth = (1 << 14); /* Max search depth */

	(void)app_dpdk_config;

	/* Check that the compiler is present */
	if (access(COMPILER_PATH, F_OK) != 0) {
		DOCA_LOG_ERR("Compiler is missing - check PATH: %s", COMPILER_PATH);
		return DOCA_ERROR_NOT_FOUND;
	}

	/* Open DOCA device */
	result = open_doca_device_with_pci(url_filter_config->pci_address, &dpi_job_is_supported, &dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open device with pci address: %s", doca_get_error_string(result));
		return result;
	}

	/* Initialization of DPI library */
	result = doca_dpi_worker_ctx_create(&dpi_ctx,
				/* Maximum job size in bytes for regex scan match */
				5000,
				/* Max packets per DPI workq
				 * max packets is X percentage of the amount of packets dpdk has per queue in order to avoid case of
				 * underrun in buffers because buffers are kept in dpi workq
				 */
				NUM_MBUFS * MBUFS_WATERMARK,
				/* DOCA device opened with pci_address */
				dev,
				/* Signature file for url_filter is empty at the very start */
				NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DPI init failed: %s", doca_get_error_string(result));
		return result;
	}

	/* Starting main process on all available cores */
	dpi_worker->dpi_on_match = drop_on_match;
	dpi_worker->user_data = (void *)url_filter_config;
	dpi_worker->max_dpi_depth = max_dpi_depth;
	dpi_worker->dpi_ctx = dpi_ctx;
	dpi_worker->dpdk_config = url_filter_config->dpdk_config;
	return DOCA_SUCCESS;
}

void
url_filter_destroy(void)
{
	if (remove(DEFAULT_CDO_OUTPUT) != 0 && errno != ENOENT)
		DOCA_LOG_DBG("File removal failed: error %d", errno);

	dpi_worker_lcores_stop(dpi_ctx);
	doca_dpi_worker_ctx_destroy(dpi_ctx);
	dpi_ctx = NULL;
}

/*
 * ARGP Callback - Handle print on match parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
print_match_callback(void *param, void *config)
{
	struct url_filter_config *url = (struct url_filter_config *) config;

	url->print_on_match = *(bool *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle fragmented packet parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
frag_callback(void *param, void *config)
{
	struct url_filter_config *url = (struct url_filter_config *) config;

	url->dpdk_config->sft_config.enable_frag = *(bool *) param;
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
	struct url_filter_config *conf = (struct url_filter_config *) config;
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
register_url_params(void)
{
	doca_error_t result;
	struct doca_argp_param *print_on_match_param, *frag_param;
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
