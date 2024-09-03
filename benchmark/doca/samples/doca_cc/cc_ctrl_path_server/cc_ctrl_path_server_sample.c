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

#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <doca_cc.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>

#include "cc_ctrl_path_common.h"
#include "common.h"

DOCA_LOG_REGISTER(CC_CTRL_PATH_SERVER);

/* Sample's objects */
struct cc_ctrl_path_server_objects {
	struct doca_dev *hw_dev;	       /* Device used in the sample */
	struct doca_dev_rep *rep_dev;	       /* Device representor used in the sample */
	struct doca_pe *pe;		       /* PE object used in the sample */
	struct doca_cc_server *server;	       /* Server object used in the sample */
	struct doca_cc_connection *connection; /* Connection object used in the sample */
	uint32_t num_connected_clients;	       /* Number of currently connected clients */
	const char *text;		       /* Message to be sent to client */
	doca_error_t result;		       /* Holds result will be updated in callbacks */
	bool finish;			       /* Controls whether progress loop should be run */
};

/**
 * Callback for send task successful completion
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void
send_task_completion_callback(struct doca_cc_send_task *task, union doca_data task_user_data,
			      union doca_data ctx_user_data)
{
	struct cc_ctrl_path_server_objects *sample_objects = (struct cc_ctrl_path_server_objects *)ctx_user_data.ptr;

	/* This argument is not in use */
	(void)task_user_data;

	sample_objects->result = DOCA_SUCCESS;
	DOCA_LOG_INFO("Task sent successfully");

	doca_task_free(doca_cc_send_task_as_task(task));
	(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
}

/**
 * Callback for send task completion with error
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void
send_task_completion_err_callback(struct doca_cc_send_task *task, union doca_data task_user_data,
				  union doca_data ctx_user_data)
{
	struct cc_ctrl_path_server_objects *sample_objects = (struct cc_ctrl_path_server_objects *)ctx_user_data.ptr;

	/* This argument is not in use */
	(void)task_user_data;

	sample_objects->result = doca_task_get_status(doca_cc_send_task_as_task(task));
	DOCA_LOG_ERR("Message failed to send with error = %s", doca_error_get_name(sample_objects->result));

	doca_task_free(doca_cc_send_task_as_task(task));
	(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
}

/**
 * Callback for connection event
 *
 * @event [in]: Connection event object
 * @cc_conn [in]: Connection object
 * @change_success [in]: Whether the connection was successful or not
 */
static void
server_connection_event_callback(struct doca_cc_event_connection_status_changed *event,
				 struct doca_cc_connection *cc_conn, bool change_success)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_ctrl_path_server_objects *sample_objects;
	doca_error_t result;

	/* This argument is not in use */
	(void)event;

	cc_server = doca_cc_server_get_server_ctx(cc_conn);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	/* Update number of connected clients in case of successful connection */
	sample_objects = (struct cc_ctrl_path_server_objects *)user_data.ptr;
	if (!change_success) {
		DOCA_LOG_ERR("Failed connection received");
		return;
	}

	sample_objects->num_connected_clients++;
	DOCA_LOG_INFO("New client connected to server");
}

/**
 * Callback for disconnection event
 *
 * @event [in]: Connection event object
 * @cc_conn [in]: Connection object
 * @change_success [in]: Whether the disconnection was successful or not
 */
static void
server_disconnection_event_callback(struct doca_cc_event_connection_status_changed *event,
				    struct doca_cc_connection *cc_conn, bool change_success)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_ctrl_path_server_objects *sample_objects;
	doca_error_t result;

	/* These arguments are not in use */
	(void)event;
	(void)change_success;

	cc_server = doca_cc_server_get_server_ctx(cc_conn);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	/* Update number of connected clients in case of disconnection, Currently disconnection only happens if server
	 * sent a message to a client which already stopped.
	 */
	sample_objects = (struct cc_ctrl_path_server_objects *)user_data.ptr;
	sample_objects->num_connected_clients--;
	DOCA_LOG_INFO("A client was disconnected from server");
}

