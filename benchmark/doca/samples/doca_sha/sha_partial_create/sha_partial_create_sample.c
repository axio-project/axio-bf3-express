/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_mmap.h>
#include <doca_pe.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_sha.h>

#include "common.h"

DOCA_LOG_REGISTER(SHA_PARTIAL_CREATE);

#define SLEEP_IN_NANOS (10 * 1000)			 /* Sample the task every 10 microseconds  */
#define LOG_NUM_PARTIAL_SHA_TASKS (0)			 /* Log of SHA tasks number */
#define SHA_SAMPLE_ALGORITHM (DOCA_SHA_ALGORITHM_SHA256) /* doca_sha_algorithm for the sample */

struct sha_resources {
	struct program_core_objects state; /* Core objects that manage our "state" */
	struct doca_sha *sha_ctx;	   /* DOCA SHA context */
	struct doca_buf *src_doca_buf;	   /* Source buffer as a DOCA Buffer */
	void *src_buffer;		   /* Source buffer as a C pointer */
	size_t remaining_src_len;	   /* Remaining bytes in source buffer */
	uint32_t partial_block_size;	   /* SHA block size */
	doca_error_t result;		   /* Current DOCA Error result */
	bool run_pe_progress;		   /* Should we keep on progressing the PE? */
};

/*
 * Free callback - free doca_buf allocated pointer
 *
 * @addr [in]: Memory range pointer
 * @len [in]: Memory range length
 * @opaque [in]: An opaque pointer passed to iterator
 */
void free_cb(void *addr, size_t len, void *opaque)
{
	(void)len;
	(void)opaque;

	free(addr);
}

/*
 * Clean all the sample resources
 *
 * @resources [in]: sha_resources struct
 * @return: DOCA_SUCCESS if the device supports SHA hash task and DOCA_ERROR otherwise.
 */
static doca_error_t sha_cleanup(struct sha_resources *resources)
{
	struct program_core_objects *state = &resources->state;
	doca_error_t result = DOCA_SUCCESS, tmp_result;

	if (state->pe != NULL && state->ctx != NULL) {
		tmp_result = doca_ctx_stop(state->ctx);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_ERROR_PROPAGATE(result, tmp_result);
			DOCA_LOG_ERR("Failed to destroy DOCA SHA: %s", doca_error_get_descr(tmp_result));
		}

		state->ctx = NULL;
	}

	if (resources->sha_ctx != NULL) {
		tmp_result = doca_sha_destroy(resources->sha_ctx);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_ERROR_PROPAGATE(result, tmp_result);
			DOCA_LOG_ERR("Failed to destroy DOCA SHA: %s", doca_error_get_descr(tmp_result));
		}
	}

	tmp_result = destroy_core_objects(state);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_ERROR_PROPAGATE(result, tmp_result);
		DOCA_LOG_ERR("Failed to destroy DOCA SHA: %s", doca_error_get_descr(tmp_result));
	}

	return result;
}

/*
 * Prepare next block in partial task and then submit the task
 *
 * @resources [in]: sha_resources struct
 * @sha_partial_hash_task [in]: the allocated partial SHA task
 * @return: DOCA_SUCCESS if the device supports SHA hash task and DOCA_ERROR otherwise.
 */
static doca_error_t prepare_and_submit_partial_sha_hash_task(struct sha_resources *resources,
							     struct doca_sha_task_partial_hash *sha_partial_hash_task)
{
	struct doca_task *task;
	size_t src_len;
	doca_error_t result;

	if (resources->remaining_src_len > resources->partial_block_size)
		src_len = resources->partial_block_size;
	else
		src_len = resources->remaining_src_len;

	/* Set data address and length in the doca_buf */
	result = doca_buf_set_data(resources->src_doca_buf, resources->src_buffer, src_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set data for source buffer: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sha_task_partial_hash_set_src(sha_partial_hash_task, resources->src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set source buffer for SHA partial hash task: %s", doca_error_get_descr(result));
		return result;
	}

	/* If we got to final task then mark it as such */
	if (src_len == resources->remaining_src_len) {
		result = doca_sha_task_partial_hash_set_is_final_buf(sha_partial_hash_task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set final buffer for SHA partial hash task: %s",
				     doca_error_get_descr(result));
			return result;
		}
	}

	/* Move the src buffer to the next block and decrease the remaining source length */
	resources->src_buffer += src_len;
	resources->remaining_src_len -= src_len;

	task = doca_sha_task_partial_hash_as_task(sha_partial_hash_task);

	/* Submit SHA partial hash task */
	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit SHA partial hash task: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * SHA partial hash task completed callback
 *
 * @sha_partial_hash_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void sha_partial_hash_completed_callback(struct doca_sha_task_partial_hash *sha_partial_hash_task,
						union doca_data task_user_data,
						union doca_data ctx_user_data)
{
	struct sha_resources *resources = (struct sha_resources *)ctx_user_data.ptr;
	bool last_task_finished = resources->remaining_src_len == 0;

	(void)task_user_data.ptr;

