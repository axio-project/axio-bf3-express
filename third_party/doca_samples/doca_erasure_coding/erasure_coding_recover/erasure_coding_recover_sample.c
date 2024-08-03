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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_erasure_coding.h>
#include <doca_error.h>
#include <doca_log.h>
#include <utils.h>

#include "common.h"

DOCA_LOG_REGISTER(EC_RECOVER);

#define SLEEP_IN_NANOS (10 * 1000) /* sample the job every 10 microseconds  */
/* assert function - if failes print error, clean up(state - ec_sample_objects) and exit  */
#define ASSERT(condition, result, error...) \
	do { \
		if (!(condition)) { \
			DOCA_LOG_ERR(error); \
			ec_cleanup(state); \
			return result; \
		} \
	} while (0)
/* assert function - same as before just for doca error  */
#define ASSERT_DOCA_ERR(result, error) \
	ASSERT(result == DOCA_SUCCESS, result, error ": %s", doca_get_error_string(result))

#define USER_MAX_PATH_NAME 255		       /* max file name length */
#define MAX_PATH_NAME (USER_MAX_PATH_NAME + 1) /* max file name string length */
#define RECOVERED_FILE_NAME "_recovered"       /* recovered file extension (if file name not given) */
#define DATA_INFO_FILE_NAME "data_info"	       /* data information file name - i.e. size & name of original file */
#define DATA_BLOCK_FILE_NAME "data_block_"     /* data blocks file name (attached index at the end) */
#define RDNC_BLOCK_FILE_NAME "rdnc_block_"     /* redudancy blocks file name (attached index at the end) */

struct ec_sample_objects {
	struct doca_buf *src_doca_buf;		/* source doca buffer as input for the job */
	struct doca_buf *dst_doca_buf;		/* destination doca buffer as input for the job */
	struct doca_ec *ec;			/* DOCA Erasure coding context */
	char *src_buffer;			/* source buffer (will be in doca buffer) as input for the job */
	char *dst_buffer;			/* destination buffer (will be in doca buffer) as input for the job */
	char *file_data;			/* block data pointer from reading block file */
	char *block_file_data;			/* block data pointer from reading block file */
	uint32_t *missing_indices;		/* data indices to that are missing and need recover */
	FILE *out_file;				/* recovered file pointer to write to */
	FILE *block_file;			/* block file pointer to write to */
	struct doca_matrix *encoding_matrix;	/* encoding matrix that will be use to create the redundacy */
	struct doca_matrix *decoding_matrix;	/* decoding matrix that will be use to recover the data */
	struct program_core_objects core_state; /* DOCA core objects - please refer to struct program_core_objects */
};

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
 * @state [in]: ec_sample_objects struct
 * @ec [in]: ec context
 */
static void
ec_cleanup(struct ec_sample_objects *state)
{
	doca_error_t result = DOCA_SUCCESS;

	if (state->src_doca_buf != NULL) {
		result = doca_buf_refcount_rm(state->src_doca_buf, NULL);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count: %s",
				     doca_get_error_string(result));
	}
	if (state->dst_doca_buf != NULL) {
		result = doca_buf_refcount_rm(state->dst_doca_buf, NULL);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to decrease DOCA buffer reference count: %s",
				     doca_get_error_string(result));
	}
	if (state->missing_indices != NULL)
		free(state->missing_indices);
	if (state->block_file_data != NULL)
		free(state->block_file_data);
	if (state->file_data != NULL)
		free(state->file_data);
	if (state->src_buffer != NULL)
		free(state->src_buffer);
	if (state->dst_buffer != NULL && state->core_state.dst_mmap == NULL)
		free(state->dst_buffer);
	if (state->out_file != NULL)
		fclose(state->out_file);
	if (state->block_file != NULL)
		fclose(state->block_file);
	if (state->encoding_matrix != NULL) {
		result = doca_ec_matrix_destroy(state->encoding_matrix);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy ec encoding matrix: %s", doca_get_error_string(result));
	}
	if (state->decoding_matrix != NULL) {
		result = doca_ec_matrix_destroy(state->decoding_matrix);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy ec decoding matrix: %s", doca_get_error_string(result));
	}
	destroy_core_objects(&state->core_state);
	if (state->ec != NULL) {
		result = doca_ec_destroy(state->ec);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy ec: %s", doca_get_error_string(result));
	}
}

