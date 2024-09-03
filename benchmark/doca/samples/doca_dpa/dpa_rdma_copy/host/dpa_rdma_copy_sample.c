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
#include <doca_mmap.h>
#include <doca_rdma.h>
#include <doca_buf_array.h>

#include "dpa_common.h"

DOCA_LOG_REGISTER(DPA_RDMA::SAMPLE);

#define LOCAL_BUF_VALUE (10) /* value of local buffer counter */

#define REMOTE_BUF_VALUE (5) /* value of remote buffer counter */

/* Number of DPA threads */
static const unsigned int num_dpa_threads = 1;

/* Remote thread arguments struct */
struct thread_arguments {
	struct dpa_resources *resources;		/* DOCA DPA resources */
	uint64_t *remote_buff;		/* Remote buffer address to copy to */
	struct doca_dpa_dev_buf_arr *remote_buf_arr_dpa_handle;		/* DPA handle of buf array */
	void *local_connection_details;		/* connection details of local rdma */
	size_t local_connection_details_len;		/* length of connection details of local rdma */
	void *remote_connection_details;		/* connection details of remote rdma */
	size_t remote_connection_details_len;		/* length of connection details of remote rdma */
	struct doca_sync_event *thread_event;		/* DPA event for synchronizing between main thread */
							/* and remote thread */
	doca_dpa_dev_sync_event_t thread_event_handler;	/* Handler for thread_event */
};

/* Kernel function declaration */
extern doca_dpa_func_t update_event_kernel;

