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

DOCA_LOG_REGISTER(RDMA_SYNC_EVENT_REQUESTER::SAMPLE);

#define EXAMPLE_SET_VALUE (0xD0CA) /* Example value to use for setting sync event */

/*
 * DOCA device with rdma remote sync event tasks capability filter callback
 *
 * @devinfo [in]: doca_devinfo
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t sync_event_tasks_supported(const struct doca_devinfo *devinfo)
{
	doca_error_t status = DOCA_ERROR_UNKNOWN;

	status = doca_rdma_cap_task_remote_net_sync_event_notify_set_is_supported(devinfo);
	if (status != DOCA_SUCCESS)
		return status;

	return doca_rdma_cap_task_remote_net_sync_event_get_is_supported(devinfo);
}

/*
 * Write the connection details for the responder to read,
 * and read the connection details and the remote sync event details of the responder
 * In DC transport mode it is only needed to read the remote connection details
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: DOCA RDMA resources
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

		DOCA_LOG_INFO("You can now copy %s to the responder", cfg->local_connection_desc_path);
	}

	DOCA_LOG_INFO(
		"Please copy %s and %s from the responder and then press enter after pressing enter in the responder side",
		cfg->remote_connection_desc_path,
		cfg->remote_resource_desc_path);

	/* Wait for enter */
	wait_for_enter();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path,
			   (char **)&resources->remote_rdma_conn_descriptor,
			   &resources->remote_rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_error_get_descr(result));
		return result;
	}

	/* Read the remote sync event connection details */
	result = read_file(cfg->remote_resource_desc_path,
			   (char **)&resources->sync_event_descriptor,
			   &resources->sync_event_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the sync event export blob: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

/*
 * Free RDMA remote net sync event notify set task resources and task
 *
 * @se_set_task [in]: the task that should be freed along with it's resources
 * @ctx_user_data [in]: doca_data from the context (used to free relevant resources)
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_remote_net_sync_event_notify_set_free_task_resources(
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task,
	union doca_data ctx_user_data)
{
	doca_error_t result, return_value = DOCA_SUCCESS;
	struct doca_task *task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	char *successful_task_message = (char *)(doca_task_get_user_data(task).ptr);

	/* Release task resources and stop the ctx */
	result = doca_buf_dec_refcount(resources->src_buf, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease buffer's refcount: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(return_value, result);
	}

	free(successful_task_message);

	doca_task_free(task);

	return return_value;
}

/*
 * RDMA remote net sync event notify set task completed callback
 *
 * @se_set_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_remote_net_sync_event_notify_set_completed_callback(
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task,
	union doca_data task_user_data,
	union doca_data ctx_user_data)
{
	doca_error_t result;
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	char *successful_task_message = (char *)task_user_data.ptr;

	DOCA_LOG_INFO("RDMA remote net sync event notify set was done successfully");
	DOCA_LOG_INFO("%s", successful_task_message);

	/* Decrement number of remaining tasks */
	resources->num_remaining_tasks--;

	if (resources->num_remaining_tasks == 0) {
		/* Release task resources and stop the ctx */
		result = rdma_remote_net_sync_event_notify_set_free_task_resources(se_set_task, ctx_user_data);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to release task resources: %s", doca_error_get_descr(result));
			DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
		}

		if (resources->cfg->use_rdma_cm == true)
			(void)rdma_cm_disconnect(resources);

		/* Stop in callback context is expected to return DOCA_ERROR_IN_PROGRESS */
		result = doca_ctx_stop(resources->rdma_ctx);
		if (result != DOCA_ERROR_IN_PROGRESS) {
			DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(result));
			DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
		}
	}
}