/**
 * Check if given device is capable of executing a DOCA_EC_JOB_CREATE.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_EC_JOB_CREATE and DOCA_ERROR otherwise.
 */
static doca_error_t
ec_jobs_create_is_supported(struct doca_devinfo *devinfo)
{
	return doca_ec_job_get_supported(devinfo, DOCA_EC_JOB_CREATE);
}

/**
 * Check if given device is capable of executing a DOCA_EC_JOB_RECOVER.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_EC_JOB_RECOVER and DOCA_ERROR otherwise.
 */
static doca_error_t
ec_jobs_recover_is_supported(struct doca_devinfo *devinfo)
{
	return doca_ec_job_get_supported(devinfo, DOCA_EC_JOB_RECOVER);
}

/**
 * Init ec core objects.
 *
 * @state [in]: The DOCA EC sample state
 * @pci_addr [in]: The PCI address of a doca device
 * @is_support_func [in]: Function that pci device should support
 * @workq_depth [in]: The DOCA EC sample state
 * @max_bufs [in]: The buffer count to create
 * @src_size [in]: The source data size (to create the buffer)
 * @dst_size [in]: The destination data size (to create the buffer)
 * @return: DOCA_SUCCESS if the core init suucessfuly and DOCA_ERROR otherwise.
 */
static doca_error_t
ec_core_init(struct ec_sample_objects *state, const char *pci_addr, jobs_check is_support_func,
	     uint32_t workq_depth, uint32_t max_bufs, uint32_t src_size, uint32_t dst_size)
{
	doca_error_t result = doca_ec_create(&state->ec);

	ASSERT_DOCA_ERR(result, "Unable to create ec engine");

	state->core_state.ctx = doca_ec_as_ctx(state->ec);

	result = open_doca_device_with_pci(pci_addr, is_support_func, &state->core_state.dev);
	ASSERT_DOCA_ERR(result, "Unable to open the pci device");

	result = init_core_objects(&state->core_state, workq_depth, max_bufs);
	ASSERT_DOCA_ERR(result, "Failed to init core");

	result = doca_mmap_set_memrange(state->core_state.dst_mmap, state->dst_buffer, dst_size);
	ASSERT_DOCA_ERR(result, "Failed to set mmap mem range dst");

	result = doca_mmap_set_free_cb(state->core_state.dst_mmap, &free_cb, NULL);
	ASSERT_DOCA_ERR(result, "Failed to set mmap free cb dst");

	result = doca_mmap_start(state->core_state.dst_mmap);
	ASSERT_DOCA_ERR(result, "Failed to start mmap dst");

	result = doca_mmap_set_memrange(state->core_state.src_mmap, state->src_buffer, src_size);
	ASSERT_DOCA_ERR(result, "Failed to set mmap mem range src");

	result = doca_mmap_start(state->core_state.src_mmap);
	ASSERT_DOCA_ERR(result, "Failed to start mmap src");

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state->core_state.buf_inv, state->core_state.src_mmap,
						state->src_buffer, src_size, &state->src_doca_buf);
	ASSERT_DOCA_ERR(result, "Unable to acquire DOCA buffer representing source buffer");

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_by_addr(state->core_state.buf_inv, state->core_state.dst_mmap,
						state->dst_buffer, dst_size, &state->dst_doca_buf);
	ASSERT_DOCA_ERR(result, "Unable to acquire DOCA buffer representing destination buffer");

	/* setting data length in doca buffer */
	result = doca_buf_set_data(state->src_doca_buf, state->src_buffer, src_size);
	ASSERT_DOCA_ERR(result, "Unable to set DOCA buffer data");

	return DOCA_SUCCESS;
}

