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

#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include "comch_ctrl_path_common.h"
#include "comch_data_path_high_speed_common.h"
#include "common.h"

DOCA_LOG_REGISTER(COMCH_DATA_PATH_HIGH_SPEED_CLIENT);

/* Sample's objects */
struct comch_data_path_client_objects {
	struct doca_dev *hw_dev;		   /* Device used in the sample */
	struct doca_pe *pe;			   /* PE object used in the sample */
	struct doca_comch_client *client;	   /* Client object used in the sample */
	struct doca_comch_connection *connection;  /* CC connection object used in the sample */
	doca_error_t client_result;		   /* Holds result will be updated in client callbacks */
	bool client_finish;			   /* Controls whether client progress loop should be run */
	bool data_path_test_started;		   /* Indicate whether we can start data_path test */
	bool data_path_test_stopped;		   /* Indicate whether we can stop data_path test */
	struct comch_data_path_objects *data_path; /* Data path objects */
};

/**
 * Callback for client send task successful completion
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void client_send_task_completion_callback(struct doca_comch_task_send *task,
						 union doca_data task_user_data,
						 union doca_data ctx_user_data)
{
	struct comch_data_path_client_objects *sample_objects;

	(void)task_user_data;

	sample_objects = (struct comch_data_path_client_objects *)(ctx_user_data.ptr);
	sample_objects->client_result = DOCA_SUCCESS;
	DOCA_LOG_INFO("Client task sent successfully");
	doca_task_free(doca_comch_task_send_as_task(task));
}

/**
 * Callback for client send task completion with error
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void client_send_task_completion_err_callback(struct doca_comch_task_send *task,
						     union doca_data task_user_data,
						     union doca_data ctx_user_data)
{
	struct comch_data_path_client_objects *sample_objects;

	(void)task_user_data;

	sample_objects = (struct comch_data_path_client_objects *)(ctx_user_data.ptr);
	sample_objects->client_result = doca_task_get_status(doca_comch_task_send_as_task(task));
	DOCA_LOG_ERR("Message failed to send with error = %s", doca_error_get_name(sample_objects->client_result));
	doca_task_free(doca_comch_task_send_as_task(task));
	(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
}

/**
 * Callback for client message recv event
 *
 * @event [in]: Recv event object
 * @recv_buffer [in]: Message buffer
 * @msg_len [in]: Message len
 * @comch_connection [in]: Connection the message was received on
 */
static void client_message_recv_callback(struct doca_comch_event_msg_recv *event,
					 uint8_t *recv_buffer,
					 uint32_t msg_len,
					 struct doca_comch_connection *comch_connection)
{
	union doca_data user_data;
	struct doca_comch_client *comch_client;
	struct comch_data_path_client_objects *sample_objects;
	doca_error_t result;

	(void)event;

	DOCA_LOG_INFO("Message received: '%.*s'", (int)msg_len, recv_buffer);

	comch_client = doca_comch_client_get_client_ctx(comch_connection);

