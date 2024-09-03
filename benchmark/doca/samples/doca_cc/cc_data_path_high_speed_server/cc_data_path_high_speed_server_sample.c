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

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_cc.h>
#include <doca_cc_consumer.h>
#include <doca_cc_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include "cc_ctrl_path_common.h"
#include "cc_data_path_high_speed_common.h"
#include "common.h"

DOCA_LOG_REGISTER(CC_DATA_PATH_HIGH_SPEED_SERVER);

/* Sample's objects */
struct cc_data_path_server_objects {
	struct doca_dev *hw_dev;		/* Device used in the sample */
	struct doca_dev_rep *rep_dev;		/* Device representor used in the sample */
	struct doca_pe *pe;			/* PE object used in the sample */
	struct doca_cc_server *server;		/* Server object used in the sample*/
	struct doca_cc_connection *connection;	/* Connection object used in the sample*/
	doca_error_t server_result;		/* Holds result will be updated in server callbacks */
	bool server_finish;			/* Controls whether server progress loop should be run */
	bool data_path_test_started;		/* Indicate whether we can start data_path test */
	bool data_path_test_stopped;		/* Indicate whether we can stop data_path test */
	struct cc_data_path_objects *data_path; /* Data path objects */
};

/**
 * Callback for server send task successful completion
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void
server_send_task_completion_callback(struct doca_cc_send_task *task, union doca_data task_user_data,
				     union doca_data ctx_user_data)
{
	struct cc_data_path_server_objects *sample_objects;

	(void)task_user_data;

	sample_objects = (struct cc_data_path_server_objects *)ctx_user_data.ptr;
	sample_objects->server_result = DOCA_SUCCESS;
	DOCA_LOG_INFO("Server task sent successfully");
	doca_task_free(doca_cc_send_task_as_task(task));
}

/**
 * Callback for server send task completion with error
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void
server_send_task_completion_err_callback(struct doca_cc_send_task *task, union doca_data task_user_data,
					 union doca_data ctx_user_data)
{
	struct cc_data_path_server_objects *sample_objects;

	(void)task_user_data;

	sample_objects = (struct cc_data_path_server_objects *)ctx_user_data.ptr;
	sample_objects->server_result = doca_task_get_status(doca_cc_send_task_as_task(task));
	DOCA_LOG_ERR("Message failed to send with error = %s", doca_error_get_name(sample_objects->server_result));
	doca_task_free(doca_cc_send_task_as_task(task));
	(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
}

/**
 * Server sends a message to client
 *
 * @sample_objects [in]: The sample object to use
 * @msg [in]: The msg to send
 * @len [in]: The msg length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
server_send_msg(struct cc_data_path_server_objects *sample_objects, const char *msg, size_t len)
{
	doca_error_t result;
	struct doca_cc_send_task *task;

	result = doca_cc_server_send_task_alloc_init(sample_objects->server, sample_objects->connection, (void *)msg,
						     len, &task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate server task with error = %s", doca_error_get_name(result));
		return result;
	}

	result = doca_task_submit(doca_cc_send_task_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to send server task with error = %s", doca_error_get_name(result));
		doca_task_free(doca_cc_send_task_as_task(task));
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Callback for server message recv event
 *
 * @event [in]: Recv event object
 * @recv_buffer [in]: Message buffer
 * @msg_len [in]: Message len
 * @cc_connection [in]: Connection the message was received on
 */