/*
 * Run ec encode
 *
 * @pci_addr [in]: PCI address of a doca device
 * @file_path [in]: file data for the ec job
 * @matrix_type [in]: matrix type
 * @output_dir_path [in]: path to the job output file
 * @data_block_count [in]: data block count
 * @rdnc_block_count [in]: redudancy block count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
ec_encode(const char *pci_addr, const char *file_path, enum doca_ec_matrix_types matrix_type,
	  const char *output_dir_path, uint32_t data_block_count, uint32_t rdnc_block_count)
{
	struct doca_event event = {0};
	uint32_t workq_depth = 1;
	uint32_t max_bufs = 2;
	uint8_t *resp_head;
	doca_error_t result;
	int ret;
	size_t i;
	size_t file_size;
	uint32_t block_size;
	uint32_t src_size;
	uint32_t dst_size;
	char file_size_str[100];
	struct ec_sample_objects state_object = {0};
	struct ec_sample_objects *state = &state_object;
	char full_path[MAX_PATH_NAME];
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	result = read_file(file_path, &state->file_data, &file_size);
	ASSERT_DOCA_ERR(result, "Can't read input file");

	block_size = file_size / data_block_count;
	if (block_size % 64 != 0)
		block_size += 64 - (block_size % 64);
	src_size = block_size * data_block_count;
	dst_size = block_size * rdnc_block_count;

	state->src_buffer = malloc(src_size);
	ASSERT(state->src_buffer != NULL, DOCA_ERROR_NO_MEMORY, "Unable to allocate src_buffer string");
	memcpy(state->src_buffer, state->file_data, file_size);

	state->dst_buffer = malloc(dst_size);
	ASSERT(state->dst_buffer != NULL, DOCA_ERROR_NO_MEMORY, "Unable to allocate dst_buffer string");

	for (i = 0; i < data_block_count; i++) {
		ret = sprintf(full_path, "%s/%s%ld", output_dir_path, DATA_BLOCK_FILE_NAME, i);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
		state->block_file = fopen(full_path, "wr");
		ASSERT(state->block_file != NULL, DOCA_ERROR_IO_FAILED, "Unable to open output file: %s", full_path);
		ret = fwrite(state->src_buffer + i * block_size, sizeof(uint8_t), block_size, state->block_file);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
		fclose(state->block_file);
		state->block_file = NULL;
	}

	ret = sprintf(full_path, "%s/%s", output_dir_path, DATA_INFO_FILE_NAME);
	ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
	state->block_file = fopen(full_path, "wr");
	ASSERT(state->block_file != NULL, DOCA_ERROR_IO_FAILED, "Unable to open output file: %s", full_path);
	ret = sprintf(file_size_str, "%ld ", file_size);
	ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
	ret = fwrite(file_size_str, sizeof(uint8_t), strnlen(file_size_str, 100), state->block_file);
	ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
	ret = fwrite(file_path, 1, strlen(file_path), state->block_file);
	ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
	fclose(state->block_file);
	state->block_file = NULL;

	result = ec_core_init(state, pci_addr, &ec_jobs_create_is_supported, workq_depth, max_bufs, src_size, dst_size);
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_ec_matrix_create(state->ec, matrix_type, data_block_count, rdnc_block_count,
				       &state->encoding_matrix);
	ASSERT_DOCA_ERR(result, "Unable to create ec matrix");

	/* Construct ec job */
	const struct doca_ec_job_create ec_job = {.base = (struct doca_job){.type = DOCA_EC_JOB_CREATE,
									    .flags = DOCA_JOB_FLAGS_NONE,
									    .ctx = state->core_state.ctx,
									    .user_data.u64 = DOCA_EC_JOB_CREATE},
						  .dst_rdnc_buff = state->dst_doca_buf,
						  .src_original_data_buff = state->src_doca_buf,
						  .create_matrix = state->encoding_matrix};

	/* Enqueue ec job */
	result = doca_workq_submit(state->core_state.workq, &ec_job.base);
	ASSERT_DOCA_ERR(result, "Failed to submit ec job");

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->core_state.workq, &event,
						      DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) == DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	ASSERT_DOCA_ERR(result, "Failed to retrieve ec job");
	ASSERT_DOCA_ERR(event.result.u64, "EC job finished unsuccessfully");
	ASSERT(((int)(event.type) == (int)DOCA_EC_JOB_CREATE) && (event.user_data.u64 == DOCA_EC_JOB_CREATE),
	       DOCA_ERROR_UNEXPECTED, "Received wrong event");

	doca_buf_get_head(ec_job.dst_rdnc_buff, (void **)&resp_head);
	for (i = 0; i < rdnc_block_count; i++) {
		ret = sprintf(full_path, "%s/%s%ld", output_dir_path, RDNC_BLOCK_FILE_NAME, i);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
		state->block_file = fopen(full_path, "wr");
		ASSERT(state->block_file != NULL, DOCA_ERROR_IO_FAILED, "Unable to open output file: %s", full_path);
		ret = fwrite(resp_head + i * block_size, sizeof(uint8_t), block_size, state->block_file);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
		fclose(state->block_file);
		state->block_file = NULL;
	}
	DOCA_LOG_INFO("File was encoded successfully and saved in: %s", output_dir_path);

	/* Clean and destroy all relevant objects */
	ec_cleanup(state);

	return result;
}

