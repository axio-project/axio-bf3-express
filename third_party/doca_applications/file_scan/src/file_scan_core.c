/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <samples/common.h>

#include <utils.h>

#include "file_scan_core.h"

DOCA_LOG_REGISTER(FILE_SCAN::Core);

#define SLEEP_IN_NANOS (10 * 1000)	/* Sample the job every 10 microseconds */
#define BUF_INVENTORY_POOL_SIZE 1000	/* Number of elements in the buffer inventory */

/*
 * Structure to hold the various pieces of data pertaining to each job
 */
struct file_scan_job_metadata {
	uint64_t id;				/* id of the job */
	struct doca_buf *job_data;		/* Pointer to the data to be scanned with this job */
	struct doca_regex_search_result result;	/* Storage for results */
};

/*
 * Calculate needed memory for the given number of chunks
 *
 * @app_cfg [in]: Application configuration
 */
static void
calculate_mempool_size(struct file_scan_config *app_cfg)
{
	long job_size = app_cfg->chunk_size > 0 ? app_cfg->chunk_size : app_cfg->data_buffer_len;

	app_cfg->mempool_size = MAX_MATCHES_PER_JOB * ((job_size / BF2_REGEX_JOB_LIMIT) + 1);
	app_cfg->nb_jobs = ((app_cfg->data_buffer_len + job_size - 1) / job_size);
}

doca_error_t
file_scan_init(struct file_scan_config *app_cfg)
{
	doca_error_t result;

	calculate_mempool_size(app_cfg);

	/* find doca_dev */
	result = open_doca_device_with_pci(app_cfg->pci_address, NULL, &app_cfg->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("No device matching PCI address found. Reason: %s", doca_get_error_string(result));
		return result;
	}

	/* Create a DOCA RegEx instance */
	result = doca_regex_create(&(app_cfg->doca_regex));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create RegEx device. Reason: %s", doca_get_error_string(result));
		doca_dev_close(app_cfg->dev);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Set the RegEx device as the main HW accelerator */
	result = doca_ctx_dev_add(doca_regex_as_ctx(app_cfg->doca_regex), app_cfg->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set RegEx device. Reason: %s", doca_get_error_string(result));
		result = DOCA_ERROR_INVALID_VALUE;
		goto regex_destroy;
	}

	/* Set work queue memory pool size */
	result = doca_regex_set_workq_matches_memory_pool_size(app_cfg->doca_regex, app_cfg->mempool_size * app_cfg->nb_jobs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set matches mempool size. Reason: %s", doca_get_error_string(result));
		goto regex_destroy;
	}

	/* Set Overlap in bytes for huge job(s) */
	result = doca_regex_set_huge_job_emulation_overlap_size(app_cfg->doca_regex, app_cfg->nb_overlap_bytes);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set huge job emulation overlap size. Reason: %s", doca_get_error_string(result));
		goto regex_destroy;
	}

	/* Attach rules file to DOCA RegEx */
	result = doca_regex_set_hardware_compiled_rules(app_cfg->doca_regex, app_cfg->rules_buffer,
								app_cfg->rules_buffer_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to program rules file. Reason: %s", doca_get_error_string(result));
		goto regex_destroy;
	}

	/* Start doca RegEx */
	result = doca_ctx_start(doca_regex_as_ctx(app_cfg->doca_regex));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start DOCA RegEx. Reason: %s", doca_get_error_string(result));
		result = DOCA_ERROR_INITIALIZATION;
		goto regex_destroy;
	}

	result = doca_buf_inventory_create(NULL, BUF_INVENTORY_POOL_SIZE, DOCA_BUF_EXTENSION_NONE, &app_cfg->buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create doca_buf_inventory. Reason: %s", doca_get_error_string(result));
		goto regex_cleanup;
	}

	result = doca_buf_inventory_start(app_cfg->buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start doca_buf_inventory. Reason: %s", doca_get_error_string(result));
		goto buf_inventory_cleanup;
	}

	result = doca_mmap_create(NULL, &app_cfg->mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create doca_mmap. Reason: %s", doca_get_error_string(result));
		goto buf_inventory_cleanup;
	}

	result = doca_mmap_dev_add(app_cfg->mmap, app_cfg->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to add device to doca_mmap. Reason: %s", doca_get_error_string(result));
		goto mmap_cleanup;
	}

	result = doca_mmap_set_memrange(app_cfg->mmap, app_cfg->data_buffer, app_cfg->data_buffer_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register memory with doca_mmap. Reason: %s", doca_get_error_string(result));
		goto mmap_cleanup;
	}

	result = doca_mmap_start(app_cfg->mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start doca_mmap. Reason: %s", doca_get_error_string(result));
		goto mmap_cleanup;
	}

	result = doca_workq_create(BUF_INVENTORY_POOL_SIZE, &app_cfg->workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create work queue. Reason: %s", doca_get_error_string(result));
		goto mmap_cleanup;
	}

	result = doca_ctx_workq_add(doca_regex_as_ctx(app_cfg->doca_regex), app_cfg->workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to attach work queue to RegEx. Reason: %s", doca_get_error_string(result));
		goto workq_destroy;
	}

	app_cfg->metadata_pool = doca_regex_mempool_create(sizeof(struct file_scan_job_metadata), BUF_INVENTORY_POOL_SIZE);
	if (app_cfg->metadata_pool  == NULL) {
		DOCA_LOG_ERR("Unable to create meta-data pool. Reason: %s", doca_get_error_string(DOCA_ERROR_NO_MEMORY));
		goto workq_cleanup;
	}

	return DOCA_SUCCESS;

