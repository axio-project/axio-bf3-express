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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <doca_mmap.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dma.h>
#include <doca_types.h>
#include <doca_log.h>
#include <doca_dma.h>
#include <doca_pe.h>

#include <samples/common.h>
#include "pe_common.h"

DOCA_LOG_REGISTER(PE_TASK_RESUBMIT::SAMPLE);

/**
 * This sample demonstrates how to resubmit a task.
 * Task resubmission may increase performance because it doesn't free and allocate tasks.
 * A reused task may contain the same resources (e.g. for DMA it can contain the same source and destination), contain
 * new resources or contain a mixture of resources. This sample releases the buffers of a completed task and uses a
 * new set of buffers for the resubmitted task but this is only to demonstrate how to do that.
 * The sample uses DOCA_DMA context as an example (DOCA PE can run any library that abides to the PE context API).
 * The sample uses 4 tasks and resubmits them until all source buffers are copied to destination buffers.
 * Diff between this sample and pe_polling sample to see the differences for using task resubmission.
 */

/**
 * This macro is used to minimize code size.
 * The macro runs an expression and returns error if the expression status is not DOCA_SUCCESS
 */
#define EXIT_ON_FAILURE(_expression_) { \
	doca_error_t _status_ = _expression_; \
	\
	if (_status_ != DOCA_SUCCESS) { \
		DOCA_LOG_ERR("%s failed with status %s", __func__, doca_error_get_descr(_status_)); \
		return _status_; \
	} \
}

#define NUM_TASKS			(4)
#define NUM_BUFFER_PAIRS		(NUM_TASKS * 4)
#define DMA_BUFFER_SIZE			(1024)
#define BUFFER_SIZE			(DMA_BUFFER_SIZE * 2 * NUM_BUFFER_PAIRS)
#define BUF_INVENTORY_SIZE		(NUM_BUFFER_PAIRS * 2)

/**
 * This struct defines the program context.
 */
struct pe_task_resubmit_sample_state {
	struct pe_sample_state_base base;

	struct doca_dma *dma;
	struct doca_ctx *dma_ctx;
	struct doca_dma_task_memcpy *tasks[NUM_TASKS];
	uint32_t buff_pair_index;
	struct doca_buf *src_buffers[NUM_BUFFER_PAIRS];
	struct doca_buf *dst_buffers[NUM_BUFFER_PAIRS];
};

/*
 * Resubmit task
 *
 * @details This function resubmits a task. The function sets a new set of buffers every time that it is called, assuming
 * that the old buffers were released.
 *
 * @state [in]: sample state
 * @dma_task [in]: task to resubmit
 */
void
dma_task_resubmit(struct pe_task_resubmit_sample_state *state, struct doca_dma_task_memcpy *dma_task)
{
	doca_error_t status = DOCA_SUCCESS;
	struct doca_task *task = doca_dma_task_memcpy_as_task(dma_task);

	if (state->buff_pair_index < NUM_BUFFER_PAIRS) {
		union doca_data user_data = {0};

		DOCA_LOG_INFO("Task %p resubmitting with buffers index %d", dma_task, state->buff_pair_index);

		/* Source buffer is filled with index + 1 that matches state->buff_pair_index + 1 */
		user_data.u64 = (state->buff_pair_index + 1);
		doca_task_set_user_data(task, user_data);

		doca_dma_task_memcpy_set_src(dma_task, state->src_buffers[state->buff_pair_index]);
		doca_dma_task_memcpy_set_dst(dma_task, state->dst_buffers[state->buff_pair_index]);
		state->buff_pair_index++;

		status = doca_task_submit(task);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit task with status %s",
				     doca_error_get_descr(doca_task_get_status(task)));

			/* Program owns a task if it failed to submit (and has to free it eventually) */
			(void)dma_task_free(dma_task);

			/* The method must increment num_completed_tasks because this task will never complete */
			state->base.num_completed_tasks++;
		}
	} else
		doca_task_free(task);
}

/*
 * DMA Memcpy task completed callback
 *
 * @dma_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
dma_memcpy_completed_callback(struct doca_dma_task_memcpy *dma_task, union doca_data task_user_data,
			      union doca_data ctx_user_data)
{
	uint8_t expected_value = (uint8_t)task_user_data.u64;
	struct pe_task_resubmit_sample_state *state = (struct pe_task_resubmit_sample_state *)ctx_user_data.ptr;

	state->base.num_completed_tasks++;

	/**
	 * process_completed_dma_memcpy_task returns doca_error_t to be able to use EXIT_ON_FAILURE, but there is nothing to do
	 * with the return value.
	 */
	(void)process_completed_dma_memcpy_task(dma_task, expected_value);

	(void)free_dma_memcpy_task_buffers(dma_task);

	dma_task_resubmit(state, dma_task);
}

/*
 * Memcpy task error callback
 *
 * @dma_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
dma_memcpy_error_callback(struct doca_dma_task_memcpy *dma_task, union doca_data task_user_data,
			  union doca_data ctx_user_data)
{
	struct pe_task_resubmit_sample_state *state = (struct pe_task_resubmit_sample_state *)ctx_user_data.ptr;
	struct doca_task *task = doca_dma_task_memcpy_as_task(dma_task);

	(void)task_user_data;

	/* This sample defines that a task is completed even if it is completed with error */
	state->base.num_completed_tasks++;

	DOCA_LOG_ERR("Task failed with status %s", doca_error_get_descr(doca_task_get_status(task)));

	(void)free_dma_memcpy_task_buffers(dma_task);

	dma_task_resubmit(state, dma_task);
}

