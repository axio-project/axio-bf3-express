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

#ifndef FILE_INTEGRITY_CORE_H_
#define FILE_INTEGRITY_CORE_H_

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_sha.h>

#include "comch_utils.h"

#include <samples/common.h>

#define MAX_FILE_NAME 255 /* Max file name */

/* File integrity running mode */
enum file_integrity_mode {
	NO_VALID_INPUT = 0, /* CLI argument is not valid */
	CLIENT,		    /* Run app as client */
	SERVER		    /* Run app as server */
};

/* State of the file transfer on the doca comch control path */
enum transfer_state {
	TRANSFER_IDLE,	      /* No transfer has started */
	TRANSFER_IN_PROGRESS, /* File transfer is under way */
	TRANSFER_COMPLETE,    /* File transfer is complete */
	TRANSFER_ERROR,	      /* An error was detected in file transfer */
};

struct server_runtime_data {
	/* Async file data */
	uint32_t expected_file_chunks; /* Number of chunks file should be sent in */
	uint32_t received_file_chunks; /* Current number of chunks received */
	uint32_t received_file_length; /* Current length of file data received */
	uint32_t expected_sha_len;     /* Length of expected SHA byte array */
	uint8_t *expected_sha;	       /* Expected SHA of file  */

	/* Async SHA data */
	char *sha_src_data;					  /* Raw data buffer containing data to send to SHA */
	struct doca_buf *sha_src_buf;				  /* Doca_buf wrapping sha_src_data */
	struct doca_sha_task_hash *sha_hash_task;		  /* Single task for doing a full SHA */
	struct doca_sha_task_partial_hash *sha_partial_hash_task; /* Single task for use in partial SHA */
	struct program_core_objects *sha_state;			  /* Core state of SHA context */
	size_t active_sha_tasks;				  /* Variable indicating number of active tasks */
	int fd;							  /* File descriptor for writing data to */
};

struct file_integrity_metadata_msg {
	uint32_t total_file_chunks; /* Number of chunks the file will be sent across */
	uint8_t sha_data[];	    /* Expected SHA of transferred file */
};

/* File integrity configuration struct */
struct file_integrity_config {
	enum file_integrity_mode mode;				  /* Mode of operation */
	char file_path[MAX_FILE_NAME];				  /* Input file path */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	int timeout;						  /* Application timeout in seconds */
	struct server_runtime_data server_data; /* Data populated on server side during file transmission */
	enum transfer_state state;		/* Indicator of completion of a file transfer */
};

/*
 * Initialize application resources
 *
 * @app_cfg [in]: application config struct
 * @state [out]: application core object struct
 * @sha_ctx [out]: context of SHA library
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_init(struct file_integrity_config *app_cfg,
				 struct program_core_objects *state,
				 struct doca_sha **sha_ctx);

/*
 * Clean all application resources
 *
 * @state [in]: application core object struct
 * @sha_ctx [in]: context of SHA library
 */
void file_integrity_cleanup(struct program_core_objects *state, struct doca_sha *sha_ctx);

/*
 * Run client logic
 *
 * @comch_cfg [in]: comch configuration object
 * @cfg [in]: application config struct
 * @state [in]: application core object struct
 * @sha_ctx [in]: context of SHA library
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_client(struct comch_cfg *comch_cfg,
				   struct file_integrity_config *cfg,
				   struct program_core_objects *state,
				   struct doca_sha *sha_ctx);

/*
 * Run server logic
 *
 * @comch_cfg [in]: comch configuration object
 * @cfg [in]: application config struct
 * @state [in]: application core object struct
 * @sha_ctx [in]: context of SHA library
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_integrity_server(struct comch_cfg *comch_cfg,
				   struct file_integrity_config *cfg,
				   struct program_core_objects *state,
				   struct doca_sha *sha_ctx);

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_file_integrity_params(void);

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

#endif /* FILE_INTEGRITY_CORE_H_ */
