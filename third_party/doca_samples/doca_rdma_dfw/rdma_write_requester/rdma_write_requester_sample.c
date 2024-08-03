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

#include <doca_error.h>
#include <doca_log.h>
#include <doca_buf_inventory.h>
#include <doca_buf.h>

#include "rdma_common.h"

DOCA_LOG_REGISTER(RDMA_WRITE_REQUESTER::SAMPLE);

/*
 * Write the connection details for the responder to read,
 * and read the connection details and the remote mmap string of the responder
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: DOCA RDMA resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
write_read_connection(struct rdma_config *cfg, struct rdma_resources *resources)
{
	int enter = 0;
	doca_error_t result = DOCA_SUCCESS;

	/* Write the RDMA connection details */
	result = write_file(cfg->local_connection_desc_path, (char *)resources->rdma_conn_details,
				resources->rdma_conn_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write the RDMA connection details: %s", doca_get_error_string(result));
		return result;
	}

	DOCA_LOG_INFO("You can now copy %s to the responder", cfg->local_connection_desc_path);
	DOCA_LOG_INFO("Please copy %s and %s from the responder and then press enter",
			cfg->remote_connection_desc_path, cfg->mmap_connection_desc_path);

	/* Wait for enter */
	while (enter != '\r' && enter != '\n')
		enter = getchar();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path, (char **)&resources->remote_rdma_conn_details,
				&resources->remote_rdma_conn_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_get_error_string(result));
		return result;
	}

	/* Read the remote mmap connection details */
	result = read_file(cfg->mmap_connection_desc_path, (char **)&resources->remote_mmap_details,
				&resources->remote_mmap_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read the remote RDMA mmap connection details: %s",
				doca_get_error_string(result));
		return result;
	}

	return result;
}

/*
 * Requester side of the RDMA write
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
rdma_write_requester(struct rdma_config *cfg)
{
	struct rdma_resources resources;
	struct doca_buf_inventory *buf_inventory;
	struct doca_buf *src_buf;
	void *src_buf_data;
	struct doca_buf *dst_buf;
	struct doca_rdma_job_read_write job_write;
	struct doca_rdma_result rdma_result;
	struct doca_event event;
	char *remote_mmap_range;
	size_t remote_mmap_range_len;
	size_t write_string_len = strlen(cfg->write_string) + 1;
	const uint32_t mmap_permissions = DOCA_ACCESS_LOCAL_READ_WRITE;
	const uint32_t rdma_permissions = DOCA_ACCESS_LOCAL_READ_WRITE;
	doca_error_t result, tmp_result;

	/* Allocating resources */
	result = allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_get_error_string(result));
		return result;
	}

	/* write and read connection details to responder */
	result = write_read_connection(cfg, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write and read connection details from the responder: %s",
				doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Connect RDMA */
	result = doca_rdma_connect(resources.rdma, resources.remote_rdma_conn_details,
					resources.remote_rdma_conn_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect the requester's DOCA RDMA to the responder's DOCA RDMA: %s",
				doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Create remote mmap */
	result = doca_mmap_create_from_export(NULL, resources.remote_mmap_details, resources.remote_mmap_details_size,
						resources.doca_device, &(resources.remote_mmap));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create mmap from export: %s", doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Create DOCA buffer inventory */
	result = doca_buf_inventory_create(NULL, INVENTORY_NUM_INITIAL_ELEMENTS, 0, &buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA buffer inventory: %s", doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Start DOCA buffer inventory */
	result = doca_buf_inventory_start(buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA buffer inventory: %s", doca_get_error_string(result));
		goto destroy_buf_inventory;
	}

	/* Get the remote mmap memory range */
	result = doca_mmap_get_memrange(resources.remote_mmap, (void **)&remote_mmap_range, &remote_mmap_range_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get DOCA memory map range: %s", doca_get_error_string(result));
		goto stop_buf_inventory;
	}

	/* Add src buffer to DOCA buffer inventory */
	result = doca_buf_inventory_buf_by_data(buf_inventory, resources.mmap, resources.mmap_memrange,
						write_string_len, &src_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s",
				doca_get_error_string(result));
		goto stop_buf_inventory;
	}

	/* Set data of src buffer to be the string we want to write */
	result = doca_buf_get_data(src_buf, &src_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get source buffer data: %s", doca_get_error_string(result));
		goto destroy_src_buf;
	}
	strncpy(src_buf_data, cfg->write_string, write_string_len);

	/* Add dst buffer to DOCA buffer inventory from the remote mmap */
	result = doca_buf_inventory_buf_by_addr(buf_inventory, resources.remote_mmap, remote_mmap_range,
						write_string_len, &dst_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s", doca_get_error_string(result));
		goto destroy_src_buf;
	}

	/* Set properties of DOCA RDMA WRITE job */
	job_write.base.ctx = resources.rdma_ctx;
	job_write.base.type = DOCA_RDMA_JOB_WRITE;
	job_write.base.flags = DOCA_JOB_FLAGS_NONE;
	job_write.src_buff = src_buf;
	job_write.dst_buff = dst_buf;
	job_write.base.user_data.u64 = 0;

	DOCA_LOG_INFO("Submitting DOCA RDMA write job that writes \"%s\" to the responder", cfg->write_string);
	/* Submit DOCA RDMA send job */
	result = doca_workq_submit(resources.workq, (struct doca_job *)(&job_write));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit a job to workQ: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}

	/* Try to retrieve the results */
	DOCA_LOG_INFO("Retrieving results from the DOCA RDMA write job");
	event.result.ptr = (void *)(&rdma_result);
	do {
		result = doca_workq_progress_retrieve(resources.workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
	} while (result == DOCA_ERROR_AGAIN);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to retrieve DOCA workQ progress: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}
	if (rdma_result.result != DOCA_SUCCESS) {
		result = rdma_result.result;
		DOCA_LOG_ERR("RDMA failed: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}
	if (job_write.base.user_data.u64 != event.user_data.u64) {
		result = DOCA_ERROR_UNEXPECTED;
		DOCA_LOG_ERR("RDMA failed: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}

	DOCA_LOG_INFO("Retrieved results successfully");
	DOCA_LOG_INFO("Written to responder \"%s\"\n", cfg->write_string);

destroy_dst_buf:
	tmp_result = doca_buf_refcount_rm(dst_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease dst_buf count: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_src_buf:
	tmp_result = doca_buf_refcount_rm(src_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease src_buf count: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
stop_buf_inventory:
	tmp_result = doca_buf_inventory_stop(buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA buffer inventory: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_buf_inventory:
	tmp_result = doca_buf_inventory_destroy(buf_inventory);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA buffer inventory: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_resources:
	tmp_result = destroy_rdma_resources(&resources, cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA buffer inventory: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
