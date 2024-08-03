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

#ifndef URL_FILTER_CORE_H_
#define URL_FILTER_CORE_H_

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <dpi_worker.h>
#include <offload_rules.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_TXT_INPUT "/tmp/signature.txt"	/* Default raw signature file path */
#define DEFAULT_CDO_OUTPUT "/tmp/signature.cdo" /* Default CDO output file path */

/* URL filter configuration structure */
struct url_filter_config {
	struct application_dpdk_config *dpdk_config;	/* DPDK configuration */
	bool print_on_match;				/* Print on match flag */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* PCI device address */
};

/*
 * Creates empty raw signature file
 *
 * @signature_filename [in]: Signature file path
 */
void create_database(const char *signature_filename);

/*
 * Compiles given raw signature file and load it to DPI engine
 *
 * @signature_filename [in]: Signature file path
 * @cdo_filename [in]: Compiled CDO file name
 */
void compile_and_load_signatures(const char *signature_filename,
		const char *cdo_filename);

/*
 * Creates new signature and add it to the existing raw signature file
 *
 * @signature_filename [in]: Signature file path
 * @msg [in]: Signature message
 * @pcre [in]: PCRE expression
 */
void create_url_signature(const char *signature_filename, const char *msg,
		const char *pcre);

/*
 * URL Filter initialization function.
 * Initializes the URL Filter application and creates the DPI context.
 *
 * @app_dpdk_config [in]: application DPDK configuration values
 * @url_filter_config [in]: URL Filter configuration
 * @dpi_worker [in]: DPI worker attributes
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t url_filter_init(const struct application_dpdk_config *app_dpdk_config,
		struct url_filter_config *url_filter_config, struct dpi_worker_attr *dpi_worker);

/*
 * URL Filter destroy
 */
void url_filter_destroy(void);

/*
 * Register the command line parameters for the URL Filter application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_url_params(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* URL_FILTER_CORE_H_ */