static void
server_message_recv_callback(struct doca_cc_event_msg_recv *event, uint8_t *recv_buffer, size_t msg_len,
			     struct doca_cc_connection *cc_connection)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_data_path_server_objects *sample_objects;
	doca_error_t result;

	(void)event;

	DOCA_LOG_INFO("Message received: '%.*s'", (int)msg_len, recv_buffer);

	cc_server = doca_cc_server_get_server_ctx(cc_connection);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	sample_objects = (struct cc_data_path_server_objects *)user_data.ptr;
	sample_objects->connection = cc_connection;

	if ((msg_len == strlen(STR_START_DATA_PATH_TEST)) &&
	    (strncmp(STR_START_DATA_PATH_TEST, (char *)recv_buffer, msg_len) == 0)) {
		result = server_send_msg(sample_objects, STR_START_DATA_PATH_TEST, strlen(STR_START_DATA_PATH_TEST));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit send task with error = %s", doca_error_get_name(result));
			(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
			return;
		}
		sample_objects->data_path_test_started = true;
	} else if ((msg_len == strlen(STR_STOP_DATA_PATH_TEST)) &&
		   (strncmp(STR_STOP_DATA_PATH_TEST, (char *)recv_buffer, msg_len) == 0)) {
		result = server_send_msg(sample_objects, STR_STOP_DATA_PATH_TEST, strlen(STR_STOP_DATA_PATH_TEST));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit send task with error = %s", doca_error_get_name(result));
			(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
			return;
		}
		sample_objects->data_path_test_stopped = true;
		(void)doca_ctx_stop(doca_cc_server_as_ctx(sample_objects->server));
	}
}

/**
 * Callback for connection event
 *
 * @event [in]: Connection event object
 * @cc_connection [in]: Connection object
 * @change_success [in]: Whether the connection was successful or not
 */
static void
server_connection_event_callback(struct doca_cc_event_connection_status_changed *event,
				 struct doca_cc_connection *cc_connection, bool change_success)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_data_path_server_objects *sample_objects;
	doca_error_t result;

	if (change_success != true) {
		DOCA_LOG_ERR("Failed connection received");
		return;
	}

	(void)event;

	cc_server = doca_cc_server_get_server_ctx(cc_connection);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	sample_objects = (struct cc_data_path_server_objects *)user_data.ptr;
	sample_objects->connection = cc_connection;
}

/**
 * Callback for disconnection event
 *
 * @event [in]: Connection event object
 * @cc_connection [in]: Connection object
 * @change_success [in]: Whether the disconnection was successful or not
 */
static void
server_disconnection_event_callback(struct doca_cc_event_connection_status_changed *event,
				    struct doca_cc_connection *cc_connection, bool change_success)
{
	(void)event;
	(void)cc_connection;

	if (change_success != true)
		DOCA_LOG_ERR("Failed disconnection received");
}

/**
 * Callback triggered whenever CC server context state changes
 *
 * @user_data [in]: User data associated with the CC server context.
 * @ctx [in]: The CC server context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void
server_state_changed_callback(const union doca_data user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state,
			      enum doca_ctx_states next_state)
{
	(void)ctx;
	(void)prev_state;

	struct cc_data_path_server_objects *sample_objects = (struct cc_data_path_server_objects *)user_data.ptr;

	switch (next_state) {
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("CC server context has been stopped");
		/* We can stop the main loop */
		sample_objects->server_finish = true;
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
 * Callback for new consumer arrival event
 *
 * @event [in]: New remote consumer event object
 * @cc_connection [in]: The connection related to the consumer
 * @id [in]: The ID of the new remote consumer
 */
