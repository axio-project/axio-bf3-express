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

#include <doca_error.h>
#include <doca_log.h>
#include <doca_buf_inventory.h>
#include <doca_buf.h>

#include "rdma_common.h"

DOCA_LOG_REGISTER(RDMA_SYNC_EVENT_REQUESTER::SAMPLE);

#define MAX_BUFF_SIZE	  (256)     /* Maximum DOCA buffer size */
#define EXAMPLE_SET_VALUE (0xD0CA)  /* Example value to use for setting sync event */

/*
 * DOCA device with rdma remote sync event tasks capability filter callback
 *
 * @devinfo [in]: doca_devinfo
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
sync_event_tasks_supported(const struct doca_devinfo *devinfo)
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
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: DOCA RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
write_read_connection(struct rdma_config *cfg, struct rdma_resources *resources)
{
	int enter = 0;
	doca_error_t result = DOCA_SUCCESS;

	/* Write the RDMA connection details */
	result = write_file(cfg->local_connection_desc_path, (char *)resources->rdma_conn_descriptor,
				resources->rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write the RDMA connection details: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("You can now copy %s to the responder", cfg->local_connection_desc_path);
	DOCA_LOG_INFO("Please copy %s and %s from the responder and then press enter after pressing enter in the responder side",
			cfg->remote_connection_desc_path, cfg->remote_resource_desc_path);

	/* Wait for enter */
	while (enter != '\r' && enter != '\n')
		enter = getchar();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path, (char **)&resources->remote_rdma_conn_descriptor,
				&resources->remote_rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_error_get_descr(result));
		return result;
	}

	/* Read the remote sync event connection details */
	result = read_file(cfg->remote_resource_desc_path, (char **)&resources->sync_event_descriptor,
				&resources->sync_event_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the sync event export blob: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

/*
 * RDMA remote net sync event notify set task completed callback
 *
 * @se_set_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
rdma_remote_net_sync_event_notify_set_completed_callback(
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task,
	union doca_data task_user_data, union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	(void)se_set_task;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Assign success to the result */
	*result = DOCA_SUCCESS;
}

/*
 * RDMA remote net sync event notify set task error callback
 *
 * @se_set_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
rdma_remote_net_sync_event_notify_set_error_callback(struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task,
			 union doca_data task_user_data, union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	struct doca_task *task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Get the result of the task */
	*result = doca_task_get_status(task);
}

/*
 * RDMA remote net sync event get task completed callback
 *
 * @se_get_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
rdma_remote_net_sync_event_get_completed_callback(struct doca_rdma_task_remote_net_sync_event_get *se_get_task,
			     union doca_data task_user_data, union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	(void)se_get_task;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Assign success to the result */
	*result = DOCA_SUCCESS;
}

/*
 * RDMA remote net sync event get task error callback
 *
 * @se_get_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
rdma_remote_net_sync_event_get_error_callback(struct doca_rdma_task_remote_net_sync_event_get *se_get_task,
			 union doca_data task_user_data, union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	struct doca_task *task = doca_rdma_task_remote_net_sync_event_get_as_task(se_get_task);
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Get the result of the task */
	*result = doca_task_get_status(task);
}