workq_cleanup:
	doca_ctx_workq_rm(doca_regex_as_ctx(app_cfg->doca_regex), app_cfg->workq);
workq_destroy:
	doca_workq_destroy(app_cfg->workq);
mmap_cleanup:
	doca_mmap_destroy(app_cfg->mmap);
buf_inventory_cleanup:
	doca_buf_inventory_destroy(app_cfg->buf_inventory);
regex_cleanup:
	doca_ctx_stop(doca_regex_as_ctx(app_cfg->doca_regex));
regex_destroy:
	doca_regex_destroy(app_cfg->doca_regex);
	doca_dev_close(app_cfg->dev);
	return result;
}

/*
 * Initialize the first job request
 *
 * @app_cfg [in]: Application configuration
 */
static void
init_job_request(struct file_scan_config *app_cfg)
{
	/* start with job id 0 */
	app_cfg->job_id_next = 0;
	/* Set the length according to max chunk size */
	if (app_cfg->chunk_size == 0 || app_cfg->chunk_size > app_cfg->data_buffer_len)
		app_cfg->chunk_size = app_cfg->data_buffer_len;
}

/*
 * Returns line number of the given data and offset
 *
 * @data [in]: String data
 * @offset [in]: Offset in the data
 * @last_newline_idx [out]: Index of the last newline character
 * @return: Line number on success and negative value otherwise
 */
static int
get_line_number(char *data, uint32_t offset, int *last_newline_idx)
{
	int res = 1;
	uint32_t idx;
	*last_newline_idx = 0;

	if (data == NULL)
		return -1;

	for (idx = 0; idx < offset; idx++) {
		if (data[idx] == '\n') {
			*last_newline_idx = idx;
			res++;
		}
	}
	return res;
}

/*
 * Prints RegEx match result
 *
 * @app_cfg [in]: Application configuration
 * @event [in]: DOCA event struct
 */
static void
report_results(struct file_scan_config *app_cfg, struct doca_event *event)
{
	int regex_match_line_nb, last_newline_idx, regex_match_i = 0;
	struct file_scan_job_metadata * const meta = (struct file_scan_job_metadata *)event->user_data.ptr;
	struct doca_regex_search_result * const result = &(meta->result);
	struct doca_regex_match *match;
	int match_index, match_start_offset;

	if (result->detected_matches > 0)
		DOCA_LOG_INFO("Job %" PRIu64 " complete. Detected %d match(es)", meta->id,
				result->detected_matches);
	app_cfg->total_matches += result->detected_matches;
	if (result->num_matches == 0)
		return;
	/* Match start is relative the whole file and not the chunk */
	match_start_offset = meta->id * app_cfg->chunk_size;
	for (match = result->matches; match != NULL;) {

		regex_match_line_nb = get_line_number(app_cfg->data_buffer, match_start_offset + match->match_start,
							&last_newline_idx);
		match_index = match_start_offset + (match->match_start - last_newline_idx);
		if (app_cfg->csv_fp != NULL)
			fprintf(app_cfg->csv_fp, "%d,%d,%d,%d\n", regex_match_line_nb, match_index, match->length,
				match->rule_id);
		DOCA_LOG_INFO("Match %d:", regex_match_i++);
		DOCA_LOG_INFO("\t\tLine Number:  %12d", regex_match_line_nb);
		DOCA_LOG_INFO("\t\tMatch Index:  %12d", match_index);
		DOCA_LOG_INFO("\t\tMatch Length: %12d", match->length);
		DOCA_LOG_INFO("\t\tRule Id:      %12d", match->rule_id);

		struct doca_regex_match *const to_release_match = match;

		match = match->next;
		doca_regex_mempool_put_obj(result->matches_mempool, to_release_match);
	}

	result->matches = NULL;
}