	/* Assign success to the result */
	resources->result = DOCA_SUCCESS;
	DOCA_LOG_INFO("SHA hash task has completed successfully");

	/* If not last task prepare the next one */
	if (!last_task_finished)
		resources->result = prepare_and_submit_partial_sha_hash_task(resources, sha_partial_hash_task);

	/* Free task and stop context once all tasks are completed or entered error */
	if (last_task_finished || resources->result != DOCA_SUCCESS) {
		doca_task_free(doca_sha_task_partial_hash_as_task(sha_partial_hash_task));
		(void)doca_ctx_stop(resources->state.ctx);
	}
}

/*
 * SHA partial hash task error callback
 *
 * @sha_partial_hash_task [in]: Failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void sha_partial_hash_error_callback(struct doca_sha_task_partial_hash *sha_partial_hash_task,
					    union doca_data task_user_data,
					    union doca_data ctx_user_data)
{
	struct sha_resources *resources = (struct sha_resources *)ctx_user_data.ptr;
	struct doca_task *task = doca_sha_task_partial_hash_as_task(sha_partial_hash_task);

	(void)task_user_data.ptr;

	/* Get the result of the task */
	resources->result = doca_task_get_status(task);
	DOCA_LOG_ERR("SHA partial hash task failed: %s", doca_error_get_descr(resources->result));

	/* Free task */
	doca_task_free(task);
	/* Stop context once error encountered */
	(void)doca_ctx_stop(resources->state.ctx);
}

/*
 * Check if given device is capable of executing a SHA partial hash task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports SHA hash task and DOCA_ERROR otherwise.
 */
static doca_error_t sha_partial_hash_is_supported(struct doca_devinfo *devinfo)
{
	return doca_sha_cap_task_partial_hash_get_supported(devinfo, SHA_SAMPLE_ALGORITHM);
}

/*
 * Perform a series of SHA partial hash tasks, which includes allocating the task, submitting it with different slices
 * of the source buffer and waiting for its completions
 *
 * @resources [in]: sha_resources struct
 * @dst_doca_buf [out]: Destination buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t perform_partial_sha_hash_task(struct sha_resources *resources, struct doca_buf **dst_doca_buf)
{
	struct program_core_objects *state = &resources->state;
	struct doca_sha_task_partial_hash *sha_partial_hash_task = NULL;
	struct doca_task *task = NULL;
	union doca_data task_user_data = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	/* Construct DOCA buffer for the source SHA buffer */
	result = doca_buf_inventory_buf_get_by_data(state->buf_inv,
						    state->src_mmap,
						    resources->src_buffer,
						    resources->remaining_src_len,
						    &resources->src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Allocate and construct SHA partial hash task. We will reuse this task for submitting all partial hash task */
	result = doca_sha_task_partial_hash_alloc_init(resources->sha_ctx,
						       SHA_SAMPLE_ALGORITHM,
						       resources->src_doca_buf,
						       *dst_doca_buf,
						       task_user_data,
						       &sha_partial_hash_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate SHA partial hash task: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(resources->src_doca_buf, NULL);
		return result;
	}

	task = doca_sha_task_partial_hash_as_task(sha_partial_hash_task);
	if (task == NULL) {
		DOCA_LOG_ERR("Failed to get SHA partial hash task as DOCA task: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(resources->src_doca_buf, NULL);
		return result;
	}

	result = prepare_and_submit_partial_sha_hash_task(resources, sha_partial_hash_task);
	if (result != DOCA_SUCCESS) {
		doca_task_free(task);
		doca_buf_dec_refcount(resources->src_doca_buf, NULL);
		return result;
	}

	resources->run_pe_progress = true;

	/* Wait for the task to be completed and for the context to be stopped */
	while (resources->run_pe_progress) {
		if (doca_pe_progress(state->pe) == 0)
			nanosleep(&ts, &ts);
	}

	doca_buf_dec_refcount(resources->src_doca_buf, NULL);

	/* Propagate result of task according to the result we update in the callbacks */
	DOCA_ERROR_PROPAGATE(result, resources->result);

	return result;
}

/**
 * Callback triggered whenever SHA context state changes
 *
 * @user_data [in]: User data associated with the SHA context. Will hold struct sha_resources *
 * @ctx [in]: The SHA context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void sha_state_changed_callback(const union doca_data user_data,
				       struct doca_ctx *ctx,
				       enum doca_ctx_states prev_state,
				       enum doca_ctx_states next_state)
{
	(void)ctx;
	(void)prev_state;

	struct sha_resources *resources = (struct sha_resources *)user_data.ptr;

	switch (next_state) {
	case DOCA_CTX_STATE_IDLE:
		DOCA_LOG_INFO("SHA context has been stopped");
		/* We can stop progressing the PE */
		resources->run_pe_progress = false;
		break;
	case DOCA_CTX_STATE_STARTING:
		/**
		 * The context is in starting state, this is unexpected for SHA.
		 */
		DOCA_LOG_ERR("SHA context entered into starting state. Unexpected transition");
		break;
	case DOCA_CTX_STATE_RUNNING:
		DOCA_LOG_INFO("SHA context is running");
		break;
	case DOCA_CTX_STATE_STOPPING:
		/**
		 * doca_ctx_stop() has been called.
		 * In this sample, this happens either due to a failure encountered, in which case doca_pe_progress()
		 * will cause any inflight task to be flushed, or due to the successful compilation of the sample flow.
		 * In both cases, in this sample, doca_pe_progress() will eventually transition the context to idle
		 * state.
		 */
		DOCA_LOG_INFO("SHA context entered into stopping state. Any inflight tasks will be flushed");
		break;
	default:
		break;
	}
}

