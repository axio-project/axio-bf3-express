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
#include <stdbool.h>

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

DOCA_LOG_REGISTER(PE_ASYNC_STOP::SAMPLE);

/**
 * This sample demonstrates how to stop a context in the middle of a run while tasks are still submitted.
 * The sample uses DOCA_DMA context as an example (DOCA PE can run any library that abides to the PE context API).
 * The sample runs 16 DMA memcpy tasks and stops the DMA ctx after half of them were completed.
 * The sample registers to a state changed callback (doca_ctx_set_state_changed_cb) and prints state transitions.
 * Diff between this sample and pe_polling sample to see the differences for mitigating asynchronous stop.
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

#define NUM_TASKS (16)
#define DMA_BUFFER_SIZE (1024)
#define BUFFER_SIZE (DMA_BUFFER_SIZE * 2 * NUM_TASKS)
#define BUF_INVENTORY_SIZE (NUM_TASKS * 2)

/**
 * This struct defines the program context.
 */
struct pe_async_stop_sample_state {
	struct pe_sample_state_base base;

	struct doca_dma *dma;
	struct doca_ctx *dma_ctx;
	struct doca_dma_task_memcpy *tasks[NUM_TASKS];
	bool dma_has_stopped;
};

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
	struct pe_async_stop_sample_state *state = (struct pe_async_stop_sample_state *)ctx_user_data.ptr;

	state->base.num_completed_tasks++;

	DOCA_LOG_INFO("Task %p completed successfully", dma_task);

	/**
	 * process_completed_dma_memcpy_task returns doca_error_t to be able to use EXIT_ON_FAILURE, but there is
	 * nothing to do with the return value.
	 */
	(void)process_completed_dma_memcpy_task(dma_task, expected_value);

	/* The task is no longer required, therefore it can be freed */
	(void)dma_task_free(dma_task);

	/**
	 * The sample stops the context after half of the tasks are completed to simulate asynchronous stop.
	 * The return value is expected to be DOCA_ERROR_IN_PROGRESS which implies that the stopping process has begun
	 * but it can't be completed synchronously because one or more tasks are still submitted.
	 * dma_state_changed_cb shall be invoked (state changed from stopping to idle) when the context is fully
	 * stopped.
	 */
	if (state->base.num_completed_tasks == (NUM_TASKS / 2)) {
		doca_error_t status = doca_ctx_stop(state->dma_ctx);

		if (status != DOCA_ERROR_IN_PROGRESS)
			DOCA_LOG_ERR("Failed to stop DMA during run with status %s", doca_error_get_descr(status));
		else
			DOCA_LOG_INFO("Stopping DMA in the middle");
	}
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
	struct pe_async_stop_sample_state *state = (struct pe_async_stop_sample_state *)ctx_user_data.ptr;
	struct doca_task *task = doca_dma_task_memcpy_as_task(dma_task);

	(void)task_user_data;

	/* This sample defines that a task is completed even if it is completed with error */
	state->base.num_completed_tasks++;

	/* The program can also get the ctx state to know if it is in stopping state by using doca_ctx_get_state */
	if (state->base.num_completed_tasks < (NUM_TASKS / 2))
		DOCA_LOG_ERR("Task failed with status %s", doca_error_get_descr(doca_task_get_status(task)));
	else
		DOCA_LOG_INFO("Task flushed with status %s", doca_error_get_descr(doca_task_get_status(task)));

	/* The task is no longer required, therefore it can be freed */
	(void)dma_task_free(dma_task);

	/**
	 * DMA shall stop in case of error so no need to stop if (state->base.num_completed_tasks == (NUM_TASKS / 2)).
	 * Other libraries may define different behavior on error so keep that in mind when implementing
	 * the stop callback.
	 */
}

/**
 * Convert doca_ctx_states to string
 *
 * @state [in]: context state
 * @return: string representation of the state
 */
static const char *
ctx_state_to_string(enum doca_ctx_states state)
{
	switch (state) {
	case DOCA_CTX_STATE_IDLE:
		return "idle";
	case DOCA_CTX_STATE_STARTING:
		return "starting";
	case DOCA_CTX_STATE_RUNNING:
		return "running";
	case DOCA_CTX_STATE_STOPPING:
		return "stopping";
	default:
		return "unknown";
	}
}

/**
 * DMA state changed callback
 *
 * @ctx_user_data [in]: ctx user data
 * @ctx [in]: ctx
 * @prev_state [in]: previous ctx state
 * @next_state [in]: next ctx state
 */
static void
dma_state_changed_cb(const union doca_data ctx_user_data, struct doca_ctx *ctx, enum doca_ctx_states prev_state,
		     enum doca_ctx_states next_state)
{
	struct pe_async_stop_sample_state *state = (struct pe_async_stop_sample_state *)ctx_user_data.ptr;

	/**
	 * idle -> starting state is irrelevant because DMA start is synchronous.
	 * idle -> running is obvious so this callback shall ignore it.
	 * running -> stopping is expected because the sample shall request to stop after half of the tasks are done.
	 * stopping -> stopped is expected (implies that all in-flight tasks are flushed).
	 * The program can use this callback to raise a flag that breaks the progress loop or any other action that
	 * depends on state transition
	 */
	DOCA_LOG_INFO("CTX %p state changed from %s to %s", ctx, ctx_state_to_string(prev_state),
		      ctx_state_to_string(next_state));

	if ((prev_state == DOCA_CTX_STATE_STOPPING) && (next_state == DOCA_CTX_STATE_IDLE))
		state->dma_has_stopped = true;
}

/**
 * Create DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_dma(struct pe_async_stop_sample_state *state)
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

	EXIT_ON_FAILURE(doca_ctx_set_state_changed_cb(state->dma_ctx, dma_state_changed_cb));

	return DOCA_SUCCESS;
}

/**
 * This method polls the PE until DMA is stopped.
 * The DMA stop is asynchronous in this sample so after all tasks are completed the PE must be called once again to move
 * the DMA from stopping to idle.
 *
 * @state [in]: sample state
 */
void
poll_for_dma_stop(struct pe_async_stop_sample_state *state)
{
	while (!state->dma_has_stopped)
		(void)doca_pe_progress(state->base.pe);
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
cleanup(struct pe_async_stop_sample_state *state)
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
run(struct pe_async_stop_sample_state *state)
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
	EXIT_ON_FAILURE(allocate_dma_tasks(&state->base, state->dma, NUM_TASKS, DMA_BUFFER_SIZE, state->tasks));
	EXIT_ON_FAILURE(submit_dma_tasks(NUM_TASKS, state->tasks));
	EXIT_ON_FAILURE(poll_for_completion(&state->base, NUM_TASKS));
	poll_for_dma_stop(state);

	return DOCA_SUCCESS;
}

/**
 * Run the PE polling sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
run_pe_async_stop_sample(void)
{
	struct pe_async_stop_sample_state state;
	doca_error_t status = run(&state);

	cleanup(&state);

	return status;
}