/*
 * Enqueue a job request to the RegEx engine
 *
 * @app_cfg [in]: Application configuration
 * @remaining_bytes [in/out]: Remaining bytes to scan
 * @nb_enqueued_jobs [out]: Number of enqueued jobs
 * @return: 0 on success and negative value on error
 */
static int
file_scan_enqueue_job(struct file_scan_config *app_cfg, uint32_t *remaining_bytes, uint32_t *nb_enqueued_jobs)
{
	doca_error_t res;
	uint32_t nb_enqueued = 0;
	uint32_t nb_free = 0;
	struct file_scan_job_metadata *meta;

	doca_buf_inventory_get_num_free_elements(app_cfg->buf_inventory, &nb_free);
	meta = (struct file_scan_job_metadata *)doca_regex_mempool_get_obj(app_cfg->metadata_pool);

	if (nb_free == 0 && meta != NULL) {
		doca_regex_mempool_put_obj(app_cfg->metadata_pool, meta);
		meta = NULL;
	}

	if (*remaining_bytes != 0 && nb_free != 0 && meta != NULL) {
		uint32_t const job_size =
			app_cfg->chunk_size < *remaining_bytes ? app_cfg->chunk_size : *remaining_bytes;
		uint32_t const read_offset = app_cfg->data_buffer_len - *remaining_bytes;
		bool const allow_aggregation = (*remaining_bytes != job_size) && (nb_free != 1);
		struct doca_buf *buf;
		void *mbuf_data;

		if (doca_buf_inventory_buf_by_addr(app_cfg->buf_inventory, app_cfg->mmap,
							app_cfg->data_buffer + read_offset, job_size,
							&buf) != DOCA_SUCCESS) {
			doca_regex_mempool_put_obj(app_cfg->metadata_pool, meta);
			*nb_enqueued_jobs = nb_enqueued;
			return 0;
		}
		doca_buf_get_data(buf, &mbuf_data);
		doca_buf_set_data(buf, mbuf_data, job_size);

		struct doca_regex_job_search const job = {
			.base = {
				.ctx = doca_regex_as_ctx(app_cfg->doca_regex),
				.type = DOCA_REGEX_JOB_SEARCH,
				.user_data = { .ptr = meta },
			},
			.rule_group_ids = { 1, 0, 0, 0 },
			.buffer = buf,
			.result = &(meta->result),
			.allow_batching = allow_aggregation
		};

		meta->id = app_cfg->job_id_next;
		res = doca_workq_submit(app_cfg->workq, (struct doca_job *)&job);
		if (res == DOCA_SUCCESS) {
			*remaining_bytes -= job_size; /* Update remaining bytes to scan */
			nb_enqueued++;

			/* store ref to job data so it can be released once a result is obtained */
			meta->job_data = buf;
			/* Prepare next chunk id */
			++app_cfg->job_id_next;
		} else if (res == DOCA_ERROR_NO_MEMORY) {
			doca_buf_refcount_rm(buf, NULL);
			doca_regex_mempool_put_obj(app_cfg->metadata_pool, meta);
			*nb_enqueued_jobs = 0; /* QP is full, try to dequeue */
		} else {

			DOCA_LOG_ERR("Unable to enqueue job. [%s]", doca_get_error_string(res));
			*nb_enqueued_jobs = nb_enqueued;
			return -res;
		}
	}

	*nb_enqueued_jobs = nb_enqueued;
	return 0;
}

/*
 * Dequeue a job response from the RegEx engine
 *
 * @app_cfg [in]: Application configuration
 * @nb_dequeued_jobs [out]: Number of dequeued jobs
 * @return: 0 on success and negative value on error
 */
