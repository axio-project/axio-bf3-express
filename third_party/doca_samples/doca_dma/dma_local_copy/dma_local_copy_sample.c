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

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dma.h>
#include <doca_error.h>
#include <doca_log.h>

#include "dma_common.h"

DOCA_LOG_REGISTER(DPU_LOCAL_DMA_COPY);

#define SLEEP_IN_NANOS (10 * 1000) /* Sample the job every 10 microseconds  */

/*
 * Checks that the two buffers are not overlap each other
 *
 * @dst_buffer [in]: Destination buffer
 * @src_buffer [in]: Source buffer
 * @length [in]: Length of both buffers
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
memory_ranges_overlap(const char *dst_buffer, const char *src_buffer, size_t length)
{
	const char *dst_range_end = dst_buffer + length;
	const char *src_range_end = src_buffer + length;

	if (((dst_buffer >= src_buffer) && (dst_buffer < src_range_end)) ||
	    ((src_buffer >= dst_buffer) && (src_buffer < dst_range_end))) {
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

/*
 * Register buffer with mmap and start it
 *
 * @mmap [in]: Memory Map object
 * @buffer [in]: Buffer
 * @length [in]: Buffer's size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
register_memory_range_and_start_mmap(struct doca_mmap *mmap, char *buffer, size_t length)
{
	doca_error_t result;

	result = doca_mmap_set_memrange(mmap, buffer, length);
	if (result != DOCA_SUCCESS)
		return result;

	return doca_mmap_start(mmap);
}

/*
 * Run DOCA DMA local copy sample
 *
 * @pcie_addr [in]: Device PCI address
 * @dst_buffer [in]: Destination buffer
 * @src_buffer [in]: Source buffer to copy
 * @length [in]: Buffer's size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
dma_local_copy(const char *pcie_addr, char *dst_buffer, char *src_buffer, size_t length)
{
	struct program_core_objects state = {0};
	struct doca_event event = {0};
	struct doca_dma_job_memcpy dma_job;
	struct doca_dma *dma_ctx;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	doca_error_t result;
	uint32_t max_bufs = 2;	/* Two buffers for source and destination */
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	if (dst_buffer == NULL || src_buffer == NULL || length == 0) {
		DOCA_LOG_ERR("Invalid input values, addresses and sizes must not be 0");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = memory_ranges_overlap(dst_buffer, src_buffer, length);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Memory ranges must not overlap");
		return result;
	}

	result = doca_dma_create(&dma_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create DMA engine: %s", doca_get_error_string(result));
		return result;
	}

	state.ctx = doca_dma_as_ctx(dma_ctx);

	result = open_doca_device_with_pci(pcie_addr, &dma_jobs_is_supported, &state.dev);
	if (result != DOCA_SUCCESS) {
		doca_dma_destroy(dma_ctx);
		return result;
	}

	result = init_core_objects(&state, WORKQ_DEPTH, max_bufs);
	if (result != DOCA_SUCCESS) {
		dma_cleanup(&state, dma_ctx);
		return result;
	}

	if (register_memory_range_and_start_mmap(state.dst_mmap, dst_buffer, length) != DOCA_SUCCESS ||
	    register_memory_range_and_start_mmap(state.src_mmap, src_buffer, length) != DOCA_SUCCESS) {
		dma_cleanup(&state, dma_ctx);
		return result;
	}

	/* Clear destination memory buffer */
	memset(dst_buffer, 0, length);

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.src_mmap, src_buffer, length, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
		dma_cleanup(&state, dma_ctx);
		return result;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.dst_mmap, dst_buffer, length, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(src_doca_buf, NULL);
		dma_cleanup(&state, dma_ctx);
		return result;
	}

	/* Construct DMA job */
	dma_job.base.type = DOCA_DMA_JOB_MEMCPY;
	dma_job.base.flags = DOCA_JOB_FLAGS_NONE;
	dma_job.base.ctx = state.ctx;
	dma_job.dst_buff = dst_doca_buf;
	dma_job.src_buff = src_doca_buf;

	/* Set data position in src_buff */
	result = doca_buf_set_data(src_doca_buf, src_buffer, length);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set data for DOCA buffer: %s", doca_get_error_string(result));
		return result;
	}

	/* Enqueue DMA job */
	result = doca_workq_submit(state.workq, &dma_job.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit DMA job: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		dma_cleanup(&state, dma_ctx);
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state.workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
		DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to retrieve DMA job: %s", doca_get_error_string(result));
		return result;
	}

	/* event result is valid */
	result = (doca_error_t)event.result.u64;
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DMA job event returned unsuccessfully: %s", doca_get_error_string(result));
		return result;
	}

	DOCA_LOG_INFO("Success, memory copied and verified as correct");

	if (doca_buf_refcount_rm(src_doca_buf, NULL) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease DOCA source buffer reference count");

	if (doca_buf_refcount_rm(dst_doca_buf, NULL) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease DOCA destination buffer reference count");

	/* Clean and destroy all relevant objects */
	dma_cleanup(&state, dma_ctx);

	return result;
}
