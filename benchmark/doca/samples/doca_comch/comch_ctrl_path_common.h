/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#ifndef COMCH_COMMON_H_
#define COMCH_COMMON_H_

#include <stdbool.h>

#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_pe.h>

#define MAX_SAMPLE_TXT_SIZE 4080	       /* Maximum size of user input text for the sample */
#define MAX_TXT_SIZE (MAX_SAMPLE_TXT_SIZE + 1) /* Maximum size of input text */
#define SLEEP_IN_NANOS (10 * 1000)	       /* Sample tasks every 10 microseconds */

struct comch_config {
	char comch_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	     /* Comm Channel DOCA device PCI address */
	char comch_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address
								      */
	char text[MAX_TXT_SIZE];				     /* Text to send to Comm Channel server */
	uint32_t text_size;					     /* Text size to send to Comm Channel server */
};

struct comch_ctrl_path_client_cb_config {
	/* User specified callback when task completed successfully */
	doca_comch_task_send_completion_cb_t send_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_comch_task_send_completion_cb_t send_task_comp_err_cb;
	/* User specified callback when a message is received */
	doca_comch_event_msg_recv_cb_t msg_recv_cb;
	/* Whether need to configure data_path related event callback */
	bool data_path_mode;
	/* User specified callback when a new consumer registered */
	doca_comch_event_consumer_cb_t new_consumer_cb;
	/* User specified callback when a consumer expired event occurs */
	doca_comch_event_consumer_cb_t expired_consumer_cb;
	/* User specified context data */
	void *ctx_user_data;
	/* User specified PE context state changed event callback */
	doca_ctx_state_changed_callback_t ctx_state_changed_cb;
};

struct comch_ctrl_path_server_cb_config {
	/* User specified callback when task completed successfully */
	doca_comch_task_send_completion_cb_t send_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_comch_task_send_completion_cb_t send_task_comp_err_cb;
	/* User specified callback when a message is received */
	doca_comch_event_msg_recv_cb_t msg_recv_cb;
	/* User specified callback when server receives a new connection */
	doca_comch_event_connection_status_changed_cb_t server_connection_event_cb;
	/* User specified callback when server finds a disconnected connection */
	doca_comch_event_connection_status_changed_cb_t server_disconnection_event_cb;
	/* Whether need to configure data_path related event callback */
	bool data_path_mode;
	/* User specified callback when a new consumer registered */
	doca_comch_event_consumer_cb_t new_consumer_cb;
	/* User specified callback when a consumer expired event occurs */
	doca_comch_event_consumer_cb_t expired_consumer_cb;
	/* User specified context data */
	void *ctx_user_data;
	/* User specified PE context state changed event callback */
	doca_ctx_state_changed_callback_t ctx_state_changed_cb;
};

/*
 * Register the command line parameters for the DOCA CC samples
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_comch_params(void);

/**
 * Clean client and its PE
 *
 * @client [in]: Client object to clean
 * @pe [in]: Client PE object to clean
 */
void clean_comch_ctrl_path_client(struct doca_comch_client *client, struct doca_pe *pe);

/**
 * Initialize a cc client and its PE
 *
 * @server_name [in]: Server name to connect to
 * @hw_dev [in]: Device to use
 * @cb_cfg [in]: Client callback configuration
 * @client [out]: Client object struct to initialize
 * @pe [out]: Client PE object struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_comch_ctrl_path_client(const char *server_name,
					 struct doca_dev *hw_dev,
					 struct comch_ctrl_path_client_cb_config *cb_cfg,
					 struct doca_comch_client **client,
					 struct doca_pe **pe);

/**
 * Clean server and its PE
 *
 * @server [in]: Server object to clean
 * @pe [in]: Server PE object to clean
 */
void clean_comch_ctrl_path_server(struct doca_comch_server *server, struct doca_pe *pe);

/**
 * Initialize a cc server and its PE
 *
 * @server_name [in]: Server name to connect to
 * @hw_dev [in]: Device to use
 * @rep_dev [in]: Representor device to use
 * @cb_cfg [in]: Server callback configuration
 * @server [out]: Server object struct to initialize
 * @pe [out]: Server PE object struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_comch_ctrl_path_server(const char *server_name,
					 struct doca_dev *hw_dev,
					 struct doca_dev_rep *rep_dev,
					 struct comch_ctrl_path_server_cb_config *cb_cfg,
					 struct doca_comch_server **server,
					 struct doca_pe **pe);

#endif // COMCH_COMMON_H_