static int
file_scan_dequeue_job(struct file_scan_config *app_cfg, uint32_t *nb_dequeued_jobs)
{
	int result = 0;
	doca_error_t res;
	uint32_t nb_dequeued = 0;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	struct doca_event event = {0};
	struct file_scan_job_metadata *meta;

	do {
		res = doca_workq_progress_retrieve(app_cfg->workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
		if (res == DOCA_SUCCESS) {
			/* Handle the completed jobs */
			meta = (struct file_scan_job_metadata *)event.user_data.ptr;
			++nb_dequeued;
			if (meta->result.status_flags & DOCA_REGEX_STATUS_SEARCH_FAILED) {
				DOCA_LOG_ERR("RegEx search failed");
				if (meta->result.status_flags & DOCA_REGEX_STATUS_MAX_MATCH)
					DOCA_LOG_ERR("DOCA RegEx engine reached maximum number of matches, should reduce job size by using \"chunk-size\" flag");
				/* In case there are other jobs in workq, need to dequeue them and then to exit */
				result = -1;
			} else
				report_results(app_cfg, &event);
			doca_buf_refcount_rm(meta->job_data, NULL);
			doca_regex_mempool_put_obj(app_cfg->metadata_pool, meta);
		} else if (res == DOCA_ERROR_AGAIN) {
			nanosleep(&ts, &ts);	/* Wait for the job to complete */
		} else {
			DOCA_LOG_ERR("Unable to dequeue results. [%s]", doca_get_error_string(res));
			return -res;
		}
	} while (res == DOCA_SUCCESS);

	*nb_dequeued_jobs = nb_dequeued;
	return result;
}

int
file_scan_run(struct file_scan_config *app_cfg)
{
	int ret;
	uint32_t remaining_bytes;
	uint32_t total_enqueued = 0;
	uint32_t total_dequeued = 0;
	uint32_t nb_jobs;


	/* Initialize the first job request */
	init_job_request(app_cfg);

	/* The main loop, enqueues jobs and dequeues for results */
	remaining_bytes = app_cfg->data_buffer_len;

	do {
		ret = file_scan_enqueue_job(app_cfg, &remaining_bytes, &nb_jobs);
		if (ret < 0)
			return ret;
		total_enqueued += nb_jobs;

		ret = file_scan_dequeue_job(app_cfg, &nb_jobs);
		if (ret < 0)
			return ret;
		total_dequeued += nb_jobs;

	} while (remaining_bytes > 0 || total_dequeued != total_enqueued);

	DOCA_LOG_DBG("==============================");
	DOCA_LOG_DBG("File size:\t\t%ld", app_cfg->data_buffer_len);
	DOCA_LOG_DBG("Total scanned bytes:\t%ld", app_cfg->data_buffer_len - remaining_bytes);
	DOCA_LOG_DBG("Total chunks:\t\t%d", total_enqueued);
	DOCA_LOG_DBG("Total matches:\t\t%d", app_cfg->total_matches);
	DOCA_LOG_DBG("==============================");

	return 0;
}

void
file_scan_cleanup(struct file_scan_config *app_cfg)
{
	doca_regex_mempool_destroy(app_cfg->metadata_pool);
	doca_ctx_workq_rm(doca_regex_as_ctx(app_cfg->doca_regex), app_cfg->workq);
	doca_workq_destroy(app_cfg->workq);
	doca_ctx_stop(doca_regex_as_ctx(app_cfg->doca_regex));
	doca_regex_destroy(app_cfg->doca_regex);
	app_cfg->doca_regex = NULL;
	if (app_cfg->mmap != NULL) {
		doca_mmap_destroy(app_cfg->mmap);
		app_cfg->mmap = NULL;
	}
	if (app_cfg->buf_inventory != NULL) {
		doca_buf_inventory_stop(app_cfg->buf_inventory);
		doca_buf_inventory_destroy(app_cfg->buf_inventory);
		app_cfg->buf_inventory = NULL;
	}
	if (app_cfg->data_buffer != NULL) {
		free(app_cfg->data_buffer);
		app_cfg->data_buffer = NULL;
	}
	if (app_cfg->rules_buffer != NULL) {
		free(app_cfg->rules_buffer);
		app_cfg->rules_buffer = NULL;
	}
	if (app_cfg->csv_fp != NULL) {
		fclose(app_cfg->csv_fp);
		app_cfg->csv_fp = NULL;
	}
	if (app_cfg->dev != NULL) {
		doca_dev_close(app_cfg->dev);
		app_cfg->dev = NULL;
	}
}

/*
 * ARGP Callback - Handle rules parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
rules_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	char *data_path = (char *)param;

	/* Read data file into the data buffer */
	return read_file(data_path, &app_cfg->rules_buffer, &app_cfg->rules_buffer_len);
}


/*
 * ARGP Callback - Handle data parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
data_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	char *data_path = (char *)param;

	/* Read data file into the data buffer */
	return read_file(data_path, &app_cfg->data_buffer, &app_cfg->data_buffer_len);
}

