/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <doca_mmap.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dma.h>
#include <doca_types.h>
#include <doca_log.h>
#include <doca_pe.h>
#include <doca_mmap_advise.h>

#include <samples/common.h>

DOCA_LOG_REGISTER(CACHE_INVALIDATE::SAMPLE);

/**
 * This sample demonstrates how to invalidate a cache of a buffer after DMA operation
 */

/**
 * This macro is used to minimize code size.
 * The macro runs an expression and returns error if the expression status is not DOCA_SUCCESS
 */
#define EXIT_ON_FAILURE(_expression_) \
	{ \
		doca_error_t _status_ = _expression_; \
\
		if (_status_ != DOCA_SUCCESS) { \
			DOCA_LOG_ERR("%s failed with status %s", __func__, doca_error_get_descr(_status_)); \
			return _status_; \
		} \
	}

#define NUM_TASKS (1)
#define DMA_BUFFER_SIZE (1024)
#define BUFFER_SIZE (DMA_BUFFER_SIZE * 2 * NUM_TASKS)
#define BUF_INVENTORY_SIZE (NUM_TASKS * 2)

/**
 * This struct defines the program context.
 */
struct cache_invalidate_sample_state {
	struct doca_dev *device;
	struct doca_mmap *mmap;
	struct doca_buf_inventory *inventory;
	struct doca_pe *pe;

	/**
	 * Buffer
	 * This buffer is used for the source and destination.
	 * Real life scenario may use more memory areas.
	 */
	uint8_t *buffer;

	struct doca_dma *dma;
	struct doca_ctx *dma_ctx;
	struct doca_buf *dma_source;
	struct doca_buf *dma_destination;
	struct doca_dma_task_memcpy *dma_task;

	struct doca_mmap_advise *mmap_advise;
	struct doca_ctx *mmap_advise_ctx;
	struct doca_mmap_advise_task_invalidate_cache *cache_invalidate_task;

	bool run_pe;
};

/**
 * Allocates a buffer that will be used for the source and destination buffers.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t allocate_buffer(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Allocating buffer");

	state->buffer = (uint8_t *)malloc(BUFFER_SIZE);
	if (state->buffer == NULL)
		return DOCA_ERROR_NO_MEMORY;

	return DOCA_SUCCESS;
}

/*
 * Check if DOCA device is DMA and cache invalidate capable
 *
 * @devinfo [in]: Device to check
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t check_dev_capable(struct doca_devinfo *devinfo)
{
	doca_error_t status = doca_dma_cap_task_memcpy_is_supported(devinfo);

	if (status != DOCA_SUCCESS)
		return status;

	status = doca_mmap_advise_cap_task_cache_invalidate_is_supported(devinfo);
	if (status != DOCA_SUCCESS)
		return status;

	return DOCA_SUCCESS;
}

/**
 * Opens a device that supports cache invalidate and DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t open_device(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Opening device");

	EXIT_ON_FAILURE(open_doca_device_with_capabilities(check_dev_capable, &state->device));

	return DOCA_SUCCESS;
}

/**
 * Create MMAP, initialize and start it.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_mmap(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Creating MMAP");

	EXIT_ON_FAILURE(doca_mmap_create(&state->mmap));
	EXIT_ON_FAILURE(doca_mmap_set_memrange(state->mmap, state->buffer, BUFFER_SIZE));
	EXIT_ON_FAILURE(doca_mmap_add_dev(state->mmap, state->device));
	EXIT_ON_FAILURE(doca_mmap_set_permissions(state->mmap, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE));
	EXIT_ON_FAILURE(doca_mmap_start(state->mmap));

	return DOCA_SUCCESS;
}

/**
 * Create buffer inventory, initialize and start it.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_buf_inventory(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Creating buf inventory");

	EXIT_ON_FAILURE(doca_buf_inventory_create(BUF_INVENTORY_SIZE, &state->inventory));
	EXIT_ON_FAILURE(doca_buf_inventory_start(state->inventory));

	return DOCA_SUCCESS;
}

/**
 * Creates a progress engine
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_pe(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Creating PE");

	EXIT_ON_FAILURE(doca_pe_create(&state->pe));

	return DOCA_SUCCESS;
}

/*
 * DMA Memcpy task completed callback
 *
 * @dma_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void dma_memcpy_completed_callback(struct doca_dma_task_memcpy *dma_task,
					  union doca_data task_user_data,
					  union doca_data ctx_user_data)
{
	struct cache_invalidate_sample_state *state = (struct cache_invalidate_sample_state *)ctx_user_data.ptr;

	(void)dma_task;
	(void)task_user_data;

	DOCA_LOG_INFO("DMA completed, Submitting cache invalidate task");
	(void)doca_task_submit(doca_mmap_advise_task_invalidate_cache_as_doca_task(state->cache_invalidate_task));
}

/*
 * Memcpy task error callback
 *
 * @dma_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void dma_memcpy_error_callback(struct doca_dma_task_memcpy *dma_task,
				      union doca_data task_user_data,
				      union doca_data ctx_user_data)
{
	struct cache_invalidate_sample_state *state = (struct cache_invalidate_sample_state *)ctx_user_data.ptr;
	struct doca_task *task = doca_dma_task_memcpy_as_task(dma_task);

	(void)task_user_data;

	DOCA_LOG_ERR("DMA Task failed with status %s", doca_error_get_descr(doca_task_get_status(task)));

	state->run_pe = false;
}

/**
 * Create DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_dma(struct cache_invalidate_sample_state *state)
{
	union doca_data ctx_user_data = {0};

	DOCA_LOG_INFO("Creating DMA");

	EXIT_ON_FAILURE(doca_dma_create(state->device, &state->dma));
	state->dma_ctx = doca_dma_as_ctx(state->dma);

	/* A context can only be connected to one PE (PE can run multiple contexts) */
	EXIT_ON_FAILURE(doca_pe_connect_ctx(state->pe, state->dma_ctx));

	/**
	 * The ctx user data is received in the task completion callback.
	 * Setting the state to the user data binds the program to the callback.
	 * See dma_memcpy_completed_callback for usage.
	 */
	ctx_user_data.ptr = state;
	EXIT_ON_FAILURE(doca_ctx_set_user_data(state->dma_ctx, ctx_user_data));

	EXIT_ON_FAILURE(doca_dma_task_memcpy_set_conf(state->dma,
						      dma_memcpy_completed_callback,
						      dma_memcpy_error_callback,
						      NUM_TASKS));

	return DOCA_SUCCESS;
}

