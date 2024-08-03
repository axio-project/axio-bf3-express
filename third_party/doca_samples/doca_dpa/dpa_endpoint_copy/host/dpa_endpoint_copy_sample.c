/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include <doca_error.h>
#include <doca_log.h>
#include <doca_types.h>

#include "dpa_common.h"

DOCA_LOG_REGISTER(ENDPOINT::SAMPLE);

/* Number of DPA threads */
const unsigned int num_dpa_threads = 1;

/* Remote thread arguments struct */
struct thread_arguments {
	struct dpa_resources *resources;		/* DOCA DPA resources */
	uint64_t *remote_buff;				/* Remote buffer address to copy to */
	struct doca_dpa_ep_addr *local_ep_addr;		/* Address of local endpoint */
	struct doca_dpa_ep_addr *remote_ep_addr;	/* Address of remote endpoint */
	uint32_t mem_remote_rkey;			/* Access key for remote buffer */
	struct doca_sync_event *thread_event;		/* DPA event for synchronizing between main thread */
							/* and remote thread */
	doca_dpa_dev_sync_event_t thread_event_handler;	/* Handler for thread_event */
};

/* Kernel function declaration */
extern doca_dpa_func_t update_event_kernel;

/* Kernel function declaration */
extern doca_dpa_func_t dpa_put_signal_nb;

/*
 * Updates thread_event using kernel_launch and wait for completion
 *
 * @doca_dpa [in]: Previously created DPA context
 * @kernel_comp_event [in]: Completion event for the kernel_launch
 * @comp_count [in]: Completion event value
 * @thread_event_handler [in]: Handler for thread event to update
 * @thread_event_val [in]: Value of thread event to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
update_thread_event(struct doca_dpa *doca_dpa, struct doca_sync_event *kernel_comp_event, uint64_t comp_count,
			doca_dpa_dev_sync_event_t thread_event_handler, uint64_t thread_event_val)
{
	doca_error_t result;

	result = doca_dpa_kernel_launch_update_set(doca_dpa, NULL, 0, kernel_comp_event, comp_count, num_dpa_threads,
					&update_event_kernel, thread_event_handler, thread_event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch update_event_kernel: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for the completion event of the kernel */
	result = doca_sync_event_wait_gt(kernel_comp_event, comp_count - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for kernel_comp_event: %s", doca_get_error_string(result));
		return result;
	}

	return result;
}

/*
 * Create and export 3 DPA events that are updated by the DPA and waited on by the CPU
 *
 * @doca_dpa [in]: Previously created DPA context
 * @doca_device [in]: DOCA device
 * @put_signal_comp_event [out]: Created DPA event
 * @thread_event [out]: Created DPA event
 * @kernel_comp_event [out]: Created DPA event
 * @remote_put_signal_comp_event [out]: Created remote event handler
 * @thread_event_handler [out]: Created event handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dpa_events(struct doca_dpa *doca_dpa, struct doca_dev *doca_device,
			struct doca_sync_event **put_signal_comp_event,	struct doca_sync_event **thread_event,
			struct doca_sync_event **kernel_comp_event,
			doca_sync_event_remote_t *remote_put_signal_comp_event,
			doca_dpa_dev_sync_event_t *thread_event_handler)
{
	doca_error_t result, tmp_result;

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, put_signal_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create put_signal_comp_event: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_sync_event_export_remote(*put_signal_comp_event, remote_put_signal_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to remote export put_signal_comp_event: %s", doca_get_error_string(result));
		goto destroy_put_signal_comp_event;
	}

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, thread_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create thread_event: %s", doca_get_error_string(result));
		goto destroy_put_signal_comp_event;
	}

	result = doca_sync_event_export_to_dpa(*thread_event, doca_dpa, thread_event_handler);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export kernel_comp_event to DPA: %s", doca_get_error_string(result));
		goto destroy_thread_event;
	}

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, kernel_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create kernel_comp_event: %s", doca_get_error_string(result));
		goto destroy_thread_event;
	}

	return result;

destroy_thread_event:
	tmp_result = doca_sync_event_destroy(*thread_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy thread_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_put_signal_comp_event:
	tmp_result = doca_sync_event_destroy(*put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy thread_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}


/*
 * Create DOCA DPA endpoint, get created endpoint address (if not NULL) and export created endpoint (if not NULL)
 *
 * @worker [in]: Worker to create the endpoint for
 * @ep_caps [in]: capabilities enabled on the endpoint
 * @ep [out]: Created endpoint
 * @ep_addr [out]: Pointer to the endpoint address
 * @ep_handle [out]: Handle of the endpoint
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dpa_endpoint_resources(struct doca_dpa_worker *worker, unsigned int ep_caps,
				struct doca_dpa_ep **ep, struct doca_dpa_ep_addr **ep_addr,
				doca_dpa_dev_ep_t *ep_handle)
{
	size_t ep_addr_length;
	doca_error_t result;
	doca_error_t tmp_result;

	/* Creating DOCA DPA endpoint */
	result = doca_dpa_ep_create(worker, ep_caps, ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA endpoint: %s", doca_get_error_string(result));
		return result;
	}

	if (ep_addr != NULL) {
		/* Get DOCA DPA endpoint address */
		result = doca_dpa_ep_addr_get(*ep, ep_addr, &ep_addr_length);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to get DOCA DPA endpoint address: %s", doca_get_error_string(result));
			goto destroy_ep;
		}
	}

	if (ep_handle != NULL) {
		/* Export DPA endpoint */
		result = doca_dpa_ep_dev_export(*ep, ep_handle);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export DOCA DPA endpoint: %s", doca_get_error_string(result));
			goto free_ep_addr;
		}
	}

	return result;

