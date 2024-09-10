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

#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_pe.h>

#include "comch_ctrl_path_common.h"
#include "common.h"

DOCA_LOG_REGISTER(COMCH_CTRL_PATH_CLIENT);

/* Sample's objects */
struct comch_ctrl_path_objects {
	struct doca_dev *hw_dev;	  /* Device used in the sample */
	struct doca_pe *pe;		  /* PE object used in the sample */
	struct doca_comch_client *client; /* Client object used in the sample */
	const char *text;		  /* Message to send to the server */
	uint32_t text_len;		  /* Length of message to send to the server */
	doca_error_t result;		  /* Holds result will be updated in callbacks */
	bool finish;			  /* Controls whether progress loop should be run */
};

/**
 * Callback for send task successful completion
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void send_task_completion_callback(struct doca_comch_task_send *task,
					  union doca_data task_user_data,
					  union doca_data ctx_user_data)
{
	struct comch_ctrl_path_objects *sample_objects = (struct comch_ctrl_path_objects *)ctx_user_data.ptr;

	/* This argument is not in use */
	(void)task_user_data;

	sample_objects->result = DOCA_SUCCESS;
	DOCA_LOG_INFO("Task sent successfully");

	doca_task_free(doca_comch_task_send_as_task(task));
}

/**
 * Callback for send task completion with error
 *
 * @task [in]: Send task object
 * @task_user_data [in]: User data for task
 * @ctx_user_data [in]: User data for context
 */
static void send_task_completion_err_callback(struct doca_comch_task_send *task,
					      union doca_data task_user_data,
					      union doca_data ctx_user_data)
{
	struct comch_ctrl_path_objects *sample_objects = (struct comch_ctrl_path_objects *)ctx_user_data.ptr;

	/* This argument is not in use */
	(void)task_user_data;

	sample_objects->result = doca_task_get_status(doca_comch_task_send_as_task(task));
	DOCA_LOG_ERR("Message failed to send with error = %s", doca_error_get_name(sample_objects->result));

	doca_task_free(doca_comch_task_send_as_task(task));
	(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
}

/**
 * Callback for message recv event
 *
 * @event [in]: Recv event object
 * @recv_buffer [in]: Message buffer
 * @msg_len [in]: Message len
 * @comch_connection [in]: Connection the message was received on
 */
static void message_recv_callback(struct doca_comch_event_msg_recv *event,
				  uint8_t *recv_buffer,
				  uint32_t msg_len,
				  struct doca_comch_connection *comch_connection)
{
	union doca_data user_data = doca_comch_connection_get_user_data(comch_connection);
	struct comch_ctrl_path_objects *sample_objects = (struct comch_ctrl_path_objects *)user_data.ptr;

	/* This argument is not in use */
	(void)event;

	DOCA_LOG_INFO("Message received: '%.*s'", (int)msg_len, recv_buffer);