/*
 * Run ec decode
 *
 * @pci_addr [in]: PCI address of a doca device
 * @matrix_type [in]: matrix type
 * @user_output_file_path [in]: path to the job output file
 * @dir_path [in]: path to the job output file
 * @data_block_count [in]: data block count
 * @rdnc_block_count [in]: redudancy block count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
ec_decode(const char *pci_addr, enum doca_ec_matrix_types matrix_type, const char *user_output_file_path,
	  const char *dir_path, uint32_t data_block_count, uint32_t rdnc_block_count)
{
	struct doca_event event = {0};
	uint32_t workq_depth = 1;
	uint32_t max_bufs = 2;
	uint8_t *resp_head;
	doca_error_t result;
	int ret;
	size_t i;
	size_t block_file_size;
	int32_t block_size = -1;
	uint32_t src_size = -1;
	uint32_t src_size_cur = 0;
	uint32_t dst_size;
	struct ec_sample_objects state_object = {0};
	struct ec_sample_objects *state = &state_object;
	size_t n_missing = 0;
	char *end;
	int64_t file_size;
	char output_file_path[MAX_PATH_NAME];
	char full_path[MAX_PATH_NAME];
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	ret = sprintf(full_path, "%s/%s", dir_path, DATA_INFO_FILE_NAME);
	ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
	result = read_file(full_path, &state->block_file_data, &block_file_size);
	ASSERT_DOCA_ERR(result, "Unable to open data file");
	ASSERT(block_file_size > 0, DOCA_ERROR_INVALID_VALUE, "File data info size is empty");
	file_size = strtol(state->block_file_data, &end, 10);
	ASSERT(file_size > 0, DOCA_ERROR_INVALID_VALUE, "File size from data info file none positive");

	if (user_output_file_path != NULL) {
		ASSERT(strnlen(user_output_file_path, MAX_PATH_NAME) <= MAX_PATH_NAME, DOCA_ERROR_INVALID_VALUE,
		       "Path excceded max path len");
		strcpy(output_file_path, user_output_file_path);
	} else {
		ret = sprintf(output_file_path, "%s", end + 1);
		ret = sprintf(output_file_path + block_file_size - (end + 1 - state->block_file_data), "%s", RECOVERED_FILE_NAME);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
	}

	free(state->block_file_data);
	state->block_file_data = NULL;

	state->out_file = fopen(output_file_path, "wr");
	ASSERT(state->out_file != NULL, DOCA_ERROR_IO_FAILED, "Unable to open output file: %s", output_file_path);

	state->missing_indices = calloc(data_block_count + rdnc_block_count, sizeof(uint32_t));
	ASSERT(state->missing_indices != NULL, DOCA_ERROR_NO_MEMORY, "Unable to allocate missing_indices");

	for (i = 0; i < data_block_count + rdnc_block_count; i++) {
		char *file_name = i < data_block_count ? DATA_BLOCK_FILE_NAME : RDNC_BLOCK_FILE_NAME;
		size_t index = i < data_block_count ? i : i - data_block_count;

		ret = sprintf(full_path, "%s/%s%ld", dir_path, file_name, index);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
		result = read_file(full_path, &state->block_file_data, &block_file_size);
		if (result == DOCA_SUCCESS && block_file_size > 0 && block_size < 0) {
			block_size = block_file_size;
			src_size = block_size * data_block_count;
			state->src_buffer = malloc(src_size);
			ASSERT(state->src_buffer != NULL, DOCA_ERROR_NO_MEMORY, "Unable to allocate src_buffer string");
			ASSERT(block_size % 64 == 0, DOCA_ERROR_INVALID_VALUE, "Block size is not 64 byte aligned");
		}
		if (result == DOCA_SUCCESS) {
			ASSERT((int32_t)block_file_size == block_size, DOCA_ERROR_INVALID_VALUE,
			       "Blocks are not same size");
			DOCA_LOG_INFO("Copy: %s", full_path);
			memcpy(state->src_buffer + src_size_cur, state->block_file_data, block_size);
			src_size_cur += block_size;
			free(state->block_file_data);
			state->block_file_data = NULL;
		} else
			state->missing_indices[n_missing++] = i;
		if (src_size_cur == src_size)
			break;
	}

	ASSERT(src_size_cur == src_size, DOCA_ERROR_INVALID_VALUE, "Not enough data for recover");
	ASSERT(n_missing > 0, DOCA_ERROR_INVALID_VALUE, "Nothing to decode, all original data block are in place");
	dst_size = block_size * n_missing;

	state->dst_buffer = malloc(dst_size);
	ASSERT(state->dst_buffer != NULL, DOCA_ERROR_NO_MEMORY, "Unable to allocate dst_buffer string");

	result = ec_core_init(state, pci_addr, &ec_jobs_recover_is_supported, workq_depth, max_bufs, src_size, dst_size);
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_ec_matrix_create(state->ec, matrix_type, data_block_count, rdnc_block_count,
				       &state->encoding_matrix);
	ASSERT_DOCA_ERR(result, "Unable to create ec matrix");

	result = doca_ec_recover_matrix_create(state->encoding_matrix, state->ec, state->missing_indices, n_missing,
					       &state->decoding_matrix);
	ASSERT_DOCA_ERR(result, "Unable to create recovery matrix");

	/* Construct ec job */
	const struct doca_ec_job_recover ec_job = {.base = (struct doca_job){.type = DOCA_EC_JOB_RECOVER,
									     .flags = DOCA_JOB_FLAGS_NONE,
									     .ctx = state->core_state.ctx,
									     .user_data.u64 = DOCA_EC_JOB_RECOVER},
						   .dst_recovered_data_buff = state->dst_doca_buf,
						   .src_remaining_data_buff = state->src_doca_buf,
						   .recover_matrix = state->decoding_matrix};

	/* Enqueue ec job */
	result = doca_workq_submit(state->core_state.workq, &ec_job.base);
	ASSERT_DOCA_ERR(result, "Failed to submit ec job");

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->core_state.workq, &event,
						      DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) == DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	ASSERT_DOCA_ERR(result, "Failed to retrieve ec job");
	ASSERT_DOCA_ERR(event.result.u64, "EC job finished unsuccessfully");
	ASSERT(((int)(event.type) == (int)DOCA_EC_JOB_RECOVER) && (event.user_data.u64 == DOCA_EC_JOB_RECOVER),
	       DOCA_ERROR_UNEXPECTED, "Received wrong event");

	/* write the result to output file */
	doca_buf_get_head(ec_job.dst_recovered_data_buff, (void **)&resp_head);
	for (i = 0; i < n_missing; i++) {
		ret = sprintf(full_path, "%s/%s%d", dir_path, DATA_BLOCK_FILE_NAME, state->missing_indices[i]);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
		state->block_file = fopen(full_path, "wr");
		ASSERT(state->block_file != NULL, DOCA_ERROR_IO_FAILED, "Unable to open output file: %s", full_path);
		ret = fwrite(resp_head + i * block_size, sizeof(uint8_t), block_size, state->block_file);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
		fclose(state->block_file);
		state->block_file = NULL;
	}

	for (i = 0; i < data_block_count; i++) {
		ret = sprintf(full_path, "%s/%s%ld", dir_path, DATA_BLOCK_FILE_NAME, i);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Path excceded max path len");
		result = read_file(full_path, &state->block_file_data, &block_file_size);
		ASSERT_DOCA_ERR(result, "Unable to open data file");
		if (i == data_block_count - 1)
			block_file_size = file_size - (data_block_count - 1) * block_file_size;
		ret = fwrite(state->block_file_data, sizeof(uint8_t), block_file_size, state->out_file);
		ASSERT(ret >= 0, DOCA_ERROR_IO_FAILED, "Failed to write to file");
		free(state->block_file_data);
		state->block_file_data = NULL;
	}

	DOCA_LOG_INFO("File was decoded successfully and saved in: %s", output_file_path);

	/* Clean and destroy all relevant objects */
	ec_cleanup(state);

	return result;
}

