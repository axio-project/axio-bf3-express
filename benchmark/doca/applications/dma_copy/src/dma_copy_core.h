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

#ifndef DMA_COPY_CORE_H_
#define DMA_COPY_CORE_H_

#include <stdbool.h>

#include <doca_argp.h>
#include <doca_comm_channel.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>


#define MAX_ARG_SIZE 128					/* PCI address and file path maximum length */
#define CC_MAX_MSG_SIZE 4080					/* Comm Channel message maximum size */
#define SERVER_NAME "dma copy server"				/* Comm Channel service name */
#define NUM_DMA_TASKS (1)					/* DMA tasks number */

enum dma_copy_mode {
	DMA_COPY_MODE_HOST,					/* Run endpoint in Host */
	DMA_COPY_MODE_DPU					/* Run endpoint in DPU */
};

struct cc_msg_dma_direction {
	bool file_in_host;					/* Indicate where the source file is located */
	uint64_t file_size;					/* File size in bytes */
};

struct cc_msg_dma_status {
	bool is_success;					/* Indicate success or failure for last message sent */
};

struct dma_copy_cfg {
	enum dma_copy_mode mode;				  /* Node running mode {host, dpu} */
	char file_path[MAX_ARG_SIZE];				  /* File path to copy from (host) or path the save DMA result (dpu) */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	bool is_file_found_locally;				  /* Indicate DMA copy direction */
	uint64_t file_size;					  /* File size in bytes */
};

struct dma_copy_resources {
	struct program_core_objects *state;			/* DOCA core objects */
	struct doca_dma *dma_ctx;				/* DOCA DMA context */
};

/*
 * Register application arguments
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_dma_copy_params(void);

/*
 * Initiate Comm Channel
 *
 * @cfg [in]: Application configuration
 * @ep [out]: DOCA comm_channel endpoint
 * @dev [out]: DOCA device object to use
 * @dev_rep [out]: DOCA device representor object to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_cc(struct dma_copy_cfg *cfg, struct doca_comm_channel_ep_t **ep, struct doca_dev **dev,
	struct doca_dev_rep **dev_rep);

/*
 * Destroy Comm Channel
 *
 * @ep [in]: Comm Channel DOCA endpoint
 * @peer [in]: Comm Channel DOCA address
 * @dev [in]: Comm Channel DOCA device
 * @dev_rep [in]: Comm Channel DOCA device representor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_cc(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t *peer,
			struct doca_dev *dev, struct doca_dev_rep *dev_rep);

/*
 * Open DOCA device for DMA operation
 *
 * @dev [in]: DOCA DMA capable device to open
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t open_dma_device(struct doca_dev **dev);

/*
 * Start DMA operation on the Host
 *
 * @dma_cfg [in]: App configuration structure
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t host_start_dma_copy(struct dma_copy_cfg *dma_cfg, struct doca_comm_channel_ep_t *ep,
				 struct doca_comm_channel_addr_t **peer_addr);

/*
 * Start DMA operation on the DPU
 *
 * @dma_cfg [in]: App configuration structure
 * @ep [in]: Comm Channel endpoint
 * @peer_addr [in]: Comm Channel peer address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t dpu_start_dma_copy(struct dma_copy_cfg *dma_cfg, struct doca_comm_channel_ep_t *ep,
				struct doca_comm_channel_addr_t **peer_addr);

#endif /* DMA_COPY_CORE_H_ */
