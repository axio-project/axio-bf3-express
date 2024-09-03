/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef CC_COMMON_H_
#define CC_COMMON_H_

#include <doca_cc.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_pe.h>

#define MAX_USER_TXT_SIZE 4096					  /* Maximum size of user input text */
#define MAX_TXT_SIZE (MAX_USER_TXT_SIZE + 1)			  /* Maximum size of input text */
#define SLEEP_IN_NANOS (10 * 1000)				  /* Sample tasks every 10 microseconds */
#define CC_MAX_MSG_SIZE 4080					  /* Comm Channel maximum message size */

struct cc_config {
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
	char text[MAX_TXT_SIZE];				  /* Text to send to Comm Channel server */
};

struct cc_ctrl_path_client_cb_config {
	/* User specified callback when task completed successfully */
	doca_cc_send_task_comp_handler send_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_cc_send_task_comp_handler send_task_comp_err_cb;
	/* User specified callback when a message is received */
	doca_cc_event_msg_recv_cb_t msg_recv_cb;
	/* Whether need to configure data_path related event callback */
	bool data_path_mode;
	/* User specified callback when a new consumer registered */
	doca_cc_event_consumer_cb_t new_consumer_cb;
	/* User specified callback when a consumer expired event occurs */
	doca_cc_event_consumer_cb_t expired_consumer_cb;
	/* User specified context data */
	void *ctx_user_data;
	/* User specified PE context state changed event callback */
	doca_ctx_state_changed_callback_t ctx_state_changed_cb;
};

struct cc_ctrl_path_server_cb_config {
	/* User specified callback when task completed successfully */
	doca_cc_send_task_comp_handler send_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_cc_send_task_comp_handler send_task_comp_err_cb;
	/* User specified callback when a message is received */
	doca_cc_event_msg_recv_cb_t msg_recv_cb;
	/* User specified callback when server receives a new connection */
	doca_cc_event_connection_status_changed_cb_t server_connection_event_cb;
	/* User specified callback when server finds a disconnected connection */
	doca_cc_event_connection_status_changed_cb_t server_disconnection_event_cb;
	/* Whether need to configure data_path related event callback */
	bool data_path_mode;
	/* User specified callback when a new consumer registered */
	doca_cc_event_consumer_cb_t new_consumer_cb;
	/* User specified callback when a consumer expired event occurs */
	doca_cc_event_consumer_cb_t expired_consumer_cb;
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
doca_error_t register_cc_params(void);

/**
 * Clean client and its PE
 *
 * @client [in]: Client object to clean
 * @pe [in]: Client PE object to clean
 */
void clean_cc_ctrl_path_client(struct doca_cc_client *client, struct doca_pe *pe);

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
doca_error_t init_cc_ctrl_path_client(const char *server_name, struct doca_dev *hw_dev,
				      struct cc_ctrl_path_client_cb_config *cb_cfg,
				      struct doca_cc_client **client, struct doca_pe **pe);

/**
 * Clean server and its PE
 *
 * @server [in]: Server object to clean
 * @pe [in]: Server PE object to clean
 */
void clean_cc_ctrl_path_server(struct doca_cc_server *server, struct doca_pe *pe);

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
doca_error_t init_cc_ctrl_path_server(const char *server_name, struct doca_dev *hw_dev, struct doca_dev_rep *rep_dev,
				      struct cc_ctrl_path_server_cb_config *cb_cfg,
				      struct doca_cc_server **server, struct doca_pe **pe);

#endif // CC_COMMON_H_