static void
new_consumer_callback(struct doca_cc_event_consumer *event, struct doca_cc_connection *cc_connection, uint32_t id)
{
	union doca_data user_data;
	struct doca_cc_server *cc_server;
	struct cc_data_path_server_objects *sample_objects;
	doca_error_t result;

	/* This argument is not in use */
	(void)event;

	cc_server = doca_cc_server_get_server_ctx(cc_connection);

	result = doca_ctx_get_user_data(doca_cc_server_as_ctx(cc_server), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	sample_objects = (struct cc_data_path_server_objects *)(user_data.ptr);
	sample_objects->data_path->remote_consumer_id = id;

	DOCA_LOG_INFO("Got a new remote consumer with ID = [%d]", id);
}

/**
 * Callback for expired consumer arrival event
 *
 * @event [in]: Expired remote consumer event object
 * @cc_connection [in]: The connection related to the consumer
 * @id [in]: The ID of the expired remote consumer
 */
void
expired_consumer_callback(struct doca_cc_event_consumer *event, struct doca_cc_connection *cc_connection, uint32_t id)
{
	/* These arguments are not in use */
	(void)event;
	(void)cc_connection;
	(void)id;
}

/**
 * Clean all sample resources
 *
 * @sample_objects [in]: Sample objects struct to clean
 */
static void
clean_cc_data_path_server_objects(struct cc_data_path_server_objects *sample_objects)
{
	doca_error_t result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	if (sample_objects == NULL)
		return;

	/* Exchange message with server to make connection is reliable */
	while (sample_objects->data_path_test_stopped == false) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}
	while (sample_objects->server_finish == false) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}

	clean_cc_ctrl_path_server(sample_objects->server, sample_objects->pe);
	sample_objects->server = NULL;
	sample_objects->pe = NULL;

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
 * Initialize sample resources
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @dev_rep_pci_addr [in]: PCI address for the representor
 * @text [in]: Message to send to the client
 * @sample_objects [in]: Sample objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_cc_data_path_server_objects(const char *server_name, const char *dev_pci_addr, const char *dev_rep_pci_addr,
				 const char *text, struct cc_data_path_server_objects *sample_objects)
{
	doca_error_t result;
	struct cc_ctrl_path_server_cb_config server_cb_cfg = {
		.send_task_comp_cb = server_send_task_completion_callback,
		.send_task_comp_err_cb = server_send_task_completion_err_callback,
		.msg_recv_cb = server_message_recv_callback,
		.server_connection_event_cb = server_connection_event_callback,
		.server_disconnection_event_cb = server_disconnection_event_callback,
		.data_path_mode = true,
		.new_consumer_cb = new_consumer_callback,
		.expired_consumer_cb = expired_consumer_callback,
		.ctx_user_data = sample_objects,
		.ctx_state_changed_cb = server_state_changed_callback};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	sample_objects->data_path->text = text;

	/* Open DOCA device according to the given PCI address */
	result = open_doca_device_with_pci(dev_pci_addr, NULL, &(sample_objects->hw_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device based on PCI address");
		return result;
	}
	sample_objects->data_path->hw_dev = sample_objects->hw_dev;

	/* Open DOCA device representor according to the given PCI address */
	result = open_doca_device_rep_with_pci(sample_objects->hw_dev, DOCA_DEVINFO_REP_FILTER_NET, dev_rep_pci_addr,
					       &(sample_objects->rep_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device representor based on PCI address");
		goto close_hw_dev;
	}

	/* Init CC server */
	result = init_cc_ctrl_path_server(server_name, sample_objects->hw_dev, sample_objects->rep_dev, &server_cb_cfg,
					  &(sample_objects->server), &(sample_objects->pe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Fail init cc server with error = %s", doca_error_get_name(result));
		goto close_rep_dev;
	}
	sample_objects->data_path->pe = sample_objects->pe;

	/* Wait start_data_path_test msg from client, so that server can get the connection information */
	while (sample_objects->connection == NULL) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}
	sample_objects->data_path->connection = sample_objects->connection;

	/* Exchange message with client to make connection is reliable */
	while (sample_objects->data_path_test_started == false) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}

	return DOCA_SUCCESS;

close_rep_dev:
	(void)doca_dev_rep_close(sample_objects->rep_dev);
close_hw_dev:
	(void)doca_dev_close(sample_objects->hw_dev);
	return result;
}

/**
 * Run cc_client sample
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @rep_pci_addr [in]: PCI address for the representor
 * @text [in]: Message to send to the server
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
start_cc_data_path_server_sample(const char *server_name, const char *dev_pci_addr, const char *rep_pci_addr,
				 const char *text)
{
	doca_error_t result;
	struct cc_data_path_server_objects sample_objects = {0};
	struct cc_data_path_objects data_path = {0};

	sample_objects.data_path = &data_path;

	result = init_cc_data_path_server_objects(server_name, dev_pci_addr, rep_pci_addr, text, &sample_objects);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize sample with error = %s", doca_error_get_name(result));
		return result;
	}

	result = cc_data_path_recv_msg(&data_path);
	if (result != DOCA_SUCCESS)
		goto exit;

	result = cc_data_path_send_msg(&data_path);
	if (result != DOCA_SUCCESS)
		goto exit;

exit:
	clean_cc_data_path_server_objects(&sample_objects);

	return sample_objects.server_result;
}