/**
 * Send message on server
 *
 * @sample_objects [in]: Sample objects struct
 * @msg_buf [in]: Message to send
 * @msg_len [in]: Length of message to send
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
server_send_pong(struct cc_ctrl_path_server_objects *sample_objects)
{
	struct doca_cc_send_task *task;
	struct doca_task *task_obj;
	union doca_data user_data;
	doca_error_t result;
	const char *text = sample_objects->text;
	size_t msg_len = strnlen(text, CC_MAX_MSG_SIZE);

	/* This function will only be called after a message was received, so connection should be available */
	if (sample_objects->connection == NULL) {
		DOCA_LOG_ERR("Failed to send response: no connection available");
		return DOCA_ERROR_NOT_CONNECTED;
	}

	result = doca_cc_server_send_task_alloc_init(sample_objects->server, sample_objects->connection, text, msg_len,
						     &task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate task in server with error = %s", doca_error_get_name(result));
		return result;
	}

	task_obj = doca_cc_send_task_as_task(task);

	user_data.ptr = (void *)sample_objects;
	doca_task_set_user_data(task_obj, user_data);

	result = doca_task_submit(task_obj);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed submitting send task with error = %s", doca_error_get_name(result));
		doca_task_free(task_obj);
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Callback for message recv event
 *
 * @event [in]: Recv event object
 * @recv_buffer [in]: Message buffer
 * @msg_len [in]: Message len
 * @cc_connection [in]: Connection the message was received on
 */
static void
message_recv_callback(struct doca_cc_event_msg_recv *event, uint8_t *recv_buffer, size_t msg_len,
		      struct doca_cc_connection *cc_connection)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_ctrl_path_server_objects *sample_objects;
	doca_error_t result;

	/* This argument is not in use */
	(void)event;

	cc_server = doca_cc_server_get_server_ctx(cc_connection);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	/* Save the connection that the ping was sent over for sending the response */
	sample_objects = (struct cc_ctrl_path_server_objects *)user_data.ptr;
	sample_objects->connection = cc_connection;

	DOCA_LOG_INFO("Message received: '%.*s'", (int)msg_len, recv_buffer);
	sample_objects->result = server_send_pong(sample_objects);
	if (sample_objects->result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit send task with error = %s", doca_error_get_name(sample_objects->result));
		(void)doca_ctx_stop(doca_cc_server_as_ctx(cc_server));
	}
}

/**
 * Clean all sample resources
 *
 * @sample_objects [in]: Sample objects struct to clean
 */
static void
clean_cc_sample_objects(struct cc_ctrl_path_server_objects *sample_objects)
{
	doca_error_t result;

	if (sample_objects->server != NULL) {
		result = doca_cc_server_destroy(sample_objects->server);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy server properly with error = %s", doca_error_get_name(result));

		sample_objects->server = NULL;
	}

	if (sample_objects->pe != NULL) {
		result = doca_pe_destroy(sample_objects->pe);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy pe properly with error = %s", doca_error_get_name(result));

		sample_objects->pe = NULL;
	}

	if (sample_objects->rep_dev != NULL) {
		result = doca_dev_rep_close(sample_objects->rep_dev);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to close rep device properly with error = %s",
				     doca_error_get_name(result));

		sample_objects->rep_dev = NULL;
	}

	if (sample_objects->hw_dev != NULL) {
		result = doca_dev_close(sample_objects->hw_dev);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to close hw device properly with error = %s", doca_error_get_name(result));

		sample_objects->hw_dev = NULL;
	}
}