/*
 * RDMA remote net sync event notify set task error callback
 *
 * @se_set_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_remote_net_sync_event_notify_set_error_callback(
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task,
	union doca_data task_user_data,
	union doca_data ctx_user_data)
{
	doca_error_t result;
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	struct doca_task *task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);

	(void)task_user_data.ptr;

	/* Decrement number of remaining tasks */
	resources->num_remaining_tasks--;

	/* Update that an error was encountered */
	result = doca_task_get_status(task);
	DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
	DOCA_LOG_ERR("RDMA remote net sync event notify set task failed: %s", doca_error_get_descr(result));

	/* Release task resources only if there are no remaining tasks */
	if (resources->num_remaining_tasks == 0) {
		result = rdma_remote_net_sync_event_notify_set_free_task_resources(se_set_task, ctx_user_data);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to release task resources: %s", doca_error_get_descr(result));
	}

	if (resources->cfg->use_rdma_cm == true)
		(void)rdma_cm_disconnect(resources);

	/* Stop in callback context is expected to return DOCA_ERROR_IN_PROGRESS */
	result = doca_ctx_stop(resources->rdma_ctx);
	if (result != DOCA_ERROR_IN_PROGRESS)
		DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(result));
}

/*
 * RDMA remote net sync event get task completed callback
 *
 * @se_get_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_remote_net_sync_event_get_completed_callback(
	struct doca_rdma_task_remote_net_sync_event_get *se_get_task,
	union doca_data task_user_data,
	union doca_data ctx_user_data)
{
	doca_error_t result = DOCA_SUCCESS;
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task =
		(struct doca_rdma_task_remote_net_sync_event_notify_set *)task_user_data.ptr;
	union doca_data set_task_user_data;
	struct doca_task *task, *set_task;
	struct doca_buf *get_buf;
	char *successful_task_message;
	void *buf_data;

	DOCA_LOG_INFO("RDMA remote net sync event get was done successfully");

	task = doca_rdma_task_remote_net_sync_event_get_as_task(se_get_task);

	get_buf = doca_rdma_task_remote_net_sync_event_get_get_dst_buf(se_get_task);

	result = doca_buf_get_data(get_buf, &buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for get task: %s", doca_error_get_descr(result));
		goto propagate_error;
	} else
		DOCA_LOG_INFO("Received sync event value 0x%X", *(int *)buf_data);

	if (*(int *)buf_data <= EXAMPLE_SET_VALUE) {
		/* Resubmit the task, as long as the data is different from expected */
		result = doca_buf_reset_data_len(get_buf);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to reset buffer's data length: %s", doca_error_get_descr(result));
			goto propagate_error;
		}

		result = doca_task_submit(task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit RDMA sync event get task: %s", doca_error_get_descr(result));
			goto propagate_error;
		}

		return;
	}

	/* Submit RDMA sync event set task to notify that the sync event is complete */
	set_task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);
	set_task_user_data = doca_task_get_user_data(set_task);

	result = doca_buf_get_data(resources->src_buf, &buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for set task: %s", doca_error_get_descr(result));
		goto propagate_error;
	}
	*(uint64_t *)buf_data = UINT64_MAX;

	DOCA_LOG_INFO("Notifying remote sync event for completion");
	successful_task_message = (char *)set_task_user_data.ptr;
	memset(successful_task_message, 0, MAX_ARG_SIZE);
	strncpy(successful_task_message,
		"Remote sync event has been notified for completion successfully",
		MAX_ARG_SIZE - 1);

	result = doca_task_submit(set_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA remote sync event set task: %s", doca_error_get_descr(result));
		goto propagate_error;
	}
	resources->num_remaining_tasks++;

propagate_error:
	DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);

	resources->num_remaining_tasks--;

	/* Release task resources and stop the ctx */
	result = doca_buf_dec_refcount(get_buf, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease buffer's refcount: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
	}

	doca_task_free(task);

	/* if num_remaining_tasks is 0, the set task wasn't successfully submitted */
	if (resources->num_remaining_tasks == 0) {
		result = rdma_remote_net_sync_event_notify_set_free_task_resources(se_set_task, ctx_user_data);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to free remote_net_sync_event_notify_set task's resources: %s",
				     doca_error_get_descr(result));

		if (resources->cfg->use_rdma_cm == true)
			(void)rdma_cm_disconnect(resources);

		/* Stop in callback context is expected to return DOCA_ERROR_IN_PROGRESS */
		result = doca_ctx_stop(resources->rdma_ctx);
		if (result != DOCA_ERROR_IN_PROGRESS)
			DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(result));
	}
}