free_ep_addr:
	/* Free DPA endpoint address */
	tmp_result = doca_dpa_ep_addr_free(*ep_addr);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to free endpoint address: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_ep:
	/* destroy DPA endpoint */
	tmp_result = doca_dpa_ep_destroy(*ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA DPA endpoint: %s", doca_get_error_string(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * Function for remote thread. Creates buffer, DPA endpoint and memory, connects to main thread's endpoint
 * to copy to remote buffer.
 *
 * @args [in]: thread_arguments
 * @return: NULL
 */
void *remote_endpoint_thread_func(void *args)
{
	struct thread_arguments *thread_args = (struct thread_arguments *)args;
	struct doca_dpa_mem *remote_mem;
	/* Access flags for DPA Endpoint and DPA memory */
	unsigned int access = DOCA_ACCESS_LOCAL_READ_WRITE | DOCA_ACCESS_RDMA_WRITE | DOCA_ACCESS_RDMA_READ |
				DOCA_ACCESS_RDMA_ATOMIC;
	/* Remote DPA endpoint */
	struct doca_dpa_endpoint *remote_ep;
	/* Remove DPA worker for endpoint */
	doca_dpa_worker_t remote_worker;
	/* Completion event for kernel_launch */
	struct doca_sync_event *kernel_comp_event;
	/* Thread event val */
	uint64_t thread_event_val = 1;
	/* Completion event val */
	uint64_t comp_event_val = 1;
	doca_error_t result;
	doca_error_t tmp_result;

	/* Allocating remote buffer*/
	thread_args->remote_buff = malloc(sizeof(uint64_t));
	if (thread_args->remote_buff == NULL) {
		DOCA_LOG_ERR("Failed to allocate remote buffer");
		return NULL;
	}
	*(thread_args->remote_buff) = 5;

	/* Wait on thread_event until the main thread updates that it has created all the resources */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_get_error_string(result));
		goto free_buffer;
	}

	/* Create DOCA DPA kernel completion event */
	result = create_doca_dpa_completion_sync_event(thread_args->resources->doca_dpa,
							thread_args->resources->doca_device, &kernel_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create kernel_comp_event: %s", doca_get_error_string(result));
		goto free_buffer;
	}

	/* Create DOCA DPA worker */
	result = doca_dpa_worker_create(thread_args->resources->doca_dpa, &remote_worker, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA worker: %s", doca_get_error_string(result));
		goto destroy_event;
	}

	/* Create DOCA DPA endpoint and its address */
	result = create_dpa_endpoint_resources(remote_worker, access, (struct doca_dpa_ep **)&remote_ep, &(thread_args->remote_ep_addr), NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA endpoint resources: %s", doca_get_error_string(result));
		goto destroy_worker;
	}

	/* Register DPA host memory */
	result = doca_dpa_mem_host_register(thread_args->resources->doca_dpa, thread_args->remote_buff,
					sizeof(uint64_t), access, &remote_mem, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register host memory: %s", doca_get_error_string(result));
		goto free_ep_address;
	}

	/* Obtain remote memory key */
	result = doca_dpa_mem_rkey_get(remote_mem, &(thread_args->mem_remote_rkey));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get memory remote key: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Remote thread finished allocating all DPA resources, signaling to main thread");

	/*
	 * Update (increment) the thread_event so that the main thread can know that the remote thread has created
	 * all the resources
	 */
	result = update_thread_event(thread_args->resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_args->thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/*
	 * Wait on thread_event until the main thread updates that the local endpoint has been connected
	 * to the remote endpoint
	 */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Connect the two endpoints */
	result = doca_dpa_ep_connect((struct doca_dpa_ep *)remote_ep, thread_args->local_ep_addr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect to local endpoint: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Remote thread finished connecting to the main thread's endpoint, signaling to main thread");

	/*
	 * Update (increment) the thread_event so that the main thread can know that remote endpoint has been
	 * connected to the local endpoint
	 */
	result = update_thread_event(thread_args->resources->doca_dpa, kernel_comp_event, comp_event_val,
					thread_args->thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Wait on thread_event until the main thread updates that it has copied the buffer */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Sleep 2 seconds before destroying DPA resources */
	sleep(2);

unregister_mem:
	/* Unregister DPA memory */
	tmp_result = doca_dpa_mem_unregister(remote_mem);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to unregister memory: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

free_ep_address:
	/* Free DPA endpoint address */
	tmp_result = doca_dpa_ep_addr_free(thread_args->remote_ep_addr);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to free local endpoint address: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	/* destroy DPA endpoint */
	tmp_result = doca_dpa_ep_destroy((struct doca_dpa_ep *)remote_ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA DPA endpoint: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_worker:
	/* Destroy remote_worker */
	tmp_result = doca_dpa_worker_destroy(remote_worker);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy remote_worker: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_event:
	/* Destroy kernel_comp_event */
	tmp_result = doca_sync_event_destroy(kernel_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy kernel_comp_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
free_buffer:
	/* Free remote_buff */
	free(thread_args->remote_buff);
	return NULL;
}

/*
 * Run endpoint sample
 *
 * @resources [in]: DOCA DPA resources that the DPA sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
endpoint_copy(struct dpa_resources *resources)
{
	/* Completion event for dpa_put_signal_nb function */
	struct doca_sync_event *put_signal_comp_event;
	/* Remote event handler for put_signal_comp_event */
	doca_sync_event_remote_t remote_put_signal_comp_event;
	/* Put signal event val */
	uint64_t put_signal_comp_event_val = 1;
	/* Event for synchronizing between main thread and remote thread */
	struct doca_sync_event *thread_event;
	/* Event handler for thread_event */
	doca_dpa_dev_sync_event_t thread_event_handler;
	/* Thread event val */
	uint64_t thread_event_val = 1;
	/* Completion event for kernel_launch */
	struct doca_sync_event *kernel_comp_event;
	/* Completion event val */
	uint64_t comp_event_val = 1;
	/* Local DPA endpoint */
	struct doca_dpa_endpoint *local_ep;
	/* Handler for local_ep */
	doca_dpa_dev_ep_t local_ep_handle;
	/* Address of local_ep */
	struct doca_dpa_ep_addr *local_ep_addr = NULL;
	/* Local worker for endpoint */
	doca_dpa_worker_t local_worker;
	/* Local buffer to copy to remote buffer */
	uint64_t local_buff = 10;
	/* doca_dpa_mem for local_buff */
	struct doca_dpa_mem *local_mem;
	/* Access flags for DPA Endpoint and DPA memory */
	unsigned int access = DOCA_ACCESS_LOCAL_READ_WRITE | DOCA_ACCESS_RDMA_WRITE | DOCA_ACCESS_RDMA_READ |
				DOCA_ACCESS_RDMA_ATOMIC;
	/* handler for local_mem */
	doca_dpa_dev_mem_t local_mem_handle;
	/* Argument for remote thread function */
	struct thread_arguments args = {
		.resources = resources,
		.local_ep_addr = local_ep_addr,
	};
	/* Remote thread ID */
	pthread_t tid = 0;
	doca_error_t result;
	doca_error_t tmp_result;
	int res = 0;

	/* Creating DOCA DPA event */
	result = create_dpa_events(resources->doca_dpa, resources->doca_device, &put_signal_comp_event, &thread_event,
					&kernel_comp_event, &remote_put_signal_comp_event, &thread_event_handler);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA events: %s", doca_get_error_string(result));
		return result;
	}

	/* Add thread event and its handler to the remote thread arguments for synchronizing */
	args.thread_event = thread_event,
	args.thread_event_handler = thread_event_handler,

	/* Run remote endpoint thread */
	res = pthread_create(&tid, NULL, remote_endpoint_thread_func, (void *)&args);
	if (res != 0) {
		DOCA_LOG_ERR("Failed to create thread");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto destroy_events;
	}

	/* Create DOCA DPA worker */
	result = doca_dpa_worker_create(resources->doca_dpa, &local_worker, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA worker: %s", doca_get_error_string(result));
		goto destroy_events;
	}

	/* Create DOCA DPA endpoint and its address */
	result = create_dpa_endpoint_resources(local_worker, access, (struct doca_dpa_ep **)&local_ep, &(args.local_ep_addr), &local_ep_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA endpoint resources: %s", doca_get_error_string(result));
		goto destroy_worker;
	}

	/* Register DPA host memory */
	result = doca_dpa_mem_host_register(resources->doca_dpa, &local_buff, sizeof(uint64_t), access,
					&local_mem, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register host memory: %s", doca_get_error_string(result));
		goto free_ep_address;
	}

	/* Export DPA host memory */
	result = doca_dpa_mem_dev_export(local_mem, &local_mem_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export host memory: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Main thread finished allocating all DPA resources, signaling to remote thread");

	/*
	 * Update (increment) the thread_event so that the remote thread can know that the main thread has created
	 * all the resources
	 */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Wait on thread_event until the remote thread updates that it has created all the resources */
	result = doca_sync_event_wait_gt(thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Connect the two endpoints */
	result = doca_dpa_ep_connect((struct doca_dpa_ep *)local_ep, args.remote_ep_addr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect local endpoint to remote endpoint: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Main thread finished connecting to remote thread's endpoint, signaling to remote thread");

	/*
	 * Update (increment) the thread_event so that the remote thread can know that local endpoint has been
	 * connected to the remote endpoint
	 */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/*
	 * Wait on thread_event until the remote thread updates that the remote endpoint has been connected
	 * to the local endpoint
	 */
	result = doca_sync_event_wait_gt(thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Main thread launching kernel to copy local buffer to remote buffer");
	DOCA_LOG_INFO("Before copying: local buffer = %lu, remote buffer = %lu", local_buff, *args.remote_buff);

	/* Launch dpa_put_signal_nb kernel to copy local_buff to remote_buff */
	result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, NULL, 0, NULL, 0, num_dpa_threads, &dpa_put_signal_nb,
			local_ep_handle, &local_buff, local_mem_handle, sizeof(local_buff), args.remote_buff,
			args.mem_remote_rkey, remote_put_signal_comp_event, put_signal_comp_event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch dpa_put_signal_nb kernel: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Wait for the completion event of the dpa_put_signal_nb */
	result = doca_sync_event_wait_gt(put_signal_comp_event, put_signal_comp_event_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for put_signal_comp_event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	DOCA_LOG_INFO("Main thread finished copying local buffer to remote buffer, signaling to remote thread");
	DOCA_LOG_INFO("After copying: local buffer = %lu, remote buffer = %lu", local_buff, *args.remote_buff);

	/* Update (increment) the thread_event so that the remote thread can know that the copying has finished */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val,
					thread_event_handler, thread_event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_get_error_string(result));
		goto unregister_mem;
	}

	/* Wait until the remote thread finishes */
	res = pthread_join(tid, NULL);
	if (res != 0) {
		DOCA_LOG_ERR("Failed to join thread");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto unregister_mem;
	}


unregister_mem:
	/* Sleep 2 seconds before destroying DPA resources */
	sleep(2);
	/* Unregister DPA memory */
	tmp_result = doca_dpa_mem_unregister(local_mem);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to unregister memory: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

free_ep_address:
	/* free DPA endpoint address */
	tmp_result = doca_dpa_ep_addr_free(local_ep_addr);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to free local endpoint address: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	/* destroy DPA endpoint */
	tmp_result = doca_dpa_ep_destroy((struct doca_dpa_ep *)local_ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA DPA endpoint: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_worker:
	/* Destroy local_worker */
	tmp_result = doca_dpa_worker_destroy(local_worker);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy local_worker: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_events:
	/* Destroy events */
	tmp_result = doca_sync_event_destroy(put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy put_signal_comp_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_sync_event_destroy(thread_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy thread_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_sync_event_destroy(kernel_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy kernel_comp_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