/*
 * Cache invalidate task completed callback
 *
 * @cache_invalidate_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void cache_invalidate_completed_callback(struct doca_mmap_advise_task_invalidate_cache *cache_invalidate_task,
						union doca_data task_user_data,
						union doca_data ctx_user_data)
{
	struct cache_invalidate_sample_state *state = (struct cache_invalidate_sample_state *)ctx_user_data.ptr;

	(void)cache_invalidate_task;
	(void)task_user_data;

	DOCA_LOG_INFO("Cache invalidate completed");

	state->run_pe = false;
}

/*
 * Cache invalidate task error callback
 *
 * @cache_invalidate_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void cache_invalidate_error_callback(struct doca_mmap_advise_task_invalidate_cache *cache_invalidate_task,
					    union doca_data task_user_data,
					    union doca_data ctx_user_data)
{
	struct cache_invalidate_sample_state *state = (struct cache_invalidate_sample_state *)ctx_user_data.ptr;
	struct doca_task *task = doca_mmap_advise_task_invalidate_cache_as_doca_task(cache_invalidate_task);

	(void)task_user_data;

	DOCA_LOG_ERR("Cache invalidate Task failed with status %s", doca_error_get_descr(doca_task_get_status(task)));

	state->run_pe = false;
}

/**
 * Create MMAP advise
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_mmap_advise(struct cache_invalidate_sample_state *state)
{
	union doca_data ctx_user_data = {0};

	DOCA_LOG_INFO("Creating MMAP advise");

	EXIT_ON_FAILURE(doca_mmap_advise_create(state->device, &state->mmap_advise));
	state->mmap_advise_ctx = doca_mmap_advise_as_ctx(state->mmap_advise);

	/* A context can only be connected to one PE (PE can run multiple contexts) */
	EXIT_ON_FAILURE(doca_pe_connect_ctx(state->pe, state->mmap_advise_ctx));

	/**
	 * The ctx user data is received in the task completion callback.
	 * Setting the state to the user data binds the program to the callback.
	 * See cache_invalidate_completed_callback for usage.
	 */
	ctx_user_data.ptr = state;
	EXIT_ON_FAILURE(doca_ctx_set_user_data(state->mmap_advise_ctx, ctx_user_data));

	EXIT_ON_FAILURE(doca_mmap_advise_task_invalidate_cache_set_conf(state->mmap_advise,
									cache_invalidate_completed_callback,
									cache_invalidate_error_callback,
									NUM_TASKS));

	return DOCA_SUCCESS;
}

/**
 * This method allocate the DMA tasks but does not submit them.
 * This is a sample choice. A task can be submitted immediately after it is allocated.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t allocate_dma_task(struct cache_invalidate_sample_state *state)
{
	union doca_data user_data = {0};
	DOCA_LOG_INFO("Allocating DMA task");

	/* Use doca_buf_inventory_buf_get_by_data to initialize the source buffer */
	EXIT_ON_FAILURE(doca_buf_inventory_buf_get_by_data(state->inventory,
							   state->mmap,
							   state->buffer,
							   DMA_BUFFER_SIZE,
							   &state->dma_source));

	/**
	 * Using doca_buf_inventory_buf_get_by_addr leaves the buffer head uninitialized. The DMA context will
	 * set the head and length at the task completion.
	 */
	EXIT_ON_FAILURE(doca_buf_inventory_buf_get_by_addr(state->inventory,
							   state->mmap,
							   state->buffer + DMA_BUFFER_SIZE,
							   DMA_BUFFER_SIZE,
							   &state->dma_destination));

	EXIT_ON_FAILURE(doca_dma_task_memcpy_alloc_init(state->dma,
							state->dma_source,
							state->dma_destination,
							user_data,
							&state->dma_task));

	return DOCA_SUCCESS;
}

