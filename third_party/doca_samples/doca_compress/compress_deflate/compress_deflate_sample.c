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
#include <doca_compress.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"

DOCA_LOG_REGISTER(COMPRESS_DEFLATE);

#define SLEEP_IN_NANOS (10 * 1000)		/* Sample the job every 10 microseconds  */
#define MAX_FILE_SIZE (128 * 1024 * 1024)	/* compress files up to 128MB */

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
 * @compress [in]: compress context
 */
static void
compress_cleanup(struct program_core_objects *state, struct doca_compress *compress)
{
	doca_error_t result;

	destroy_core_objects(state);

	result = doca_compress_destroy(compress);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy compress: %s", doca_get_error_string(result));
}

/**
 * Check if given device is capable of executing a DOCA_COMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_COMPRESS_DEFLATE_JOB and DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_compress_is_supported(struct doca_devinfo *devinfo)
{
	return doca_compress_job_get_supported(devinfo, DOCA_COMPRESS_DEFLATE_JOB);
}

/**
 * Check if given device is capable of executing a DOCA_DECOMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_DECOMPRESS_DEFLATE_JOB and DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_decompress_is_supported(struct doca_devinfo *devinfo)
{
	return doca_compress_job_get_supported(devinfo, DOCA_DECOMPRESS_DEFLATE_JOB);
}

/*
 * Run compress_deflate sample
 *
 * @pci_addr [in]: PCI address of a doca device
 * @file_data [in]: file data for the compress job
 * @file_size [in]: file size
 * @job_type [in]: job type - compress/decompress
 * @output_path [in]: path to the job output file
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
compress_deflate(const char *pci_addr, char *file_data, size_t file_size, enum doca_compress_job_types job_type, const char *output_path)
{
	struct program_core_objects state = {0};
	struct doca_event event = {0};
	struct doca_compress *compress;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	uint32_t workq_depth = 1;		/* The sample will run 1 compress job */
	uint32_t max_bufs = 2;			/* The sample will use 2 doca buffers */
	char *dst_buffer;
	uint8_t *resp_head;
	size_t data_len;
	doca_error_t result;
	size_t i;
	FILE *out_file = fopen(output_path, "wr");
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	if (out_file == NULL) {
		DOCA_LOG_ERR("Unable to open output file: %s", output_path);
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_compress_create(&compress);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create compress engine: %s", doca_get_error_string(result));
		fclose(out_file);
		return result;
	}

	state.ctx = doca_compress_as_ctx(compress);

	if (job_type == DOCA_COMPRESS_DEFLATE_JOB)
		result = open_doca_device_with_pci(pci_addr, &compress_jobs_compress_is_supported, &state.dev);
	else
		result = open_doca_device_with_pci(pci_addr, &compress_jobs_decompress_is_supported, &state.dev);

	if (result != DOCA_SUCCESS) {
		fclose(out_file);
		doca_compress_destroy(compress);
		return result;
	}

	result = init_core_objects(&state, workq_depth, max_bufs);
	if (result != DOCA_SUCCESS) {
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	dst_buffer = calloc(1, MAX_FILE_SIZE);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		fclose(out_file);
		compress_cleanup(&state, compress);
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state.dst_mmap, dst_buffer, MAX_FILE_SIZE);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}
	result = doca_mmap_set_free_cb(state.dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}
	result = doca_mmap_start(state.dst_mmap);
	if (result != DOCA_SUCCESS) {
		free(dst_buffer);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	result = doca_mmap_set_memrange(state.src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}
	result = doca_mmap_start(state.src_mmap);
	if (result != DOCA_SUCCESS) {
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state.buf_inv, state.dst_mmap, dst_buffer, MAX_FILE_SIZE, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(src_doca_buf, NULL);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	/* setting data length in doca buffer */
	result = doca_buf_set_data(src_doca_buf, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set DOCA buffer data: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(src_doca_buf, NULL);
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	/* Construct compress job */
	const struct doca_compress_deflate_job compress_job = {
		.base = (struct doca_job) {
			.type = job_type,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state.ctx,
			.user_data.u64 = job_type,
			},
		.dst_buff = dst_doca_buf,
		.src_buff = src_doca_buf,
	};

	/* Enqueue compress job */
	result = doca_workq_submit(state.workq, &compress_job.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit compress job: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		fclose(out_file);
		compress_cleanup(&state, compress);
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state.workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
		DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve compress job: %s", doca_get_error_string(result));

	else if (event.result.u64 != DOCA_SUCCESS)
		DOCA_LOG_ERR("Compress job finished unsuccessfully");

	else if (((int)(event.type) != (int)job_type) ||
		(event.user_data.u64 != job_type))
		DOCA_LOG_ERR("Received wrong event");

	else {
		/* write the result to output file */
		doca_buf_get_head(compress_job.dst_buff, (void **)&resp_head);
		doca_buf_get_data_len(compress_job.dst_buff, &data_len);
		for (i = 0; i < data_len; i++)
			fwrite(resp_head + i, sizeof(uint8_t), 1, out_file);
		DOCA_LOG_INFO("File was %s successfully and saved in: %s",
			      job_type == DOCA_COMPRESS_DEFLATE_JOB ? "compressed" : "decompressed", output_path);
	}

	if (doca_buf_refcount_rm(src_doca_buf, NULL) != DOCA_SUCCESS ||
	    doca_buf_refcount_rm(dst_doca_buf, NULL) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count");

	/* Clean and destroy all relevant objects */
	compress_cleanup(&state, compress);

	fclose(out_file);

	return result;
}