/**
 * Callback triggered whenever CC server context state changes
 *
 * @user_data [in]: User data associated with the CC server context. Will hold struct cc_ctrl_path_server_objects *
 * @ctx [in]: The CC server context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void
cc_server_state_changed_callback(const union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state,
				 enum doca_ctx_states next_state)
{
	(void)ctx;
	(void)prev_state;

	struct cc_ctrl_path_server_objects *sample_objects = (struct cc_ctrl_path_server_objects *)user_data.ptr;

	switch (next_state) {
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("CC server context has been stopped");
		/* We can stop the main loop */
		sample_objects->finish = true;
		break;
	case DOCA_CTX_STATE_STARTING:
		/**
		 * The context is in starting state, this is unexpected for CC server.
		 */
		DOCA_LOG_ERR("CC server context entered into starting state. Unexpected transition");
		break;
	case DOCA_CTX_STATE_RUNNING:
		DOCA_LOG_INFO("CC server context is running. Waiting for clients to connect");
		break;
	case DOCA_CTX_STATE_STOPPING:
		/**
		 * The context is in stopping, this can happen when fatal error encountered or when stopping context.
		 * doca_pe_progress() will cause all tasks to be flushed, and finally transition state to idle
		 */
		DOCA_LOG_INFO("CC server context entered into stopping state. Terminating connections with clients");
		break;
	default:
		break;
	}
}

/**
 * Initialize sample resources
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @dev_rep_pci_addr [in]: PCI address for the representor
 * @sample_objects [in]: Sample objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_cc_ctrl_path_server_objects(const char *server_name, const char *dev_pci_addr, const char *dev_rep_pci_addr,
				 struct cc_ctrl_path_server_objects *sample_objects)
{
	doca_error_t result;

	struct cc_ctrl_path_server_cb_config cfg = {.send_task_comp_cb = send_task_completion_callback,
						    .send_task_comp_err_cb = send_task_completion_err_callback,
						    .msg_recv_cb = message_recv_callback,
						    .server_connection_event_cb = server_connection_event_callback,
						    .server_disconnection_event_cb =
							    server_disconnection_event_callback,
						    .data_path_mode = false,
						    .new_consumer_cb = NULL,
						    .expired_consumer_cb = NULL,
						    .ctx_user_data = sample_objects,
						    .ctx_state_changed_cb = cc_server_state_changed_callback};

	/* Open DOCA device according to the given PCI address */
	result = open_doca_device_with_pci(dev_pci_addr, NULL, &(sample_objects->hw_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device based on PCI address");
		return result;
	}

	/* Open DOCA device representor according to the given PCI address */
	result = open_doca_device_rep_with_pci(sample_objects->hw_dev, DOCA_DEVINFO_REP_FILTER_NET, dev_rep_pci_addr,
					       &(sample_objects->rep_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device representor based on PCI address");
		clean_cc_sample_objects(sample_objects);
		return result;
	}

	result = init_cc_ctrl_path_server(server_name, sample_objects->hw_dev, sample_objects->rep_dev, &cfg,
					  &(sample_objects->server), &(sample_objects->pe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Fail init cc server with error = %s", doca_error_get_name(result));
		clean_cc_sample_objects(sample_objects);
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Run cc_server sample
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @rep_pci_addr [in]: PCI address for the representor
 * @text [in]: Message to send to the server
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
start_cc_ctrl_path_server_sample(const char *server_name, const char *dev_pci_addr, const char *rep_pci_addr,
				 const char *text)
{
	doca_error_t result;
	struct cc_ctrl_path_server_objects sample_objects = {0};

	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	sample_objects.text = text;
	sample_objects.finish = false;

	result = init_cc_ctrl_path_server_objects(server_name, dev_pci_addr, rep_pci_addr, &sample_objects);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize sample with error = %s", doca_error_get_name(result));
		return result;
	}

	/* Waiting to receive a message from the client */
	while (!sample_objects.finish) {
		if (doca_pe_progress(sample_objects.pe) == 0)
			nanosleep(&ts, &ts);
	}

	clean_cc_sample_objects(&sample_objects);
	return sample_objects.result;
}
