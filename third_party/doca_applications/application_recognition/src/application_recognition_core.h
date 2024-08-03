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

#ifndef APPLICATION_RECOGNITION_CORE_H_
#define APPLICATION_RECOGNITION_CORE_H_

#include <doca_dpi.h>

#include <dpi_worker.h>
#include <offload_rules.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FILE_NAME 255			/* Maximal length of file path */

struct ar_config {
	struct application_dpdk_config *dpdk_config;	/* DPDK configuration */
	char cdo_filename[MAX_FILE_NAME];		/* Path to CDO file */
	char csv_filename[MAX_FILE_NAME];		/* Path to CSV file */
	bool print_on_match;				/* Print on match flag */
	bool create_csv;				/* Create CSV flag */
	bool interactive_mode;				/* Interactive mode flag */
	int netflow_source_id;				/* Netflow source ID */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* PCI device address */
};

/*
 * Application Recognition initialization function.
 * Initializes the AR application and creates the DPI context.
 *
 * @ar_config [in]: AR configuration
 * @dpi_worker [in]: DPI worker attributes
 * @return: 0 on success and negative value otherwise
 */
int ar_init(struct ar_config *ar_config, struct dpi_worker_attr *dpi_worker);

/*
 * AR destroy.
 *
 * @ar [in]: application configuration structure
 */
void ar_destroy(struct ar_config *ar);

/*
 * Register the command line parameters for the AR application.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_ar_params(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APPLICATION_RECOGNITION_CORE_H_ */
