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

#include <doca_dev.h>

#define MAX_USER_TXT_SIZE 4096					  /* Maximum size of user input text */
#define MAX_TXT_SIZE (MAX_USER_TXT_SIZE + 1)			  /* Maximum size of input text */

struct cc_config {
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	char text[MAX_TXT_SIZE];				  /* Text to send to Comm Channel server */
};

/*
 * Register the command line parameters for the DOCA CC samples
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_cc_params(void);
