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

DOCA_LOG_REGISTER(SHA_CREATE);

#define SLEEP_IN_NANOS (10 * 1000) /* Sample the job every 10 microseconds  */

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
	doca_error_t result;

	destroy_core_objects(state);

	result = doca_sha_destroy(sha_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy sha: %s", doca_get_error_string(result));
}

/**
 * Check if given device is capable of executing a DOCA_SHA_JOB_SHA256 job with HW.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
job_sha_hardware_is_supported(struct doca_devinfo *devinfo)
{
	doca_error_t result;

	result = doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256);
	if (result != DOCA_SUCCESS)
		return result;
	return doca_sha_get_hardware_supported(devinfo);
}

/**
 * Check if given device is capable of executing a DOCA_SHA_JOB_SHA256 job with SW.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
job_sha_software_is_supported(struct doca_devinfo *devinfo)
{
	return doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256);
}

/*
 * Run sha_create sample
 *
 * @src_buffer [in]: source data for the SHA job
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
sha_create(char *src_buffer)
{
	struct program_core_objects state = {0};
	struct doca_event event = {0};
	struct doca_sha *sha_ctx;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	doca_error_t result;
	uint32_t workq_depth = 1;		/* The sample will run 1 sha job */
	uint32_t max_bufs = 2;			/* The sample will use 2 doca buffers */
	char *dst_buffer = NULL;
	int i;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* Engine outputs hex format. For char format output, we need double the length */
	char sha_output[DOCA_SHA256_BYTE_COUNT * 2 + 1] = {0};
	uint8_t *resp_head;

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

	dst_buffer = calloc(1, DOCA_SHA256_BYTE_COUNT);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		sha_cleanup(&state, sha_ctx);
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state.dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_set_free_cb(state.dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_start(state.dst_mmap);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_set_memrange(state.src_mmap, src_buffer, strlen(src_buffer));
	if (result != DOCA_SUCCESS) {
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	result = doca_mmap_start(state.src_mmap);
	if (result != DOCA_SUCCESS) {
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.src_mmap, src_buffer, strlen(src_buffer),
						&src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
		sha_cleanup(&state, sha_ctx);
		return result;
	}
	/* Set data address and length in the doca_buf */
	result = doca_buf_set_data(src_doca_buf, src_buffer, strlen(src_buffer));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure");
		doca_buf_refcount_rm(src_doca_buf, NULL);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT,
						&dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(src_doca_buf, NULL);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* Construct sha job */
	const struct doca_sha_job sha_job = {
		.base = (struct doca_job) {
			.type = DOCA_SHA_JOB_SHA256,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state.ctx,
			.user_data.u64 = DOCA_SHA_JOB_SHA256,
			},
		.resp_buf = dst_doca_buf,
		.req_buf = src_doca_buf,
		.flags = DOCA_SHA_JOB_FLAGS_NONE,
	};

	/* Enqueue sha job */
	result = doca_workq_submit(state.workq, &sha_job.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit sha job: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		sha_cleanup(&state, sha_ctx);
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state.workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve sha job: %s", doca_get_error_string(result));

	else if (event.result.u64 != DOCA_SUCCESS)
		DOCA_LOG_ERR("SHA job finished unsuccessfully");

	else if (((int)(event.type) != (int)DOCA_SHA_JOB_SHA256) ||
		(event.user_data.u64 != DOCA_SHA_JOB_SHA256))
		DOCA_LOG_ERR("Received wrong event");

	else {
		doca_buf_get_data(sha_job.resp_buf, (void **)&resp_head);
		for (i = 0; i < DOCA_SHA256_BYTE_COUNT; i++)
			snprintf(sha_output + (2 * i), 3, "%02x", resp_head[i]);
		DOCA_LOG_INFO("SHA256 output of %s is: %s", src_buffer, sha_output);
	}

	if (doca_buf_refcount_rm(src_doca_buf, NULL) != DOCA_SUCCESS ||
	    doca_buf_refcount_rm(dst_doca_buf, NULL) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count");

	/* Clean and destroy all relevant objects */
	sha_cleanup(&state, sha_ctx);

	return result;
}
