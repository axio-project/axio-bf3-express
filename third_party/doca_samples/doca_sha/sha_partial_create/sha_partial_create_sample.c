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

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_sha.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"

DOCA_LOG_REGISTER(SHA_PARTIAL_CREATE);

#define SLEEP_IN_NANOS (10 * 1000)	/* Sample the job every 10 microseconds  */
#define PARTIAL_SHA_LEN 64		/* Buffer length of first partial SHA jpb */

/*
 * Free callback - free doca_buf allocated pointer
 *
 * @addr [in]: Memory range pointer
 * @len [in]: Memory range length
 * @opaque [in]: An opaque pointer passed to iterator
 */
void
free_cb(void *addr, size_t len, void *opaque)
{
	(void)len;
	(void)opaque;

	free(addr);
}

/*
 * Clean all the sample resources
 *
 * @state [in]: program_core_objects struct
 * @sha_ctx [in]: SHA context
 */
static void
sha_cleanup(struct program_core_objects *state, struct doca_sha *sha_ctx)
{
	doca_error_t res;

	destroy_core_objects(state);

	res = doca_sha_destroy(sha_ctx);
	if (res != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy sha: %s", doca_get_error_string(res));
}

/**
 * Check if given device is capable of executing a DOCA_SHA_JOB_SHA256_PARTIAL job with HW.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
job_sha_hardware_is_supported(struct doca_devinfo *devinfo)
{
	doca_error_t result;

	result = doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256_PARTIAL);
	if (result != DOCA_SUCCESS)
		return result;
	return doca_sha_get_hardware_supported(devinfo);
}

/**
 * Check if given device is capable of executing a DOCA_SHA_JOB_SHA256_PARTIAL job with SW.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
job_sha_software_is_supported(struct doca_devinfo *devinfo)
{
	return doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256_PARTIAL);
}

/*
 * Clean all the sample resources
 *
 * @state [in]: program_core_objects struct
 * @session [in]: partial SHA session
 * @src_doca_buf [in]: source doca buffer
 * @dst_doca_buf [in]: destination doca buffer
 * @is_final [in]: true if this is the last segment to send and false otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
submit_sha_partial_job(struct program_core_objects *state, struct doca_sha_partial_session *session,
			struct doca_buf *src_doca_buf, struct doca_buf *dst_doca_buf, bool is_final)
{
	struct doca_event event = {0};
	uint64_t flags = DOCA_SHA_JOB_FLAGS_NONE;
	doca_error_t result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* send SHA_PARTIAL_FINAL to last partial job */
	if (is_final)
		flags = DOCA_SHA_JOB_FLAGS_SHA_PARTIAL_FINAL;

	/* Construct sha job */
	struct doca_sha_job sha_job = {
		.base = (struct doca_job) {
			.type = DOCA_SHA_JOB_SHA256_PARTIAL,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state->ctx,
			.user_data.u64 = DOCA_SHA_JOB_SHA256_PARTIAL,
			},
		.resp_buf = dst_doca_buf,
		.req_buf = src_doca_buf,
		.flags = flags,
	};

	struct doca_sha_partial_job sha_partial_job = {
		.sha_job = sha_job,
		.session = session,
	};

	/* Enqueue sha job */
	result = doca_workq_submit(state->workq, &sha_partial_job.sha_job.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit sha job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve sha job: %s", doca_get_error_string(result));
	else if (event.result.u64 != DOCA_SUCCESS)
		DOCA_LOG_ERR("SHA job finished unsuccessfully");

	else if (((int)(event.type) != (int)DOCA_SHA_JOB_SHA256_PARTIAL) ||
		(event.user_data.u64 != DOCA_SHA_JOB_SHA256_PARTIAL))
		DOCA_LOG_ERR("Received wrong event");

	return result;
}

/*
 * Run sha_partial_create sample
 *
 * @src_buffer [in]: source data for the SHA job
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
sha_partial_create(char *src_buffer)
{
	struct program_core_objects state = {0};
	struct doca_sha_partial_session *session;
	struct doca_sha *sha_ctx;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	doca_error_t result;
	uint32_t workq_depth = 1;		/* The sample will run 1 partial sha job at a time */
	uint32_t max_bufs = 2;			/* The sample will use 2 doca buffers */
	uint32_t total_jobs;			/* The sample input will be divided to 64 bytes segments */
	char *dst_buffer = NULL;
	size_t total_src_len;
	size_t src_len;
	char *buf_head;
	bool is_final = false;
	size_t i;

	/* Engine outputs hex format. For char format output, we need double the length */
	char sha_output[DOCA_SHA256_BYTE_COUNT * 2 + 1] = {0};

	result = doca_sha_create(&sha_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create sha engine: %s", doca_get_error_string(result));
		return result;
	}

	state.ctx = doca_sha_as_ctx(sha_ctx);

	result = open_doca_device_with_capabilities(&job_sha_hardware_is_supported, &state.dev);
	if (result != DOCA_SUCCESS) {
		result = open_doca_device_with_capabilities(&job_sha_software_is_supported, &state.dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to find device for SHA job");
			result = doca_sha_destroy(sha_ctx);
			return result;
		}
		DOCA_LOG_WARN("SHA engine is not enabled, using openssl instead");
	}

	result = init_core_objects(&state, workq_depth, max_bufs);
	if (result != DOCA_SUCCESS) {
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* create sha session for all partial sha jobs */
	result = doca_sha_partial_session_create(sha_ctx, state.workq, &session);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create sha partial session: %s", doca_get_error_string(result));
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	dst_buffer = calloc(1, DOCA_SHA256_BYTE_COUNT);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		sha_cleanup(&state, sha_ctx);
		return DOCA_ERROR_NO_MEMORY;
	}

	total_src_len = strlen(src_buffer);
	result = doca_mmap_set_memrange(state.src_mmap, src_buffer, total_src_len);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_start(state.src_mmap);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_set_memrange(state.dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_set_free_cb(state.dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_start(state.dst_mmap);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* Construct response DOCA buffer for all partial jobs */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT,
						&dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s", doca_get_error_string(result));
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	total_jobs = (total_src_len + PARTIAL_SHA_LEN - 1) / PARTIAL_SHA_LEN;

	for (i = 0; i < total_jobs; i++) {
		if (total_src_len > PARTIAL_SHA_LEN)
			src_len = PARTIAL_SHA_LEN;
		else
			src_len = total_src_len;

		/* Construct DOCA buffer for src partial sha */
		result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.src_mmap, src_buffer, src_len,
							&src_doca_buf);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
			sha_cleanup(&state, sha_ctx);
			return result;
		}
		/* Set data address and length in the doca_buf */
		result = doca_buf_set_data(src_doca_buf, src_buffer, src_len);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure");
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_buf_refcount_rm(src_doca_buf, NULL);
			doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
			sha_cleanup(&state, sha_ctx);
			return result;
		}

		if (i == total_jobs - 1)
			is_final = true;

		result = submit_sha_partial_job(&state, session, src_doca_buf, dst_doca_buf, is_final);
		if (result != DOCA_SUCCESS) {
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_buf_refcount_rm(src_doca_buf, NULL);
			doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
			sha_cleanup(&state, sha_ctx);
			return result;
		}

		src_buffer += src_len;
		total_src_len -= src_len;
		doca_buf_refcount_rm(src_doca_buf, NULL);
	}

	doca_buf_get_data(dst_doca_buf, (void **)&buf_head);
	for (i = 0; i < DOCA_SHA256_BYTE_COUNT; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", buf_head[i]);
	DOCA_LOG_INFO("SHA256 output is: %s", sha_output);

	/* Clean and destroy all relevant objects */
	doca_buf_refcount_rm(dst_doca_buf, NULL);
	doca_sha_partial_session_destroy(sha_ctx, state.workq, session);
	sha_cleanup(&state, sha_ctx);

	return DOCA_SUCCESS;
}