/**
 * Create DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_dma(struct pe_task_resubmit_sample_state *state)
{
	union doca_data ctx_user_data = {0};

	DOCA_LOG_INFO("Creating DMA");

	EXIT_ON_FAILURE(doca_dma_create(state->base.device, &state->dma));
	state->dma_ctx = doca_dma_as_ctx(state->dma);

	/* A context can only be connected to one PE (PE can run multiple contexts) */
	EXIT_ON_FAILURE(doca_pe_connect_ctx(state->base.pe, state->dma_ctx));

	/**
	 * The ctx user data is received in the task completion callback.
	 * Setting the state to the user data binds the program to the callback.
	 * See dma_memcpy_completed_callback for usage.
	 */
	ctx_user_data.ptr = state;
	EXIT_ON_FAILURE(doca_ctx_set_user_data(state->dma_ctx, ctx_user_data));

	EXIT_ON_FAILURE(doca_dma_task_memcpy_set_conf(state->dma, dma_memcpy_completed_callback,
						      dma_memcpy_error_callback, NUM_TASKS));

	return DOCA_SUCCESS;
}

/**
 * This method allocate the source and destination buffers
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
allocate_doca_bufs(struct pe_task_resubmit_sample_state *state)
{
	uint32_t i = 0;

	DOCA_LOG_INFO("Allocating doca buffers");

	for (i = 0; i < NUM_BUFFER_PAIRS; i++) {
		/* Use doca_buf_inventory_buf_get_by_data to initialize the source buffer */
		EXIT_ON_FAILURE(doca_buf_inventory_buf_get_by_data(state->base.inventory, state->base.mmap,
								   state->base.available_buffer, DMA_BUFFER_SIZE,
								   &state->src_buffers[i]));

		memset(state->base.available_buffer, (i + 1), DMA_BUFFER_SIZE);
		state->base.available_buffer += DMA_BUFFER_SIZE;

		EXIT_ON_FAILURE(doca_buf_inventory_buf_get_by_addr(state->base.inventory, state->base.mmap,
								   state->base.available_buffer, DMA_BUFFER_SIZE,
								   &state->dst_buffers[i]));

		memset(state->base.available_buffer, 0, DMA_BUFFER_SIZE);
		state->base.available_buffer += DMA_BUFFER_SIZE;
	}

	return DOCA_SUCCESS;
}

/**
 * This method allocate the DMA tasks but does not submit them.
 * This is a sample choice. A task can be submitted immediately after it is allocated.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
allocate_tasks_for_resubmit(struct pe_task_resubmit_sample_state *state)
{
	uint32_t i = 0;

	DOCA_LOG_INFO("Allocating tasks");

	for (i = 0; i < NUM_TASKS; i++) {
		union doca_data user_data = {0};

		user_data.u64 = (state->buff_pair_index + 1);
		EXIT_ON_FAILURE(doca_dma_task_memcpy_alloc_init(state->dma, state->src_buffers[state->buff_pair_index],
								state->dst_buffers[state->buff_pair_index],
								user_data, &state->tasks[i]));

		DOCA_LOG_INFO("Task %p allocated with buffers index %d", state->tasks[i], state->buff_pair_index);

		state->buff_pair_index++;
	}

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
void
cleanup(struct pe_task_resubmit_sample_state *state)
{
	/* A context must be stopped before it is destroyed */
	if (state->dma_ctx != NULL)
		(void)doca_ctx_stop(state->dma_ctx);

	/* All contexts must be destroyed before PE is destroyed. Context destroy disconnects it from the PE */
	if (state->dma != NULL)
		(void)doca_dma_destroy(state->dma);

	pe_sample_base_cleanup(&state->base);
}

/**
 * Run the sample
 * The method (and the method it calls) does not cleanup anything in case of failures.
 * It assumes that cleanup is called after it at any case.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
run(struct pe_task_resubmit_sample_state *state)
{
	memset(state, 0, sizeof(*state));

	state->base.buffer_size = BUFFER_SIZE;
	state->base.buf_inventory_size = BUF_INVENTORY_SIZE;

	EXIT_ON_FAILURE(allocate_buffer(&state->base));
	EXIT_ON_FAILURE(open_device(&state->base));
	EXIT_ON_FAILURE(create_mmap(&state->base));
	EXIT_ON_FAILURE(create_buf_inventory(&state->base));
	EXIT_ON_FAILURE(create_pe(&state->base));
	EXIT_ON_FAILURE(create_dma(state));
	EXIT_ON_FAILURE(doca_ctx_start(state->dma_ctx));
	EXIT_ON_FAILURE(allocate_doca_bufs(state));
	EXIT_ON_FAILURE(allocate_tasks_for_resubmit(state));
	EXIT_ON_FAILURE(submit_dma_tasks(NUM_TASKS, state->tasks));
	EXIT_ON_FAILURE(poll_for_completion(&state->base, NUM_BUFFER_PAIRS));

	return DOCA_SUCCESS;
}

/**
 * Run the PE polling sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
run_pe_task_resubmit_sample(void)
{
	struct pe_task_resubmit_sample_state state;
	doca_error_t status = run(&state);

	cleanup(&state);

	return status;
}