	result = doca_ctx_get_user_data(doca_comch_client_as_ctx(comch_client), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	sample_objects = (struct comch_data_path_client_objects *)(user_data.ptr);

	if ((msg_len == strlen(STR_START_DATA_PATH_TEST)) &&
	    (strncmp(STR_START_DATA_PATH_TEST, (char *)recv_buffer, msg_len) == 0))
		sample_objects->data_path_test_started = true;
	else if ((msg_len == strlen(STR_STOP_DATA_PATH_TEST)) &&
		 (strncmp(STR_STOP_DATA_PATH_TEST, (char *)recv_buffer, msg_len) == 0)) {
		sample_objects->data_path_test_stopped = true;
		sample_objects->data_path->remote_consumer_id = INVALID_CONSUMER_ID;
		(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
	}
}

/**
 * Client sends a message to server
 *
 * @sample_objects [in]: The sample object to use
 * @msg [in]: The msg to send
 * @len [in]: The msg length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t client_send_msg(struct comch_data_path_client_objects *sample_objects, const char *msg, size_t len)
{
	doca_error_t result;
	struct doca_comch_task_send *task;

	result = doca_comch_client_task_send_alloc_init(sample_objects->client,
							sample_objects->connection,
							(void *)msg,
							len,
							&task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate client task with error = %s", doca_error_get_name(result));
		return result;
	}

	result = doca_task_submit(doca_comch_task_send_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to send client task with error = %s", doca_error_get_name(result));
		doca_task_free(doca_comch_task_send_as_task(task));
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Callback triggered whenever CC client context state changes
 *
 * @user_data [in]: User data associated with the CC client context.
 * @ctx [in]: The CC client context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void client_state_changed_callback(const union doca_data user_data,
					  struct doca_ctx *ctx,
					  enum doca_ctx_states prev_state,
					  enum doca_ctx_states next_state)
{
	(void)ctx;
	(void)prev_state;

	struct comch_data_path_client_objects *sample_objects = (struct comch_data_path_client_objects *)user_data.ptr;

	switch (next_state) {
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("CC client context has been stopped");
		/* We can stop progressing the PE */
		sample_objects->client_finish = true;
		break;
	case DOCA_CTX_STATE_STARTING:
		/**
		 * The context is in starting state, need to progress until connection with server is established.
		 */
		DOCA_LOG_INFO("CC client context entered into starting state. Waiting for connection establishment");
		break;
	case DOCA_CTX_STATE_RUNNING:
		/* Get a connection channel */
		if (sample_objects->connection == NULL) {
			sample_objects->client_result =
				doca_comch_client_get_connection(sample_objects->client, &sample_objects->connection);
			if (sample_objects->client_result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to get connection from cc client with error = %s",
					     doca_error_get_name(sample_objects->client_result));
				(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
			}
			DOCA_LOG_INFO("CC client context is running. Get a connection from server");
		}
		break;
	case DOCA_CTX_STATE_STOPPING:
		/**
		 * The context is in stopping, this can happen when fatal error encountered or when stopping context.
		 * doca_pe_progress() will cause all tasks to be flushed, and finally transition state to idle
		 */
		DOCA_LOG_INFO("CC client context entered into stopping state. Waiting for connection termination");
		break;
	default:
		break;
	}
}

/**
 * Callback for new consumer arrival event
 *
 * @event [in]: New remote consumer event object
 * @comch_connection [in]: The connection related to the consumer
 * @id [in]: The ID of the new remote consumer
 */
static void new_consumer_callback(struct doca_comch_event_consumer *event,
				  struct doca_comch_connection *comch_connection,
				  uint32_t id)
{
	union doca_data user_data;
	struct doca_comch_client *comch_client;
	struct comch_data_path_client_objects *sample_objects;
	doca_error_t result;

	(void)event;

	comch_client = doca_comch_client_get_client_ctx(comch_connection);

	result = doca_ctx_get_user_data(doca_comch_client_as_ctx(comch_client), &user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get user data from ctx with error = %s", doca_error_get_name(result));
		return;
	}

	sample_objects = (struct comch_data_path_client_objects *)(user_data.ptr);
	sample_objects->data_path->remote_consumer_id = id;

	DOCA_LOG_INFO("Got a new remote consumer with ID = [%d]", id);
}

/**
 * Callback for expired consumer arrival event
 *
 * @event [in]: Expired remote consumer event object
 * @comch_connection [in]: The connection related to the consumer
 * @id [in]: The ID of the expired remote consumer
 */
void expired_consumer_callback(struct doca_comch_event_consumer *event,
			       struct doca_comch_connection *comch_connection,
			       uint32_t id)
{
	/* These arguments are not in use */
	(void)event;
	(void)comch_connection;
	(void)id;
}

/**
 * Clean all sample resources
 *
 * @sample_objects [in]: Sample objects struct to clean
 */