	(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
}

/**
 * Send and receive message on client
 *
 * @sample_objects [in]: Sample objects struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t client_send_ping_pong(struct comch_ctrl_path_objects *sample_objects)
{
	struct doca_comch_task_send *task;
	struct doca_task *task_obj;
	struct doca_comch_connection *connection;
	doca_error_t result;
	union doca_data user_data;
	const char *text = sample_objects->text;
	size_t msg_len = sample_objects->text_len;

	result = doca_comch_client_get_connection(sample_objects->client, &connection);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get connection from client with error = %s", doca_error_get_name(result));
		return result;
	}

	user_data.ptr = (void *)sample_objects;
	result = doca_comch_connection_set_user_data(connection, user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set user_data for connection with error = %s", doca_error_get_name(result));
		return result;
	}

	result = doca_comch_client_task_send_alloc_init(sample_objects->client, connection, text, msg_len, &task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate task in client with error = %s", doca_error_get_name(result));
		return result;
	}

	task_obj = doca_comch_task_send_as_task(task);

	result = doca_task_submit(task_obj);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed submitting send task with error = %s", doca_error_get_name(result));
		doca_task_free(task_obj);
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Clean all sample resources
 *
 * @sample_objects [in]: Sample objects struct to clean
 */
static void clean_comch_sample_objects(struct comch_ctrl_path_objects *sample_objects)
{
	doca_error_t result;

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
 * Callback triggered whenever CC client context state changes
 *
 * @user_data [in]: User data associated with the CC client context. Will hold struct comch_ctrl_path_objects *
 * @ctx [in]: The CC client context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void comch_client_state_changed_callback(const union doca_data user_data,
						struct doca_ctx *ctx,
						enum doca_ctx_states prev_state,
						enum doca_ctx_states next_state)
{
	(void)ctx;
	(void)prev_state;

	struct comch_ctrl_path_objects *sample_objects = (struct comch_ctrl_path_objects *)user_data.ptr;

	switch (next_state) {
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("CC client context has been stopped");
		/* We can stop progressing the PE */
		sample_objects->finish = true;
		break;
	case DOCA_CTX_STATE_STARTING:
		/**
		 * The context is in starting state, need to progress until connection with server is established.
		 */
		DOCA_LOG_INFO("CC client context entered into starting state. Waiting for connection establishment");
		break;
	case DOCA_CTX_STATE_RUNNING:
		DOCA_LOG_INFO("CC client context is running. Sending message");
		sample_objects->result = client_send_ping_pong(sample_objects);
		if (sample_objects->result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit send task with error = %s",
				     doca_error_get_name(sample_objects->result));
			(void)doca_ctx_stop(doca_comch_client_as_ctx(sample_objects->client));
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
 * Initialize sample resources
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @sample_objects [in]: Sample objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t init_comch_ctrl_path_objects(const char *server_name,
						 const char *dev_pci_addr,
						 struct comch_ctrl_path_objects *sample_objects)
{
	doca_error_t result;
	struct comch_ctrl_path_client_cb_config cfg = {.send_task_comp_cb = send_task_completion_callback,
						       .send_task_comp_err_cb = send_task_completion_err_callback,
						       .msg_recv_cb = message_recv_callback,
						       .data_path_mode = false,
						       .new_consumer_cb = NULL,
						       .expired_consumer_cb = NULL,
						       .ctx_user_data = sample_objects,
						       .ctx_state_changed_cb = comch_client_state_changed_callback};

	/* Open DOCA device according to the given PCI address */
	result = open_doca_device_with_pci(dev_pci_addr, NULL, &(sample_objects->hw_dev));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open Comm Channel DOCA device based on PCI address");
		return result;
	}

	result = init_comch_ctrl_path_client(server_name,
					     sample_objects->hw_dev,
					     &cfg,
					     &(sample_objects->client),
					     &(sample_objects->pe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init cc client with error = %s", doca_error_get_name(result));
		clean_comch_sample_objects(sample_objects);
		return result;
	}

	return DOCA_SUCCESS;
}

/**
 * Run comch_ctrl_path_client sample
 *
 * @server_name [in]: Server name to connect to
 * @dev_pci_addr [in]: PCI address to connect over
 * @text [in]: Message to send to the server
 * @text_len [in]: Message length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t start_comch_ctrl_path_client_sample(const char *server_name,
						 const char *dev_pci_addr,
						 const char *text,
						 const uint32_t text_len)
{
	doca_error_t result;
	struct comch_ctrl_path_objects sample_objects = {0};
	uint32_t max_msg_size;

	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	result = init_comch_ctrl_path_objects(server_name, dev_pci_addr, &sample_objects);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize sample with error = %s", doca_error_get_name(result));
		clean_comch_sample_objects(&sample_objects);
		return result;
	}

	result = doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(sample_objects.hw_dev), &max_msg_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get max supported message size with error = %s", doca_error_get_name(result));
		clean_comch_sample_objects(&sample_objects);
		return result;
	}

	if (text_len > max_msg_size) {
		DOCA_LOG_ERR(
			"Failed to run sample, text size is larger than supported message size. text_len = %u, supported message size = %u",
			text_len,
			max_msg_size);
		return DOCA_ERROR_INVALID_VALUE;
	}

	sample_objects.text = text;
	sample_objects.text_len = text_len;
	sample_objects.finish = false;

	while (!sample_objects.finish) {
		if (doca_pe_progress(sample_objects.pe) == 0)
			nanosleep(&ts, &ts);
	}

	clean_comch_sample_objects(&sample_objects);
	return sample_objects.result;
}