/*
 * RDMA remote net sync event get task error callback
 *
 * @se_get_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void rdma_remote_net_sync_event_get_error_callback(struct doca_rdma_task_remote_net_sync_event_get *se_get_task,
							  union doca_data task_user_data,
							  union doca_data ctx_user_data)
{
	doca_error_t result;
	struct rdma_resources *resources = (struct rdma_resources *)ctx_user_data.ptr;
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task =
		(struct doca_rdma_task_remote_net_sync_event_notify_set *)task_user_data.ptr;
	struct doca_task *task = doca_rdma_task_remote_net_sync_event_get_as_task(se_get_task);

	/* Decrement number of remaining tasks */
	resources->num_remaining_tasks--;

	/* Update that an error was encountered */
	result = doca_task_get_status(task);
	DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
	DOCA_LOG_ERR("RDMA remote net sync event get task failed: %s", doca_error_get_descr(result));

	/* Release task resources and stop the ctx */
	result = rdma_remote_net_sync_event_notify_set_free_task_resources(se_set_task, ctx_user_data);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to free remote_net_sync_event_notify_set task's resources: %s",
			     doca_error_get_descr(result));

	result = doca_buf_dec_refcount(resources->dst_buf, NULL);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease buffer's refcount: %s", doca_error_get_descr(result));

	doca_task_free(task);

	if (resources->cfg->use_rdma_cm == true)
		(void)rdma_cm_disconnect(resources);

	/* Stop in callback context is expected to return DOCA_ERROR_IN_PROGRESS */
	result = doca_ctx_stop(resources->rdma_ctx);
	if (result != DOCA_ERROR_IN_PROGRESS)
		DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(result));
}

/*
 * Prepare and submit RDMA sync event tasks
 *
 * @resources [in]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_sync_event_requestor_prepare_and_submit_tasks(struct rdma_resources *resources)
{
	doca_error_t result, tmp_result;
	struct doca_task *task = NULL;
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task;
	struct doca_rdma_task_remote_net_sync_event_get *se_get_task;
	union doca_data task_user_data = {0};
	void *set_buf_data;
	void *get_buf_data;
	char *successful_task_message;

	if (resources->cfg->use_rdma_cm == true) {
		/* Create remote net sync event */
		result = doca_sync_event_remote_net_create_from_export(resources->doca_device,
								       resources->sync_event_descriptor,
								       resources->sync_event_descriptor_size,
								       &(resources->remote_se));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create remote sync event from export: %s",
				     doca_error_get_descr(result));
			return result;
		}
	}

	successful_task_message = calloc(1, MAX_ARG_SIZE);
	if (successful_task_message == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for string");
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Retrieve buffers from DOCA buffer inventory */
	result = doca_buf_inventory_buf_get_by_data(resources->buf_inventory,
						    resources->mmap,
						    resources->mmap_memrange,
						    sizeof(uint64_t),
						    &resources->src_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s",
			     doca_error_get_descr(result));
		goto free_successful_task_message;
	}

	result = doca_buf_get_data(resources->src_buf, &set_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for set task: %s", doca_error_get_descr(result));
		goto destroy_set_buf;
	}
	*(uint64_t *)set_buf_data = EXAMPLE_SET_VALUE;

	/* Include a message for successful sync_event_notify_set tasks, to be used in the callbacks */
	task_user_data.ptr = (void *)successful_task_message;

	/* Allocate and construct RDMA sync event set task */
	result = doca_rdma_task_remote_net_sync_event_notify_set_allocate_init(resources->rdma,
									       resources->connections[0],
									       resources->remote_se,
									       resources->src_buf,
									       task_user_data,
									       &se_set_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA sync event set task: %s", doca_error_get_descr(result));
		goto destroy_set_buf;
	}

	task_user_data.ptr = (void *)se_set_task;

	/* Submit RDMA sync event set task */
	task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);

	DOCA_LOG_INFO("Signaling remote sync event");
	strncpy(successful_task_message,
		"Remote sync event has been signaled successfully, now waiting for remote sync event to be signaled",
		MAX_ARG_SIZE - 1);
	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA remote sync event set task: %s", doca_error_get_descr(result));
		goto free_set_task;
	}
	resources->num_remaining_tasks++;

	result = doca_buf_inventory_buf_get_by_addr(resources->buf_inventory,
						    resources->mmap,
						    resources->mmap_memrange,
						    sizeof(uint64_t),
						    &resources->dst_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s",
			     doca_error_get_descr(result));
		/* Set task was submitted, resources will be freed in error completion callback */
		return result;
	}

	result = doca_buf_get_data(resources->dst_buf, &get_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for get task: %s", doca_error_get_descr(result));
		goto destroy_get_buf;
	}

	/* Allocate and construct RDMA sync event get task */
	result = doca_rdma_task_remote_net_sync_event_get_allocate_init(resources->rdma,
									resources->connections[0],
									resources->remote_se,
									resources->dst_buf,
									task_user_data,
									&se_get_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA sync event get task: %s", doca_error_get_descr(result));
		goto destroy_get_buf;
	}

	/* Submit RDMA sync event get task */
	task = doca_rdma_task_remote_net_sync_event_get_as_task(se_get_task);

	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA remote sync event get task: %s", doca_error_get_descr(result));
		doca_task_free(task);
		goto destroy_get_buf;
	}
	resources->num_remaining_tasks++;

	return DOCA_SUCCESS;