static void clean_comch_data_path_client_objects(struct comch_data_path_client_objects *sample_objects)
{
	doca_error_t result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	if (sample_objects == NULL)
		return;

	/* Verify client is not already stopped due to a server error */
	if (sample_objects->client_finish == false) {
		/* Exchange message with server to make connection is reliable */
		sample_objects->client_result =
			client_send_msg(sample_objects, STR_STOP_DATA_PATH_TEST, strlen(STR_STOP_DATA_PATH_TEST));
		if (sample_objects->client_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit send task with error = %s",
				     doca_error_get_name(sample_objects->client_result));
			(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
		}
		while (sample_objects->data_path_test_stopped == false) {
			if (doca_pe_progress(sample_objects->pe) == 0)
				nanosleep(&ts, &ts);
		}
		while (sample_objects->client_finish == false) {
			if (doca_pe_progress(sample_objects->pe) == 0)
				nanosleep(&ts, &ts);
		}
	}

	clean_comch_ctrl_path_client(sample_objects->client, sample_objects->pe);
	sample_objects->client = NULL;
	sample_objects->pe = NULL;

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
 * @text [in]: Message to send to the server
 * @sample_objects [in]: Sample objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t init_comch_data_path_client_objects(const char *server_name,
							const char *dev_pci_addr,
							const char *text,
							struct comch_data_path_client_objects *sample_objects)
{
	doca_error_t result;
	struct comch_ctrl_path_client_cb_config client_cb_cfg = {
		.send_task_comp_cb = client_send_task_completion_callback,
		.send_task_comp_err_cb = client_send_task_completion_err_callback,
		.msg_recv_cb = client_message_recv_callback,
		.data_path_mode = true,
		.new_consumer_cb = new_consumer_callback,
		.expired_consumer_cb = expired_consumer_callback,
		.ctx_user_data = sample_objects,
		.ctx_state_changed_cb = client_state_changed_callback};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	sample_objects->data_path->text = text;

	/* Open DOCA device according to the given PCI address */
	result = open_doca_device_with_pci(dev_pci_addr, NULL, &(sample_objects->hw_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open Comm Channel DOCA device based on PCI address");
		return result;
	}
	sample_objects->data_path->hw_dev = sample_objects->hw_dev;

	/* Init CC client */
	result = init_comch_ctrl_path_client(server_name,
					     sample_objects->hw_dev,
					     &client_cb_cfg,
					     &(sample_objects->client),
					     &(sample_objects->pe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init cc client with error = %s", doca_error_get_name(result));
		goto close_hw_dev;
	}
	sample_objects->data_path->pe = sample_objects->pe;

	/* Wait connection establishment */
	while (sample_objects->connection == NULL && sample_objects->client_finish == false) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}

	if (sample_objects->client_finish == true) {
		clean_comch_data_path_client_objects(sample_objects);
		return DOCA_ERROR_INITIALIZATION;
	}

	sample_objects->data_path->connection = sample_objects->connection;

	/* Exchange message with server, to make connection is reliable */
	sample_objects->client_result =
		client_send_msg(sample_objects, STR_START_DATA_PATH_TEST, strlen(STR_START_DATA_PATH_TEST));
	if (sample_objects->client_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit send task with error = %s",
			     doca_error_get_name(sample_objects->client_result));
		(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
		goto destroy_client;
	}
	while (sample_objects->data_path_test_started == false) {
		if (doca_pe_progress(sample_objects->pe) == 0)
			nanosleep(&ts, &ts);
	}

	return DOCA_SUCCESS;

destroy_client:
	clean_comch_ctrl_path_client(sample_objects->client, sample_objects->pe);
close_hw_dev:
	(void)doca_dev_close(sample_objects->hw_dev);
	return result;
}

/**
 * Run comch_data_path_client sample
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @text [in]: Message to send to the server
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t start_comch_data_path_client_sample(const char *server_name, const char *dev_pci_addr, const char *text)
{
	doca_error_t result;
	struct comch_data_path_client_objects sample_objects = {0};
	struct comch_data_path_objects data_path = {0};

	sample_objects.data_path = &data_path;

	result = init_comch_data_path_client_objects(server_name, dev_pci_addr, text, &sample_objects);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize sample with error = %s", doca_error_get_name(result));
		return result;
	}

	result = comch_data_path_send_msg(&data_path);
	if (result != DOCA_SUCCESS)
		goto exit;

	result = comch_data_path_recv_msg(&data_path);
	if (result != DOCA_SUCCESS)
		goto exit;

exit:
	clean_comch_data_path_client_objects(&sample_objects);

	return result != DOCA_SUCCESS ? result : sample_objects.client_result;
}
