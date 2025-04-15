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

#include <doca_error.h>
#include <doca_log.h>
#include <doca_buf_inventory.h>
#include <doca_buf.h>
#include <doca_ctx.h>

#include "rdma_common.h"

#define MAX_BUFF_SIZE (256)		 /* Maximum DOCA buffer size */
#define EXAMPLE_IMMEDIATE_VALUE (0xABCD) /* Example immediate value to send */

DOCA_LOG_REGISTER(RDMA_SEND_IMMEDIATE::SAMPLE);

/*
 * Write the connection details for the receiver to read, and read the connection details of the receiver
 * In DC transport mode it is only needed to read the remote connection details
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t write_read_connection(struct rdma_config *cfg, struct rdma_resources *resources)
{
	doca_error_t result = DOCA_SUCCESS;

	if (cfg->transport_type == DOCA_RDMA_TRANSPORT_TYPE_RC) {
		/* Write the RDMA connection details */
		result = write_file(cfg->local_connection_desc_path,
				    (char *)resources->rdma_conn_descriptor,
				    resources->rdma_conn_descriptor_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to write the RDMA connection details: %s", doca_error_get_descr(result));
			return result;
		}

		DOCA_LOG_INFO("You can now copy %s to the receiver", cfg->local_connection_desc_path);
	}

	DOCA_LOG_INFO("Please copy %s from the receiver and then press enter after pressing enter in the receiver side",
		      cfg->remote_connection_desc_path);

	/* Wait for enter */
	wait_for_enter();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path,
			   (char **)&resources->remote_rdma_conn_descriptor,
			   &resources->remote_rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_error_get_descr(result));

	return result;
}

/*
 * RDMA send with immediate task completed callback
 *
 * @rdma_send_imm_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_send_imm_completed_callback(struct doca_rdma_task_send_imm *rdma_send_imm_task,
					     union doca_data task_user_data,
					     union doca_data ctx_user_data)
{
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	doca_error_t *first_encountered_error = (doca_error_t *)task_user_data.ptr;
	doca_error_t result = DOCA_SUCCESS, tmp_result;

	DOCA_LOG_INFO("RDMA send with immediate task was done successfully");

	doca_task_free(doca_rdma_task_send_imm_as_task(rdma_send_imm_task));
	tmp_result = doca_buf_dec_refcount(resources->src_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease src_buf count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Update that an error was encountered, if any */
	DOCA_ERROR_PROPAGATE(*first_encountered_error, tmp_result);

	resources->num_remaining_tasks--;
	/* Stop context once all tasks are completed */
	if (resources->num_remaining_tasks == 0) {
		if (resources->cfg->use_rdma_cm == true)
			(void)rdma_cm_disconnect(resources);
		(void)doca_ctx_stop(resources->rdma_ctx);
	}
}

/*
 * RDMA send with immediate task error callback
 *
 * @rdma_send_imm_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_send_imm_error_callback(struct doca_rdma_task_send_imm *rdma_send_imm_task,
					 union doca_data task_user_data,
					 union doca_data ctx_user_data)
{
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	struct doca_task *task = doca_rdma_task_send_imm_as_task(rdma_send_imm_task);
	doca_error_t *first_encountered_error = (doca_error_t *)task_user_data.ptr;
	doca_error_t result;

	/* Update that an error was encountered */
	result = doca_task_get_status(task);
	DOCA_ERROR_PROPAGATE(*first_encountered_error, result);
	DOCA_LOG_ERR("RDMA send with immediate task failed: %s", doca_error_get_descr(result));

	doca_task_free(task);
	result = doca_buf_dec_refcount(resources->src_buf, NULL);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease src_buf count: %s", doca_error_get_descr(result));

	resources->num_remaining_tasks--;
	/* Stop context once all tasks are completed */
	if (resources->num_remaining_tasks == 0) {
		if (resources->cfg->use_rdma_cm == true)
			(void)rdma_cm_disconnect(resources);
		(void)doca_ctx_stop(resources->rdma_ctx);
	}
}

