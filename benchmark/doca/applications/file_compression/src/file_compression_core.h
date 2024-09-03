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

#ifndef FILE_COMPRESSION_CORE_H_
#define FILE_COMPRESSION_CORE_H_

#include <doca_comm_channel.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_compress.h>

#include <samples/common.h>
#include <samples/doca_compress/compress_common.h>

#define MAX_MSG_SIZE 	(4080)		/* Max comm channel message size */

/* File compression running mode */
enum file_compression_mode {
	NO_VALID_INPUT = 0,	/* CLI argument is not valid */
	CLIENT,			/* Run app as client */
	SERVER			/* Run app as server */
};

/* File compression compress method */
enum file_compression_compress_method {
	COMPRESS_DEFLATE_HW,	/* Compress file using DOCA Compress library */
	COMPRESS_DEFLATE_SW	/* Compress file using zlib */
};

/* File compression configuration struct */
struct file_compression_config {
	enum file_compression_mode mode;			  /* Application mode */
	char file_path[MAX_FILE_NAME];				  /* Input file path */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	int timeout;						  /* Application timeout in seconds */
	enum file_compression_compress_method compress_method;	  /* Whether to run compress with HW or SW */
};

/*
 * Initialize application resources
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @app_cfg [in]: application config struct
 * @resources [out]: DOCA compress resources pointer
 * @max_buf_size [out]: Maximum buffer size allowed for compress operations pointer
 * @method [out]: Compression method to be used
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
file_compression_init(struct doca_comm_channel_ep_t **ep, struct doca_comm_channel_addr_t **peer_addr,
		      struct file_compression_config *app_cfg, struct compress_resources *resources,
		      uint64_t *max_buf_size, enum file_compression_compress_method *method);

/*
 * Clean all application resources
 *
 * @app_cfg [in]: application config struct
 * @ep [in]: handle for comm channel local endpoint
 * @mode [in]: application mode - client or server
 * @peer_addr [in]: destination address handle of the send operation
 * @resources [in]: DOCA compress resources pointer
 */
void
file_compression_cleanup(struct file_compression_config *app_cfg, struct doca_comm_channel_ep_t *ep,
			 enum file_compression_mode mode,
			 struct doca_comm_channel_addr_t **peer_addr, struct compress_resources *resources);

/*
 * Run client logic
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @app_cfg [in]: application config struct
 * @resources [in]: DOCA compress resources pointer
 * @max_buf_size [in]: Maximum buffer size allowed for compress operations
 * @method [in]: Compression method to be used
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
file_compression_client(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
			struct file_compression_config *app_cfg, struct compress_resources *resources,
			uint64_t max_buf_size, enum file_compression_compress_method method);

/*
 * Run server logic
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @app_cfg [in]: application config struct
 * @resources [in]: DOCA compress resources pointer
 * @max_buf_size [in]: Maximum buffer size allowed for compress operations
 * @method [in]: Compression method to be used
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
file_compression_server(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
			struct file_compression_config *app_cfg, struct compress_resources *resources,
			uint64_t max_buf_size, enum file_compression_compress_method method);

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_file_compression_params(void);

#endif /* FILE_COMPRESSION_CORE_H_ */
