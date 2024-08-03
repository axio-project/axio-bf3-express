/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef DPI_COMMON_H
#define DPI_COMMON_H

#include <doca_dev.h>

#define MAX_USER_FILE_PATH_SIZE 512				/* Maximum size of user file path */
#define MAX_FILE_PATH_SIZE (MAX_USER_FILE_PATH_SIZE + 1)	/* Maximum size of file path */

/* Configuration struct */
struct dpi_scan_config {
	char sig_file_path[MAX_FILE_PATH_SIZE];		/* Signatures file path */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* PCI device address */
};

/*
 * Register the command line parameters for the DOCA DPI samples
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_dpi_scan_params(void);

#endif /* DPI_COMMON_H */