/*
 * Export and receive connection details, and connect to the remote RDMA
 *
 * @resources [in]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_send_immediate_export_and_connect(struct rdma_resources *resources)
{
	doca_error_t result;

	if (resources->cfg->use_rdma_cm == true)
		return rdma_cm_connect(resources);

	/* Export RDMA connection details */
	result = doca_rdma_export(resources->rdma,
				  &(resources->rdma_conn_descriptor),
				  &(resources->rdma_conn_descriptor_size),
				  &(resources->connections[0]));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export RDMA: %s", doca_error_get_descr(result));
		return result;
	}

	/* Write and read connection details to the receiver */
	result = write_read_connection(resources->cfg, resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write and read connection details from receiver: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Connect RDMA */
	result = doca_rdma_connect(resources->rdma,
				   resources->remote_rdma_conn_descriptor,
				   resources->remote_rdma_conn_descriptor_size,
				   resources->connections[0]);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to connect the sender's RDMA to the receiver's RDMA: %s",
			     doca_error_get_descr(result));

	return result;
}

/*
 * Prepare and submit RDMA send immediate task
 *
 * @resources [in]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_send_immediate_prepare_and_submit_task(struct rdma_resources *resources)
{
	struct doca_rdma_task_send_imm *rdma_send_imm_task = NULL;
	union doca_data task_user_data = {0};
	void *src_buf_data;
	doca_error_t result, tmp_result;

	if (resources->cfg->use_rdma_cm == true) {
		DOCA_LOG_INFO(
			"Please press enter after the receive task has been successfully submitted in the receiver side");

		/* Wait for enter */
		wait_for_enter();
	}

	/* Add src buffer to DOCA buffer inventory */
	result = doca_buf_inventory_buf_get_by_data(resources->buf_inventory,
						    resources->mmap,
						    resources->mmap_memrange,
						    MAX_BUFF_SIZE,
						    &resources->src_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Set data of src buffer */
	result = doca_buf_get_data(resources->src_buf, &src_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get source buffer data: %s", doca_error_get_descr(result));
		goto destroy_src_buf;
	}
	strncpy(src_buf_data, resources->cfg->send_string, MAX_BUFF_SIZE + 1);

	/* Include first_encountered_error in user data of task to be used in the callbacks */
	task_user_data.ptr = &(resources->first_encountered_error);
	/* Allocate and construct RDMA send with immediate task */
	result = doca_rdma_task_send_imm_allocate_init(resources->rdma,
						       resources->connections[0],
						       resources->src_buf,
						       EXAMPLE_IMMEDIATE_VALUE,
						       task_user_data,
						       &rdma_send_imm_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA send with immediate task: %s", doca_error_get_descr(result));
		goto destroy_src_buf;
	}

	/* Submit RDMA send with immediate task */
	DOCA_LOG_INFO("Submitting RDMA send with immediate task that sends \"%s\" with immediate value %u to receiver",
		      resources->cfg->send_string,
		      EXAMPLE_IMMEDIATE_VALUE);
	resources->num_remaining_tasks++;
	result = doca_task_submit(doca_rdma_task_send_imm_as_task(rdma_send_imm_task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA send with immediate task: %s", doca_error_get_descr(result));
		goto free_task;
	}

	return result;

free_task:
	doca_task_free(doca_rdma_task_send_imm_as_task(rdma_send_imm_task));
destroy_src_buf:
	tmp_result = doca_buf_dec_refcount(resources->src_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease src_buf count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * RDMA send with immediate state change callback
 * This function represents the state machine for this RDMA program
 *
 * @user_data [in]: doca_data from the context
 * @ctx [in]: DOCA context
 * @prev_state [in]: Previous DOCA context state
 * @next_state [in]: Next DOCA context state
 */
static void rdma_send_immediate_state_change_callback(const union doca_data user_data,
						      struct doca_ctx *ctx,
						      enum doca_ctx_states prev_state,
						      enum doca_ctx_states next_state)
{
	struct rdma_resources *resources = (struct rdma_resources *)user_data.ptr;
	struct rdma_config *cfg = resources->cfg;
	doca_error_t result = DOCA_SUCCESS;
	(void)prev_state;
	(void)ctx;

	switch (next_state) {
	case DOCA_CTX_STATE_STARTING:
		DOCA_LOG_INFO("RDMA context entered starting state");
		break;
	case DOCA_CTX_STATE_RUNNING:
		DOCA_LOG_INFO("RDMA context is running");

		result = rdma_send_immediate_export_and_connect(resources);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("rdma_send_immediate_export_and_connect() failed: %s",
				     doca_error_get_descr(result));
			break;
		} else
			DOCA_LOG_INFO("RDMA context finished initialization");

		if (cfg->use_rdma_cm == true)
			break;

		result = rdma_send_immediate_prepare_and_submit_task(resources);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("rdma_send_immediate_prepare_and_submit_task() failed: %s",
				     doca_error_get_descr(result));
		break;
	case DOCA_CTX_STATE_STOPPING:
		/**
		 * doca_ctx_stop() has been called.
		 * In this sample, this happens either due to a failure encountered, in which case doca_pe_progress()
		 * will cause any inflight task to be flushed, or due to the successful compilation of the sample flow.
		 * In both cases, in this sample, doca_pe_progress() will eventually transition the context to idle
		 * state.
		 */
		DOCA_LOG_INFO("RDMA context entered into stopping state. Any inflight tasks will be flushed");
		break;
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("RDMA context has been stopped");

		/* We can stop progressing the PE */
		resources->run_pe_progress = false;
		break;
	default:
		break;
	}

	/* If something failed - update that an error was encountered and stop the ctx */
	if (result != DOCA_SUCCESS) {
		DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
		(void)doca_ctx_stop(ctx);
	}
}

/*
 * Send a message to the receiver with immediate
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdma_send_immediate(struct rdma_config *cfg)
{
	struct rdma_resources resources = {0};
	union doca_data ctx_user_data = {0};
	const uint32_t mmap_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
	const uint32_t rdma_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result, tmp_result;

	/* Allocating resources */
	result = allocate_rdma_resources(cfg,
					 mmap_permissions,
					 rdma_permissions,
					 doca_rdma_cap_task_send_imm_is_supported,
					 &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_rdma_task_send_imm_set_conf(resources.rdma,
						  rdma_send_imm_completed_callback,
						  rdma_send_imm_error_callback,
						  NUM_RDMA_TASKS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set configurations for RDMA send with immediate task: %s",
			     doca_error_get_descr(result));
		goto destroy_resources;
	}

	result = doca_ctx_set_state_changed_cb(resources.rdma_ctx, rdma_send_immediate_state_change_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set state change callback for RDMA context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/* Include the program's resources in user data of context to be used in callbacks */
	ctx_user_data.ptr = &(resources);
	result = doca_ctx_set_user_data(resources.rdma_ctx, ctx_user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set context user data: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/* Create DOCA buffer inventory */
	result = doca_buf_inventory_create(INVENTORY_NUM_INITIAL_ELEMENTS, &resources.buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/* Start DOCA buffer inventory */
	result = doca_buf_inventory_start(resources.buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto destroy_buf_inventory;
	}

	if (cfg->use_rdma_cm == true) {
		/* Set rdma cm connection configuration callbacks */
		resources.require_remote_mmap = false;
		resources.task_fn = rdma_send_immediate_prepare_and_submit_task;
		result = config_rdma_cm_callback_and_negotiation_task(&resources,
								      /* need_send_mmap_info */ false,
								      /* need_recv_mmap_info */ false);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to config RDMA CM callbacks and negotiation functions: %s",
				     doca_error_get_descr(result));
			goto destroy_buf_inventory;
		}
	}

	/* Start RDMA context */
	result = doca_ctx_start(resources.rdma_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start RDMA context: %s", doca_error_get_descr(result));
		goto stop_buf_inventory;
	}

	/*
	 * Run the progress engine which will run the state machine defined in
	 * rdma_send_immediate_state_change_callback() When the context moves to idle, the context change callback call
	 * will signal to stop running the progress engine.
	 */
	while (resources.run_pe_progress) {
		if (doca_pe_progress(resources.pe) == 0)
			nanosleep(&ts, &ts);
	}

	/* Assign the result we update in the callbacks */
	result = resources.first_encountered_error;

stop_buf_inventory:
	tmp_result = doca_buf_inventory_stop(resources.buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA buffer inventory: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_buf_inventory:
	tmp_result = doca_buf_inventory_destroy(resources.buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA buffer inventory: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_resources:
	tmp_result = destroy_rdma_resources(&resources, cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
