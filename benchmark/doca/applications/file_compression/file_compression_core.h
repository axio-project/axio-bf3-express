/*
 * Copyright (c) 2022-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef FILE_COMPRESSION_CORE_H_
#define FILE_COMPRESSION_CORE_H_

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_compress.h>

#include "comch_utils.h"

#include <samples/common.h>
#include <samples/doca_compress/compress_common.h>

/* File compression running mode */
enum file_compression_mode {
	NO_VALID_INPUT = 0, /* CLI argument is not valid */
	CLIENT,		    /* Run app as client */
	SERVER		    /* Run app as server */
};

/* File compression compress method */
enum file_compression_compress_method {
	COMPRESS_DEFLATE_HW, /* Compress file using DOCA Compress library */
	COMPRESS_DEFLATE_SW  /* Compress file using zlib */
};

/* State of the file transfer on the doca comch control path */
enum transfer_state {
	TRANSFER_IDLE,	      /* No transfer has started */
	TRANSFER_IN_PROGRESS, /* File transfer is under way */
	TRANSFER_COMPLETE,    /* File transfer is complete */
	TRANSFER_ERROR /* An error was detected in file transfer */,
};

struct server_runtime_data {
	uint64_t expected_checksum;    /* Expected checksum of transferred file */
	uint32_t expected_file_chunks; /* Number of chunks file should be sent in */
	uint32_t received_file_chunks; /* Current number of chunks received */
	uint32_t received_file_length; /* Current length of file data received */
	char *compressed_file;	       /* File received on server */
};

/* File compression configuration struct */
struct file_compression_config {
	enum file_compression_mode mode;			  /* Application mode */
	char file_path[MAX_FILE_NAME];				  /* Input file path */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	int timeout;						  /* Application timeout in seconds */
	enum file_compression_compress_method compress_method;	  /* Whether to run compress with HW or SW */
	uint64_t max_compress_file_len;				  /* Max supported length of compress file */
	struct server_runtime_data server_data; /* Data populated on server side during file transmission */
	enum transfer_state state;		/* Indicator of completion of a file transfer */
};

/*
 * Initialize application resources
 *
 * @compress_cfg [in]: application config struct
 * @resources [out]: DOCA compress resources pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_compression_init(struct file_compression_config *compress_cfg, struct compress_resources *resources);

/*
 * Clean all application resources
 *
 * @compress_cfg [in]: application config struct
 * @resources [in]: DOCA compress resources pointer
 */
void file_compression_cleanup(struct file_compression_config *compress_cfg, struct compress_resources *resources);

/*
 * Run client logic
 *
 * @comch_cfg [in]: comch object to use for control messages
 * @compress_cfg [in]: application config struct
 * @resources [in]: DOCA compress resources pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_compression_client(struct comch_cfg *comch_cfg,
				     struct file_compression_config *compress_cfg,
				     struct compress_resources *resources);

/*
 * Run server logic
 *
 * @comch_cfg [in]: comch object to use for control messages
 * @compress_cfg [in]: application config struct
 * @resources [in]: DOCA compress resources pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_compression_server(struct comch_cfg *comch_cfg,
				     struct file_compression_config *compress_cfg,
				     struct compress_resources *resources);

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_file_compression_params(void);

/*
 * Callback event for client messages
 *
 * @event [in]: message receive event
 * @recv_buffer [in]: array of bytes containing the message data
 * @msg_len [in]: number of bytes in the recv_buffer
 * @comch_connection [in]: comm channel connection over which the event occurred
 */
void client_recv_event_cb(struct doca_comch_event_msg_recv *event,
			  uint8_t *recv_buffer,
			  uint32_t msg_len,
			  struct doca_comch_connection *comch_connection);

/*
 * Callback event for server messages
 *
 * @event [in]: message receive event
 * @recv_buffer [in]: array of bytes containing the message data
 * @msg_len [in]: number of bytes in the recv_buffer
 * @comch_connection [in]: comm channel connection over which the event occurred
 */
void server_recv_event_cb(struct doca_comch_event_msg_recv *event,
			  uint8_t *recv_buffer,
			  uint32_t msg_len,
			  struct doca_comch_connection *comch_connection);
#endif /* FILE_COMPRESSION_CORE_H_ */