/* Kernel function declaration */
extern doca_dpa_func_t dpa_rdma_write_and_signal;

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
		DOCA_LOG_ERR("Failed to launch update_event_kernel: %s", doca_error_get_descr(result));
		return result;
	}

	/* Wait for the completion event of the kernel */
	result = doca_sync_event_wait_gt(kernel_comp_event, comp_count - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for kernel_comp_event: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

/*
 * Create and export 4 DPA events that are updated by the DPA and waited on by the CPU
 *
 * @doca_dpa [in]: Previously created DPA context
 * @doca_device [in]: DOCA device
 * @put_signal_comp_event [out]: Created DPA event
 * @thread_event [out]: Created DPA event
 * @kernel_comp_event [out]: Created DPA event
 * @remote_put_signal_comp_event [out]: Created remote event
 * @remote_put_signal_comp_event_handle [out]: Created remote event handler
 * @thread_event_handler [out]: Created event handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dpa_events(struct doca_dpa *doca_dpa, struct doca_dev *doca_device,
		  struct doca_sync_event **put_signal_comp_event, struct doca_sync_event **thread_event,
		  struct doca_sync_event **kernel_comp_event,
		  struct doca_sync_event_remote_net **remote_put_signal_comp_event,
		  doca_dpa_dev_sync_event_remote_net_t *remote_put_signal_comp_event_handle,
		  doca_dpa_dev_sync_event_t *thread_event_handler)
{
	doca_error_t result, tmp_result;

	result = create_doca_remote_net_sync_event(doca_device, put_signal_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create put_signal_comp_event: %s", doca_error_get_descr(result));
		return result;
	}

	result = export_doca_remote_net_sync_event_to_dpa(doca_device, doca_dpa, *put_signal_comp_event,
		remote_put_signal_comp_event, remote_put_signal_comp_event_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to remote export put_signal_comp_event: %s", doca_error_get_descr(result));
		goto destroy_put_signal_comp_event;
	}

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, thread_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create thread_event: %s", doca_error_get_descr(result));
		goto destroy_remote_put_signal_comp_event;
	}

	result = doca_sync_event_export_to_dpa(*thread_event, doca_dpa, thread_event_handler);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export kernel_comp_event to DPA: %s", doca_error_get_descr(result));
		goto destroy_thread_event;
	}

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, kernel_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create kernel_comp_event: %s", doca_error_get_descr(result));
		goto destroy_thread_event;
	}

	return result;

destroy_thread_event:
	tmp_result = doca_sync_event_destroy(*thread_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy thread_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_remote_put_signal_comp_event:
	tmp_result = doca_sync_event_remote_net_destroy(*remote_put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy remote_put_signal_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_put_signal_comp_event:
	tmp_result = doca_sync_event_destroy(*put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy put_signal_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * Create DOCA rdma instance
 * Export created rdma connection details (if not NULL) and get DPA handle of created rdma (if not NULL)
 *
 * @doca_dpa [in]: DPA context to set datapath on
 * @doca_device [in]: device to associate to rdma context
 * @rdma_caps [in]: capabilities enabled on the rdma context
 * @rdma [out]: Created rdma
 * @rdma_dpa_handle [out]: DPA Handle of the rdma
 * @rdma_connection_details [out]: pointer to the rdma connection details
 * @rdma_connection_details_len [out]: pointer to the rdma connection details length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_rdma_resources(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, unsigned int rdma_caps,
		      struct doca_rdma **rdma, struct doca_dpa_dev_rdma **rdma_dpa_handle,
		      const void **rdma_connection_details, size_t *rdma_connection_details_len)
{
	struct doca_ctx *rdma_as_doca_ctx;
	doca_error_t result;
	doca_error_t tmp_result;

	/* Creating DOCA rdma instance */
	result = doca_rdma_create(doca_device, rdma);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA rdma instance: %s", doca_error_get_descr(result));
		return result;
	}

	/* Setup DOCA rdma as DOCA context */
	rdma_as_doca_ctx = doca_rdma_as_ctx(*rdma);

	/* Set permissions for DOCA rdma */
	result = doca_rdma_set_permissions(*rdma, rdma_caps);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions for DOCA rdma: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Set grh flag for DOCA rdma */
	result = doca_rdma_set_grh_enabled(*rdma, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set grh for DOCA rdma: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Set datapath of DOCA rdma context on DPA */
	result = doca_ctx_set_datapath_on_dpa(rdma_as_doca_ctx, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set datapath for DOCA rdma on DPA: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Start DOCA rdma context */
	result = doca_ctx_start(rdma_as_doca_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start context for DOCA rdma: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Get DPA handle of DOCA rdma */
	if (rdma_dpa_handle != NULL) {
		result = doca_rdma_get_dpa_handle(*rdma, rdma_dpa_handle);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to get DPA handle for DOCA rdma: %s", doca_error_get_descr(result));
			goto destroy_rdma;
		}
	}

	/* Export connection details of DOCA rdma */
	if (rdma_connection_details != NULL && rdma_connection_details_len != NULL) {
		result = doca_rdma_export(*rdma, rdma_connection_details, rdma_connection_details_len);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export DOCA rdma: %s", doca_error_get_descr(result));
			goto destroy_rdma;
		}
	}

	return result;

destroy_rdma:
	/* destroy DPA rdma */
	tmp_result = doca_ctx_stop(rdma_as_doca_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA rdma context: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_rdma_destroy(*rdma);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA rdma instance: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * Create DOCA mmap
 *
 * @doca_device [in]: device to associate to mmap context
 * @mmap_permissions [in]: capabilities enabled on the mmap
 * @memrange_addr [in]: memrange address to set on the mmap
 * @memrange_len [in]: length of memrange to set on the mmap
 * @mmap [out]: Created mmap
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_mmap_resources(struct doca_dev *doca_device, unsigned int mmap_permissions, void *memrange_addr,
		      size_t memrange_len, struct doca_mmap **mmap)
{
	doca_error_t result;
	doca_error_t tmp_result;

	/* Creating DOCA mmap */
	result = doca_mmap_create(mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA mmap: %s", doca_error_get_descr(result));
		return result;
	}

	/* Add DOCA device to DOCA mmap */
	result = doca_mmap_add_dev(*mmap, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add DOCA device: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	/* Set permissions for DOCA mmap */
	result = doca_mmap_set_permissions(*mmap, mmap_permissions);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions for DOCA mmap: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	/* Set memrange for DOCA mmap */
	result = doca_mmap_set_memrange(*mmap, memrange_addr, memrange_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set memrange for DOCA mmap: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	/* Start DOCA mmap */
	result = doca_mmap_start(*mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA mmap: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	return result;

destroy_mmap:
	/* destroy DOCA mmap */
	tmp_result = doca_mmap_destroy(*mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA mmap: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * Create DOCA buf array
 *
 * @doca_dpa [in]: DPA context to set datapath on
 * @mmap [in]: mmap to associate to buf array context
 * @element_size [in]: size of the element the buf array will hold
 * @num_elements [in]: number of the elements the buf array will hold
 * @buf_arr [out]: Created buf array
 * @dpa_buf_arr [out]: DPA Handle of the buf array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_buf_array_resources(struct doca_dpa *doca_dpa, struct doca_mmap *mmap, size_t element_size,
			   uint32_t num_elements, struct doca_buf_arr **buf_arr, struct doca_dpa_dev_buf_arr **dpa_buf_arr)
{
	doca_error_t result;
	doca_error_t tmp_result;

	/* Creating DOCA buf array */
	result = doca_buf_arr_create(mmap, buf_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buf array: %s", doca_error_get_descr(result));
		return result;
	}

	/* Set params to DOCA buf array */
	result = doca_buf_arr_set_params(*buf_arr, element_size, num_elements, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add DOCA device: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Set target device to DOCA buf array */
	result = doca_buf_arr_set_target_dpa(*buf_arr, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set target device for DOCA buf array: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Start DOCA buf array */
	result = doca_buf_arr_start(*buf_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA buf array: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Start DOCA buf array */
	if (dpa_buf_arr != NULL) {
		result = doca_buf_arr_get_dpa_handle(*buf_arr, dpa_buf_arr);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to get DPA handle of DOCA buf array: %s", doca_error_get_descr(result));
			goto destroy_buf_arr;
		}
	}

	return result;

destroy_buf_arr:
	/* destroy DOCA buf array */
	tmp_result = doca_buf_arr_destroy(*buf_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA buf array: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

/*
 * Function for remote thread. Creates buffer, rdma and memory, connects to main thread's rdma
 * to copy to remote buffer.
 *
 * @args [in]: thread_arguments
 * @return: NULL
 */
void *remote_rdma_thread_func(void *args)
{
	struct thread_arguments *thread_args = (struct thread_arguments *)args;
	/* Remote buffer DOCA mmap */
	struct doca_mmap *remote_buf_mmap;
	/* Remote buffer buf array */
	struct doca_buf_arr *buf_array;
	/* Access flags for rdma and mmap */
	unsigned int access = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE | DOCA_ACCESS_FLAG_RDMA_READ |
				DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	/* Remote rdma */
	struct doca_rdma *remote_rdma;
	/* Completion event for kernel_launch */
	struct doca_sync_event *kernel_comp_event;
	/* Thread event val */
	uint64_t thread_event_val = 1;
	/* Completion event val */
	uint64_t comp_event_val = 1;
	/* Size of the buffer */
	size_t buffer_size = sizeof(uint64_t);
	doca_error_t result;
	doca_error_t tmp_result;

	/* Allocating remote buffer*/
	thread_args->remote_buff = (uint64_t *)malloc(buffer_size);
	if (thread_args->remote_buff == NULL) {
		DOCA_LOG_ERR("Failed to allocate remote buffer");
		return NULL;
	}
	*(thread_args->remote_buff) = REMOTE_BUF_VALUE;

	/* Wait on thread_event until the main thread updates that it has created all the resources */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_error_get_descr(result));
		goto free_buffer;
	}

	/* Create DOCA DPA kernel completion event */
	result = create_doca_dpa_completion_sync_event(thread_args->resources->doca_dpa,
							thread_args->resources->doca_device, &kernel_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create kernel_comp_event: %s", doca_error_get_descr(result));
		goto free_buffer;
	}

	/* Create DOCA rdma and its resources */
	result = create_rdma_resources(thread_args->resources->doca_dpa, thread_args->resources->doca_device, access,
		(struct doca_rdma **)&remote_rdma, NULL, (const void **)&(thread_args->remote_connection_details), &(thread_args->remote_connection_details_len));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA rdma resources: %s", doca_error_get_descr(result));
		goto destroy_event;
	}

	/* Create DOCA mmap for the remote buffer and its resources */
	result = create_mmap_resources(thread_args->resources->doca_device, access, thread_args->remote_buff, buffer_size,
		(struct doca_mmap **)&remote_buf_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA mmap resources: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Create DOCA buf array for the remote buffer and its resources */
	result = create_buf_array_resources(thread_args->resources->doca_dpa, remote_buf_mmap, buffer_size, 1,
		(struct doca_buf_arr **)&buf_array, &(thread_args->remote_buf_arr_dpa_handle));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buf array resources: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	DOCA_LOG_INFO("Remote thread finished allocating all DPA resources, signaling to main thread");

	/*
	 * Update (increment) the thread_event so that the main thread can know that the remote thread has created
	 * all the resources
	 */
	result = update_thread_event(thread_args->resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_args->thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/*
	 * Wait on thread_event until the main thread updates that the local rdma has been connected
	 * to the remote rdma
	 */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Connect the two rdma instances */
	result = doca_rdma_connect(remote_rdma, thread_args->local_connection_details,
		thread_args->local_connection_details_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect to local rdma: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	DOCA_LOG_INFO("Remote thread finished connecting to the main thread's rdma instances, signaling to main thread");

	/*
	 * Update (increment) the thread_event so that the main thread can know that remote rdma has been
	 * connected to the local rdma
	 */
	result = update_thread_event(thread_args->resources->doca_dpa, kernel_comp_event, comp_event_val,
					thread_args->thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Wait on thread_event until the main thread updates that it has copied the buffer */
	result = doca_sync_event_wait_gt(thread_args->thread_event, thread_event_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Sleep 2 seconds before destroying DPA resources */
	sleep(2);

destroy_buf_arr:
	/* Destroy DOCA buf array */
	tmp_result = doca_buf_arr_destroy(buf_array);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy buf array: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_mmap:
	/* Destroy DOCA mmap */
	tmp_result = doca_mmap_destroy(remote_buf_mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_rdma:
	/* Destroy DOCA rdma */
	tmp_result = doca_ctx_stop(doca_rdma_as_ctx(remote_rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA rdma context: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_rdma_destroy(remote_rdma);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy remote rdma context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_event:
	/* Destroy kernel_comp_event */
	tmp_result = doca_sync_event_destroy(kernel_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy kernel_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

free_buffer:
	/* Free remote_buff */
	free(thread_args->remote_buff);
	return NULL;
}

/*
 * Run DPA rdma sample
 *
 * @resources [in]: DOCA DPA resources that the DPA sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
dpa_rdma_copy(struct dpa_resources *resources)
{
	/* Completion event for dpa_rdma_write_and_signal function */
	struct doca_sync_event *put_signal_comp_event;
	/* Remote event for put_signal_comp_event */
	struct doca_sync_event_remote_net *remote_put_signal_comp_event;
	/* Remote event handler for put_signal_comp_event */
	doca_dpa_dev_sync_event_remote_net_t remote_put_signal_comp_event_handle;
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
	/* Local rdma instance */
	struct doca_rdma *local_rdma;
	/* Handler for local_ep */
	struct doca_dpa_dev_rdma *local_rdma_handle;
	/* Local buffer DOCA mmap */
	struct doca_mmap *local_buf_mmap;
	/* Local buffer to copy to remote buffer */
	uint64_t local_buff = LOCAL_BUF_VALUE;
	/* Local buffer buf array */
	struct doca_buf_arr *buf_array;
	/* buf array DPA handle */
	struct doca_dpa_dev_buf_arr *local_buf_arr_dpa_handle;
	/* Access flags for DPA Endpoint and DPA memory */
	unsigned int access = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE | DOCA_ACCESS_FLAG_RDMA_READ |
			      DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	/* Argument for remote thread function */
	struct thread_arguments args = {
		.resources = resources,
	};
	/* Remote thread ID */
	pthread_t tid = 0;
	doca_error_t result;
	doca_error_t tmp_result;
	int res = 0;

	/* Creating DOCA DPA event */
	result = create_dpa_events(resources->doca_dpa, resources->doca_device, &put_signal_comp_event, &thread_event,
					&kernel_comp_event, &remote_put_signal_comp_event,
					&remote_put_signal_comp_event_handle, &thread_event_handler);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA events: %s", doca_error_get_descr(result));
		return result;
	}

	/* Add thread event and its handler to the remote thread arguments for synchronizing */
	args.thread_event = thread_event,
	args.thread_event_handler = thread_event_handler,

	/* Run remote rdma thread */
	res = pthread_create(&tid, NULL, remote_rdma_thread_func, (void *)&args);
	if (res != 0) {
		DOCA_LOG_ERR("Failed to create thread");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto destroy_events;
	}

	/* Create DOCA rdma and its resources */
	res = create_rdma_resources(resources->doca_dpa, resources->doca_device, access,
		(struct doca_rdma **)&local_rdma, &local_rdma_handle, (const void **)&(args.local_connection_details),
		&(args.local_connection_details_len));
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA rdma resources: %s", doca_error_get_descr(result));
		goto destroy_events;
	}

	/* Create DOCA mmap for the local buffer and its resources */
	result = create_mmap_resources(resources->doca_device, access, &local_buff, sizeof(local_buff),
		(struct doca_mmap **)&local_buf_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA mmap resources: %s", doca_error_get_descr(result));
		goto destroy_rdma;
	}

	/* Create DOCA buf array for the local buffer and its resources */
	result = create_buf_array_resources(resources->doca_dpa, local_buf_mmap, sizeof(local_buff), 1,
		(struct doca_buf_arr **)&buf_array, &local_buf_arr_dpa_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buf array resources: %s", doca_error_get_descr(result));
		goto destroy_mmap;
	}

	DOCA_LOG_INFO("Main thread finished allocating all DPA resources, signaling to remote thread");

	/*
	 * Update (increment) the thread_event so that the remote thread can know that the main thread has created
	 * all the resources
	 */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Wait on thread_event until the remote thread updates that it has created all the resources */
	result = doca_sync_event_wait_gt(thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Connect the two rdma instances */
	result = doca_rdma_connect(local_rdma, args.remote_connection_details, args.remote_connection_details_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect local rdma to remote rdma: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	DOCA_LOG_INFO("Main thread finished connecting to remote thread's endpoint, signaling to remote thread");

	/*
	 * Update (increment) the thread_event so that the remote thread can know that local endpoint has been
	 * connected to the remote endpoint
	 */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val++,
					thread_event_handler, thread_event_val++);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/*
	 * Wait on thread_event until the remote thread updates that the remote endpoint has been connected
	 * to the local endpoint
	 */
	result = doca_sync_event_wait_gt(thread_event, thread_event_val++ - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	DOCA_LOG_INFO("Main thread launching kernel to copy local buffer to remote buffer");
	DOCA_LOG_INFO("Before copying: local buffer = %lu, remote buffer = %lu", local_buff, *args.remote_buff);

	/* Launch dpa_rdma_write_and_signal kernel to copy local_buff to remote_buff */
	result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, NULL, 0, NULL, 0, num_dpa_threads, &dpa_rdma_write_and_signal,
			(uint64_t)local_rdma_handle, (uint64_t)(args.remote_buf_arr_dpa_handle), (uint64_t)local_buf_arr_dpa_handle,
			sizeof(local_buff), remote_put_signal_comp_event_handle, put_signal_comp_event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch dpa_rdma_write_and_signal kernel: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Wait for the completion event of the dpa_rdma_write_and_signal */
	result = doca_sync_event_wait_gt(put_signal_comp_event, put_signal_comp_event_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for put_signal_comp_event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	DOCA_LOG_INFO("Main thread finished copying local buffer to remote buffer, signaling to remote thread");
	DOCA_LOG_INFO("After copying: local buffer = %lu, remote buffer = %lu", local_buff, *args.remote_buff);

	/* Update (increment) the thread_event so that the remote thread can know that the copying has finished */
	result = update_thread_event(resources->doca_dpa, kernel_comp_event, comp_event_val,
					thread_event_handler, thread_event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update thread event: %s", doca_error_get_descr(result));
		goto destroy_buf_arr;
	}

	/* Wait until the remote thread finishes */
	res = pthread_join(tid, NULL);
	if (res != 0) {
		DOCA_LOG_ERR("Failed to join thread");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto destroy_buf_arr;
	}


destroy_buf_arr:
	/* Destroy DOCA buf array */
	tmp_result = doca_buf_arr_destroy(buf_array);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy buf array: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_mmap:
	/* Destroy DOCA mmap */
	tmp_result = doca_mmap_destroy(local_buf_mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_rdma:
	/* Destroy DOCA rdma */
	tmp_result = doca_ctx_stop(doca_rdma_as_ctx(local_rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA rdma context: %s", doca_error_get_descr(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_rdma_destroy(local_rdma);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy local rdma context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

destroy_events:
	/* Destroy events */
	tmp_result = doca_sync_event_destroy(put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy put_signal_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_sync_event_remote_net_destroy(remote_put_signal_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy remote_put_signal_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_sync_event_destroy(thread_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy thread_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	tmp_result = doca_sync_event_destroy(kernel_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy kernel_comp_event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
