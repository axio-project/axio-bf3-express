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

#ifndef FILE_INTEGRITY_CORE_H_
#define FILE_INTEGRITY_CORE_H_

#include <doca_comm_channel.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_sha.h>

#include <samples/common.h>

#define MAX_MSG_SIZE 4032			/* Max comm channel message size */
#define MAX_FILE_NAME 255			/* Max file name */

/* File integrity running mode */
enum file_integrity_mode {
	NO_VALID_INPUT = 0,	/* CLI argument is not valid */
	CLIENT,			/* Run app as client */
	SERVER			/* Run app as server */
};

/* File integrity configuration struct */
struct file_integrity_config {
	enum file_integrity_mode mode;				  /* Mode of operation */
	char file_path[MAX_FILE_NAME];				  /* Input file path */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	int timeout;						  /* Application timeout in seconds */
};

/*
 * Initialize application resources
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @app_cfg [in]: application config struct
 * @state [out]: application core object struct
 * @sha_ctx [out]: context of SHA library
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_init(struct doca_comm_channel_ep_t **ep,
				 struct doca_comm_channel_addr_t **peer_addr,
				 struct file_integrity_config *app_cfg,
				 struct program_core_objects *state,
				 struct doca_sha **sha_ctx);

/*
 * Clean all application resources
 *
 * @state [in]: application core object struct
 * @sha_ctx [in]: context of SHA library
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [out]: destination address handle of the send operation
 */
void file_integrity_cleanup(struct program_core_objects *state,
			    struct doca_sha *sha_ctx,
			    struct doca_comm_channel_ep_t *ep,
			    struct doca_comm_channel_addr_t **peer_addr);

/*
 * Run client logic
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @cfg [in]: application config struct
 * @state [in]: application core object struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_client(struct doca_comm_channel_ep_t *ep,
				   struct doca_comm_channel_addr_t **peer_addr,
				   struct file_integrity_config *cfg,
				   struct program_core_objects *state);

/*
 * Run server logic
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @cfg [in]: application config struct
 * @state [in]: application core object struct
 * @sha_ctx [in]: context of SHA library
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_server(struct doca_comm_channel_ep_t *ep,
				   struct doca_comm_channel_addr_t **peer_addr,
				   struct file_integrity_config *cfg,
				   struct program_core_objects *state,
				   struct doca_sha *sha_ctx);

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_file_integrity_params(void);

#endif /* FILE_INTEGRITY_CORE_H_ */