destroy_get_buf:
	tmp_result = doca_buf_dec_refcount(resources->dst_buf, NULL);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease buffer's refcount: %s", doca_error_get_descr(tmp_result));

	/* After set task is submitted, resources will be freed in error completion callback */
	return result;

free_set_task:
	doca_task_free(doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task));

destroy_set_buf:
	tmp_result = doca_buf_dec_refcount(resources->src_buf, NULL);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease buffer's refcount: %s", doca_error_get_descr(tmp_result));

free_successful_task_message:
	free(successful_task_message);

	return result;
}

/*
 * Export/connect RDMA and connection and sync event details, connect to the remote RDMA and create remote sync event
 *
 * @resources [in]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_sync_event_requestor_export_and_connect(struct rdma_resources *resources)
{
	doca_error_t result;

	if (resources->cfg->use_rdma_cm == true)
		return rdma_cm_connect(resources);

	/* Export DOCA RDMA */
	result = doca_rdma_export(resources->rdma,
				  &(resources->rdma_conn_descriptor),
				  &(resources->rdma_conn_descriptor_size),
				  &(resources->connections[0]));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export DOCA RDMA: %s", doca_error_get_descr(result));
		return result;
	}

	/* Write and read connection details from the responder */
	result = write_read_connection(resources->cfg, resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write and read connection details from the responder: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Connect RDMA */
	result = doca_rdma_connect(resources->rdma,
				   resources->remote_rdma_conn_descriptor,
				   resources->remote_rdma_conn_descriptor_size,
				   resources->connections[0]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect the requester's DOCA RDMA to the responder's DOCA RDMA: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Create remote net sync event */
	result = doca_sync_event_remote_net_create_from_export(resources->doca_device,
							       resources->sync_event_descriptor,
							       resources->sync_event_descriptor_size,
							       &(resources->remote_se));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create remote sync event from export: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * destroy remote sync event if exists
 *
 * @resources [in]: RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_sync_event_requestor_destroy_remote_sync_event(struct rdma_resources *resources)
{
	doca_error_t result;

	/* Destroy remote sync event if exists */
	if (resources->remote_se != NULL) {
		result = doca_sync_event_remote_net_destroy(resources->remote_se);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy DOCA remote sync event: %s", doca_error_get_descr(result));
			return result;
		}

		resources->remote_se = NULL;
	}

	return DOCA_SUCCESS;
}

/*
 * RDMA sync event requestor state change callback
 * This function represents the state machine for this RDMA program
 *
 * @user_data [in]: doca_data from the context
 * @ctx [in]: DOCA context
 * @prev_state [in]: Previous DOCA context state
 * @next_state [in]: Next DOCA context state
 */
static void rdma_sync_event_requestor_state_change_callback(const union doca_data user_data,
							    struct doca_ctx *ctx,
							    enum doca_ctx_states prev_state,
							    enum doca_ctx_states next_state)
{
	struct rdma_resources *resources = (struct rdma_resources *)user_data.ptr;
	doca_error_t result = DOCA_SUCCESS;
	(void)prev_state;
	(void)ctx;

	switch (next_state) {
	case DOCA_CTX_STATE_STARTING:
		DOCA_LOG_INFO("RDMA context entered starting state");
		break;
	case DOCA_CTX_STATE_RUNNING:
		DOCA_LOG_INFO("RDMA context is running");

		result = rdma_sync_event_requestor_export_and_connect(resources);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("rdma_sync_event_requestor_export_and_connect() failed: %s",
				     doca_error_get_descr(result));
			break;
		} else
			DOCA_LOG_INFO("RDMA context finished initialization");

		if (resources->cfg->use_rdma_cm == true)
			break;

		result = rdma_sync_event_requestor_prepare_and_submit_tasks(resources);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("rdma_sync_event_requestor_prepare_and_submit_tasks() failed: %s",
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

		result = rdma_sync_event_requestor_destroy_remote_sync_event(resources);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy remote sync event: %s", doca_error_get_descr(result));
			DOCA_ERROR_PROPAGATE(resources->first_encountered_error, result);
		}

		/* We can stop the progressing the PE */
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
 * Requester side of the RDMA sync event
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdma_sync_event_requester(struct rdma_config *cfg)
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
	result =
		allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions, sync_event_tasks_supported, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_rdma_task_remote_net_sync_event_notify_set_set_conf(
		resources.rdma,
		rdma_remote_net_sync_event_notify_set_completed_callback,
		rdma_remote_net_sync_event_notify_set_error_callback,
		NUM_RDMA_TASKS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set configurations for RDMA sync event set task: %s",
			     doca_error_get_descr(result));
		goto destroy_resources;
	}

	result = doca_rdma_task_remote_net_sync_event_get_set_conf(resources.rdma,
								   rdma_remote_net_sync_event_get_completed_callback,
								   rdma_remote_net_sync_event_get_error_callback,
								   NUM_RDMA_TASKS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set configurations for RDMA sync event get task: %s",
			     doca_error_get_descr(result));
		goto destroy_resources;
	}

	result = doca_ctx_set_state_changed_cb(resources.rdma_ctx, rdma_sync_event_requestor_state_change_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set state change callback for RDMA context: %s", doca_error_get_descr(result));
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

	/* Include the program's resources in user data of context to be used in callbacks */
	ctx_user_data.ptr = &(resources);
	result = doca_ctx_set_user_data(resources.rdma_ctx, ctx_user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set context user data: %s", doca_error_get_descr(result));
		goto stop_buf_inventory;
	}

	if (cfg->use_rdma_cm == true) {
		resources.recv_sync_event_desc = true;
		resources.is_requester = true;
		resources.require_remote_mmap = true;
		resources.task_fn = rdma_sync_event_requestor_prepare_and_submit_tasks;
		result = config_rdma_cm_callback_and_negotiation_task(&resources,
								      /* need_send_mmap_info */ false,
								      /* need_recv_mmap_info */ true);
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
	 * rdma_sync_event_requestor_state_change_callback().
	 * When signaled, stop running the progress engine and continue.
	 */
	while (resources.run_pe_progress) {
		if (doca_pe_progress(resources.pe) == 0)
			nanosleep(&ts, &ts);
	}

	/* Check the first_encountered_error we update in the callbacks */
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