/*
 * ARGP Callback - Handle RegEx PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
pci_address_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	char *pci_address = (char *)param;

	if (strnlen(pci_address, DOCA_DEVINFO_PCI_ADDR_SIZE) == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	strlcpy(app_cfg->pci_address, pci_address, DOCA_DEVINFO_PCI_ADDR_SIZE);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle output CSV file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
csv_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	char *csv_path = (char *)param;

	if (csv_path == NULL)
		return DOCA_SUCCESS;
	app_cfg->csv_fp = fopen(csv_path, "w");
	if (app_cfg->csv_fp == NULL) {
		DOCA_LOG_ERR("Failed to create CSV file. Skipping");
		return DOCA_SUCCESS;
	}
	fprintf(app_cfg->csv_fp, "Line number,Match Index,Match Length,Rule Id\n");
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle chunk size parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
chunk_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	int chunk = *(int *)param;

	if (chunk < 0) {
		DOCA_LOG_ERR("Chunk size must be > 0");
		return DOCA_ERROR_INVALID_VALUE;
	}
	app_cfg->chunk_size = (uint32_t)chunk;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle overlap size parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
overlap_callback(void *param, void *config)
{
	struct file_scan_config *app_cfg = (struct file_scan_config *)config;
	int overlap = *(int *)param;

	if (overlap < 0) {
		DOCA_LOG_ERR("Overlap size must be > 0");
		return DOCA_ERROR_INVALID_VALUE;
	}
	app_cfg->nb_overlap_bytes = (uint32_t)overlap;
	return DOCA_SUCCESS;
}

doca_error_t
register_file_scan_params(void)
{
	doca_error_t result;
	struct doca_argp_param *rules_param, *data_param, *pci_param, *csv_param, *chunk_param, *overlap_param;

	/* Create and register RegEx rules param */
	result = doca_argp_param_create(&rules_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(rules_param, "r");
	doca_argp_param_set_long_name(rules_param, "rules");
	doca_argp_param_set_arguments(rules_param, "<path>");
	doca_argp_param_set_description(rules_param, "Path to compiled rules file (rof2.binary)");
	doca_argp_param_set_callback(rules_param, rules_callback);
	doca_argp_param_set_type(rules_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(rules_param);
	result = doca_argp_register_param(rules_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register data to scan param */
	result = doca_argp_param_create(&data_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(data_param, "d");
	doca_argp_param_set_long_name(data_param, "data");
	doca_argp_param_set_arguments(data_param, "<path>");
	doca_argp_param_set_description(data_param, "Path to data file");
	doca_argp_param_set_callback(data_param, data_callback);
	doca_argp_param_set_type(data_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(data_param);
	result = doca_argp_register_param(data_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register RegEx PCI address param */
	result = doca_argp_param_create(&pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(pci_param, "p");
	doca_argp_param_set_long_name(pci_param, "pci-addr");
	doca_argp_param_set_arguments(pci_param, "<address>");
	doca_argp_param_set_description(pci_param, "Set PCI address of the RXP engine to use");
	doca_argp_param_set_callback(pci_param, pci_address_callback);
	doca_argp_param_set_type(pci_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(pci_param);
	result = doca_argp_register_param(pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register output CSV file path param */
	result = doca_argp_param_create(&csv_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(csv_param, "o");
	doca_argp_param_set_long_name(csv_param, "output-csv");
	doca_argp_param_set_arguments(csv_param, "<path>");
	doca_argp_param_set_description(csv_param, "Path to the output of the CSV file");
	doca_argp_param_set_callback(csv_param, csv_callback);
	doca_argp_param_set_type(csv_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(csv_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register chunk size param */
	result = doca_argp_param_create(&chunk_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(chunk_param, "c");
	doca_argp_param_set_long_name(chunk_param, "chunk-size");
	doca_argp_param_set_arguments(chunk_param, "<bytes_number>");
	doca_argp_param_set_description(chunk_param,
					"Chunk size of each job sent to the regex,  use 0 to send the file as 1 chunk");
	doca_argp_param_set_callback(chunk_param, chunk_callback);
	doca_argp_param_set_type(chunk_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(chunk_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register overlap size param */
	result = doca_argp_param_create(&overlap_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(overlap_param, "s");
	doca_argp_param_set_long_name(overlap_param, "overlap-size");
	doca_argp_param_set_arguments(overlap_param, "<bytes_number>");
	doca_argp_param_set_description(overlap_param, "Number of bytes of overlap to use for huge jobs");
	doca_argp_param_set_callback(overlap_param, overlap_callback);
	doca_argp_param_set_type(overlap_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(overlap_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_get_error_string(result));
		return result;
	}
	return DOCA_SUCCESS;
}