/**
 * This method allocate the DMA tasks but does not submit them.
 * This is a sample choice. A task can be submitted immediately after it is allocated.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t allocate_cache_invalidate_task(struct cache_invalidate_sample_state *state)
{
	union doca_data user_data = {0};
	DOCA_LOG_INFO("Allocating cache invalidate task");

	/* Using DMA source buffer */
	EXIT_ON_FAILURE(doca_mmap_advise_task_invalidate_cache_alloc_init(state->mmap_advise,
									  state->dma_source,
									  user_data,
									  &state->cache_invalidate_task));

	return DOCA_SUCCESS;
}

/**
 * This method submits the DMA task
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t submit_dma_task(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Submitting DMA task");

	EXIT_ON_FAILURE(doca_task_submit(doca_dma_task_memcpy_as_task(state->dma_task)));

	return DOCA_SUCCESS;
}

/**
 * Poll the PE until all tasks are completed.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t poll_for_completion(struct cache_invalidate_sample_state *state)
{
	DOCA_LOG_INFO("Polling until all tasks are completed");

	state->run_pe = true;

	/* This loop ticks the progress engine */
	while (state->run_pe == true) {
		/**
		 * doca_pe_progress shall return 1 if a task was completed and 0 if not. In this case the sample
		 * does not have anything to do with the return value because it is a polling sample.
		 */
		(void)doca_pe_progress(state->pe);
	}

	DOCA_LOG_INFO("All tasks are completed");

	return DOCA_SUCCESS;
}

/**
 * This method cleans up the sample resources in reverse order of their creation.
 * This method does not check for destroy return values for simplify.
 * Real code should check the return value and act accordingly (e.g. if doca_ctx_stop failed with DOCA_ERROR_IN_PROGRESS
 * it means that some contexts are still added or even that there are still in flight tasks in the progress engine).
 *
 * @state [in]: sample state
 */
static void cleanup(struct cache_invalidate_sample_state *state)
{
	if (state->dma_task != NULL)
		doca_task_free(doca_dma_task_memcpy_as_task(state->dma_task));

	if (state->cache_invalidate_task != NULL)
		doca_task_free(doca_mmap_advise_task_invalidate_cache_as_doca_task(state->cache_invalidate_task));

	/* A context must be stopped before it is destroyed */
	if (state->dma_ctx != NULL)
		(void)doca_ctx_stop(state->dma_ctx);

	if (state->mmap_advise_ctx != NULL)
		(void)doca_ctx_stop(state->mmap_advise_ctx);

	/* All contexts must be destroyed before PE is destroyed. Context destroy disconnects it from the PE */
	if (state->dma != NULL)
		(void)doca_dma_destroy(state->dma);

	if (state->mmap_advise != NULL)
		(void)doca_mmap_advise_destroy(state->mmap_advise);

	if (state->pe != NULL)
		(void)doca_pe_destroy(state->pe);

	if (state->dma_source != NULL)
		(void)doca_buf_dec_refcount(state->dma_source, NULL);

	if (state->dma_destination != NULL)
		(void)doca_buf_dec_refcount(state->dma_destination, NULL);

	if (state->inventory != NULL) {
		(void)doca_buf_inventory_stop(state->inventory);
		(void)doca_buf_inventory_destroy(state->inventory);
	}

	if (state->mmap != NULL) {
		(void)doca_mmap_stop(state->mmap);
		(void)doca_mmap_destroy(state->mmap);
	}

	if (state->device != NULL)
		(void)doca_dev_close(state->device);

	if (state->buffer != NULL)
		free(state->buffer);
}

/**
 * Run the sample
 * The method (and the method it calls) does not cleanup anything in case of failures.
 * It assumes that cleanup is called after it at any case.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t run(struct cache_invalidate_sample_state *state)
{
	memset(state, 0, sizeof(*state));

	EXIT_ON_FAILURE(allocate_buffer(state));
	EXIT_ON_FAILURE(open_device(state));
	EXIT_ON_FAILURE(create_mmap(state));
	EXIT_ON_FAILURE(create_buf_inventory(state));
	EXIT_ON_FAILURE(create_pe(state));
	EXIT_ON_FAILURE(create_dma(state));
	EXIT_ON_FAILURE(create_mmap_advise(state));
	EXIT_ON_FAILURE(doca_ctx_start(state->dma_ctx));
	EXIT_ON_FAILURE(doca_ctx_start(state->mmap_advise_ctx));
	EXIT_ON_FAILURE(allocate_dma_task(state));
	EXIT_ON_FAILURE(allocate_cache_invalidate_task(state));
	EXIT_ON_FAILURE(submit_dma_task(state));
	EXIT_ON_FAILURE(poll_for_completion(state));

	return DOCA_SUCCESS;
}

/**
 * Run the PE polling sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t run_cache_invalidate_sample(void)
{
	struct cache_invalidate_sample_state state;
	doca_error_t status = run(&state);

	cleanup(&state);

	return status;
}