/*
 * Run sha_partial_create sample
 *
 * @src_buffer [in]: source data for the SHA task
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t sha_partial_create(char *src_buffer)
{
	struct sha_resources resources;
	struct program_core_objects *state = &resources.state;
	union doca_data ctx_user_data = {0};
	struct doca_buf *dst_doca_buf = NULL;
	uint8_t *dst_buffer = NULL;
	uint8_t *dst_buffer_data = NULL;
	char *sha_output = NULL;
	uint32_t max_bufs = 2; /* The sample will use 2 DOCA buffers */
	uint32_t min_dst_sha_buffer_size, i;
	size_t hash_length;
	doca_error_t result;

	memset(&resources, 0, sizeof(resources));
	resources.src_buffer = src_buffer;
	resources.remaining_src_len = strlen(src_buffer);

	result = open_doca_device_with_capabilities(&sha_partial_hash_is_supported, &state->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to open DOCA device for SHA partial hash task: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sha_cap_get_min_dst_buf_size(doca_dev_as_devinfo(state->dev),
						   SHA_SAMPLE_ALGORITHM,
						   &min_dst_sha_buffer_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get minimum destination buffer size for DOCA SHA: %s",
			     doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	result = doca_sha_cap_get_partial_hash_block_size(doca_dev_as_devinfo(state->dev),
							  SHA_SAMPLE_ALGORITHM,
							  &resources.partial_block_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the partial hash block size for DOCA SHA: %s",
			     doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	if (resources.remaining_src_len <= resources.partial_block_size) {
		DOCA_LOG_ERR("User data length %lu should be bigger than one partial hash block size %u",
			     resources.remaining_src_len,
			     resources.partial_block_size);
		sha_cleanup(&resources);
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = doca_sha_create(state->dev, &resources.sha_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create sha engine: %s", doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	state->ctx = doca_sha_as_ctx(resources.sha_ctx);

	result = create_core_objects(state, max_bufs);
	if (result != DOCA_SUCCESS) {
		sha_cleanup(&resources);
		return result;
	}

	/* Connect context to progress engine */
	result = doca_pe_connect_ctx(state->pe, state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect progress engine to context: %s", doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	result = doca_sha_task_partial_hash_set_conf(resources.sha_ctx,
						     sha_partial_hash_completed_callback,
						     sha_partial_hash_error_callback,
						     LOG_NUM_PARTIAL_SHA_TASKS);
	if (result != DOCA_SUCCESS) {
		sha_cleanup(&resources);
		return result;
	}

	result = doca_ctx_set_state_changed_cb(state->ctx, sha_state_changed_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set SHA state change callback: %s", doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	dst_buffer = calloc(1, min_dst_sha_buffer_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		sha_cleanup(&resources);
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state->src_mmap, resources.src_buffer, resources.remaining_src_len);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, min_dst_sha_buffer_size);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_mmap_set_free_cb(state->dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&resources);
		return result;
	}

	/* Construct response DOCA buffer for all partial tasks */
	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv,
						    state->dst_mmap,
						    dst_buffer,
						    min_dst_sha_buffer_size,
						    &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_error_get_descr(result));
		sha_cleanup(&resources);
		return result;
	}

	/* Include tasks counter in user data of context to be decremented in callbacks */
	ctx_user_data.ptr = &resources;
	doca_ctx_set_user_data(state->ctx, ctx_user_data);

	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		sha_cleanup(&resources);
		return result;
	}

	/* Perform partial hashing */
	result = perform_partial_sha_hash_task(&resources, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to perform partial SHA hash tasks: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_buf_get_data_len(dst_doca_buf, &hash_length);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data length of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		sha_cleanup(&resources);
		return result;
	}

	result = doca_buf_get_data(dst_doca_buf, (void **)&dst_buffer_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		sha_cleanup(&resources);
		return result;
	}

	/* Engine outputs hex format. For char format output, we need double the length */
	sha_output = calloc(1, (hash_length * 2) + 1);
	if (sha_output == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		sha_cleanup(&resources);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Convert the hex output to string */
	for (i = 0; i < hash_length; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", dst_buffer_data[i]);
	DOCA_LOG_INFO("SHA256 output is: %s", sha_output);

	/* Clean and destroy all relevant objects */
	free(sha_output);
	doca_buf_dec_refcount(dst_doca_buf, NULL);
	sha_cleanup(&resources);

	return DOCA_SUCCESS;
}