/*
 * Requester side of the RDMA sync event
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
rdma_sync_event_requester(struct rdma_config *cfg)
{
	struct rdma_resources resources = {0};
	enum doca_ctx_states ctx_state;
	struct doca_task *task = NULL;
	struct doca_rdma_task_remote_net_sync_event_notify_set *se_set_task;
	struct doca_rdma_task_remote_net_sync_event_get *se_get_task;
	union doca_data ctx_user_data = {0};
	union doca_data task_user_data = {0};
	struct doca_buf_inventory *buf_inventory;
	struct doca_buf *get_buf;
	struct doca_buf *set_buf;
	void *set_buf_data;
	void *get_buf_data;
	size_t num_remaining_tasks = 0;
	const uint32_t mmap_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
	const uint32_t rdma_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result, tmp_result, task_result;

	/* Allocating resources */
	result = allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions,
					 sync_event_tasks_supported, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_rdma_task_remote_net_sync_event_notify_set_set_conf(resources.rdma,
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

	/* Include tasks counter in user data of context to be decremented in callbacks */
	ctx_user_data.ptr = &num_remaining_tasks;
	result = doca_ctx_set_user_data(resources.rdma_ctx, ctx_user_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set context user data: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/* Start RDMA context */
	result = doca_ctx_start(resources.rdma_ctx);
	if (result == DOCA_ERROR_IN_PROGRESS) {
		/* Export DOCA RDMA */
		result = doca_rdma_export(resources.rdma, &(resources.rdma_conn_descriptor),
						&(resources.rdma_conn_descriptor_size));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export DOCA RDMA: %s", doca_error_get_descr(result));
			goto request_stop_ctx;
		}


		/* Write and read connection details from the responder */
		result = write_read_connection(cfg, &resources);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to write and read connection details from the responder: %s",
					doca_error_get_descr(result));
			goto request_stop_ctx;
		}

		/* Connect RDMA */
		result = doca_rdma_connect(resources.rdma, resources.remote_rdma_conn_descriptor,
						resources.remote_rdma_conn_descriptor_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to connect the requester's DOCA RDMA to the responder's DOCA RDMA: %s",
					doca_error_get_descr(result));
			goto request_stop_ctx;
		}

		/* Move ctx to running state */
		do {
			if (doca_pe_progress(resources.pe) == 0)
				nanosleep(&ts, &ts);

			result = doca_ctx_get_state(resources.rdma_ctx, &ctx_state);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to retrieve RDMA context state: %s",
						doca_error_get_descr(result));
				goto request_stop_ctx;
			}
		} while (ctx_state != DOCA_CTX_STATE_RUNNING);
	} else if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start RDMA context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	/* Create remote mmap */
	result = doca_sync_event_remote_net_create_from_export(resources.doca_device, resources.sync_event_descriptor,
								resources.sync_event_descriptor_size,
								&(resources.remote_se));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create remote sync event from export: %s", doca_error_get_descr(result));
		goto request_stop_ctx;
	}

	/* Create DOCA buffer inventory */
	result = doca_buf_inventory_create(INVENTORY_NUM_INITIAL_ELEMENTS, &buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto request_stop_ctx;
	}

	/* Start DOCA buffer inventory */
	result = doca_buf_inventory_start(buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto destroy_buf_inventory;
	}


	/* Add dst buffer to DOCA buffer inventory */
	result = doca_buf_inventory_buf_get_by_data(buf_inventory, resources.mmap, resources.mmap_memrange,
						    sizeof(uint64_t), &set_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto stop_buf_inventory;
	}

	result = doca_buf_get_data(set_buf, &set_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for set task: %s", doca_error_get_descr(result));
		goto destroy_set_buf;
	}
	*(uint64_t *)set_buf_data = EXAMPLE_SET_VALUE;

	result = doca_buf_inventory_buf_get_by_addr(buf_inventory, resources.mmap, resources.mmap_memrange,
						    sizeof(uint64_t), &get_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s", doca_error_get_descr(result));
		goto destroy_set_buf;
	}

	result = doca_buf_get_data(get_buf, &get_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get buffer data for get task: %s", doca_error_get_descr(result));
		goto destroy_get_buf;
	}


	/* Include result in user data of task to be used in the callbacks */
	task_user_data.ptr = &task_result;

	/* Allocate and construct RDMA sync event set task */
	result = doca_rdma_task_remote_net_sync_event_notify_set_allocate_init(resources.rdma, resources.remote_se,
							set_buf, task_user_data, &se_set_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA sync event set task: %s", doca_error_get_descr(result));
		goto destroy_get_buf;
	}

	task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);

	/* Submit RDMA sync event set task */
	DOCA_LOG_INFO("Signaling remote sync event");
	num_remaining_tasks += 1;
	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA remote sync event set task: %s", doca_error_get_descr(result));
		goto free_task;
	}

	/* Wait for all tasks to be completed */
	while (num_remaining_tasks > 0) {
		if (doca_pe_progress(resources.pe) == 0)
			nanosleep(&ts, &ts);
	}

	/* Check result of task according to the result we update in the callbacks */
	result = task_result;
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RDMA remote sync event set task failed: %s", doca_error_get_descr(result));
		goto free_task;
	}

	DOCA_LOG_INFO("Remote sync event has been signaled successfully");

	doca_task_free(task);

	/* Submit RDMA sync event get task */
	result = doca_rdma_task_remote_net_sync_event_get_allocate_init(resources.rdma, resources.remote_se, get_buf,
							task_user_data, &se_get_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA sync event get task: %s", doca_error_get_descr(result));
		goto free_task;
	}

	DOCA_LOG_INFO("Waiting for remote sync event to be signaled");
	while (*(int *)get_buf_data <= EXAMPLE_SET_VALUE) {
		task = doca_rdma_task_remote_net_sync_event_get_as_task(se_get_task);

		result = doca_task_submit(task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit RDMA sync event get task: %s", doca_error_get_descr(result));
			goto free_task;
		}

		/* Wait for all tasks to be completed */
		while (doca_pe_progress(resources.pe) == 0)
			nanosleep(&ts, &ts);
	}

	doca_task_free(task);

	*(uint64_t *)set_buf_data = UINT64_MAX;

	/* Allocate and construct RDMA sync event set task */
	result = doca_rdma_task_remote_net_sync_event_notify_set_allocate_init(resources.rdma, resources.remote_se,
							set_buf, task_user_data, &se_set_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA sync event set task: %s", doca_error_get_descr(result));
		goto destroy_get_buf;
	}

	task = doca_rdma_task_remote_net_sync_event_notify_set_as_task(se_set_task);

	/* Submit RDMA sync event set task */
	DOCA_LOG_INFO("Notifying remote sync event for completion");
	num_remaining_tasks += 1;
	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit RDMA remote sync event set task: %s", doca_error_get_descr(result));
		goto free_task;
	}

	/* Wait for all tasks to be completed */
	while (num_remaining_tasks > 0) {
		if (doca_pe_progress(resources.pe) == 0)
			nanosleep(&ts, &ts);
	}

	/* Check result of task according to the result we update in the callbacks */
	result = task_result;
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RDMA remote sync event set task failed: %s", doca_error_get_descr(result));
		goto free_task;
	}

	DOCA_LOG_INFO("Remote sync event has been notified for completion successfully");

	DOCA_LOG_INFO("Done");

free_task:
	doca_task_free(task);
destroy_get_buf:
	tmp_result = doca_buf_dec_refcount(get_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease get_buf count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_set_buf:
	tmp_result = doca_buf_dec_refcount(set_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease set_buf count: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
stop_buf_inventory:
	tmp_result = doca_buf_inventory_stop(buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA buffer inventory: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_buf_inventory:
	tmp_result = doca_buf_inventory_destroy(buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA buffer inventory: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
request_stop_ctx:
	tmp_result = request_stop_ctx(resources.pe, resources.rdma_ctx);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(tmp_result));
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