/*
 * Delete data (that EC will recover)
 *
 * @output_path [in]: path to the job output file
 * @missing_indices [in]: data indices to delete
 * @n_missing [in]: indices count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
ec_delete_data(const char *output_path, uint32_t *missing_indices, size_t n_missing)
{
	uint32_t i;
	char full_path[MAX_PATH_NAME];
	int ret;

	for (i = 0; i < n_missing; i++) {
		ret = sprintf(full_path, "%s/%s%d", output_path, DATA_BLOCK_FILE_NAME, missing_indices[i]);
		if (ret >= 0 && remove(full_path) == 0)
			DOCA_LOG_INFO("Deleted successfully: %s", full_path);
		else
			return DOCA_ERROR_IO_FAILED;
	}
	return DOCA_SUCCESS;
}

/*
 * Run ec_recover sample
 *
 * @pci_addr [in]: PCI address of a doca device
 * @input_path [in]: input file to encode or input blocks dir to decode
 * @output_path [in]: output might be a file or a folder - depends on the input and do_both
 * @do_both [in]: to do full process - encoding & decoding
 * @matrix_type [in]: matrix type
 * @data_block_count [in]: data block count
 * @rdnc_block_count [in]: redudancy block count
 * @missing_indices [in]: data indices to delete
 * @n_missing [in]: indices count
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t
ec_recover(const char *pci_addr, const char *input_path, const char *output_path, bool do_both,
	   enum doca_ec_matrix_types matrix_type, uint32_t data_block_count, uint32_t rdnc_block_count,
	   uint32_t *missing_indices, size_t n_missing)
{
	doca_error_t result = DOCA_SUCCESS;
	struct stat path_stat;
	bool input_path_is_file;
	const char *dir_path = output_path;
	const char *output_file_path = NULL;

	if (stat(input_path, &path_stat) != 0) {
		DOCA_LOG_INFO("Can't read input file stat: %s", input_path);
		return DOCA_ERROR_IO_FAILED;
	}
	input_path_is_file = S_ISREG(path_stat.st_mode);
	if (!do_both && !input_path_is_file) { /* only decode mode */
		dir_path = input_path;
		output_file_path = output_path;
	}

	if (do_both || input_path_is_file)
		result = ec_encode(pci_addr, input_path, matrix_type, output_path, data_block_count, rdnc_block_count);
	if (result != DOCA_SUCCESS)
		return result;
	if (do_both)
		result = ec_delete_data(output_path, missing_indices, n_missing);
	if (result != DOCA_SUCCESS)
		return result;
	if (do_both || !input_path_is_file)
		result =
			ec_decode(pci_addr, matrix_type, output_file_path, dir_path, data_block_count, rdnc_block_count);
	return result;
}
