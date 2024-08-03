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
#include <doca_argp.h>

#include "rdma_common.h"

#define MAX_BUFF_SIZE	(2560)	/* Maximum DOCA buffer size */

DOCA_LOG_REGISTER(RDMA_RECEIVE::SAMPLE);

/*
 * Write the connection details for the sender to read, and read the connection details of the sender
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

	DOCA_LOG_INFO("You can now copy %s to the sender", cfg->local_connection_desc_path);
	DOCA_LOG_INFO("Please copy %s from the sender and then press enter", cfg->remote_connection_desc_path);

	/* Wait for enter */
	while (enter != '\r' && enter != '\n')
		enter = getchar();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path, (char **)&resources->remote_rdma_conn_details,
				&resources->remote_rdma_conn_details_size);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_get_error_string(result));

	return result;
}

/*
 * Receive a message from the sender
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
rdma_receive(struct rdma_config *cfg)
{
	struct rdma_resources resources = {0};
	struct doca_buf_inventory *buf_inventory;
	struct doca_buf *dst_buf;
	void *dst_buf_data;
	struct doca_rdma_job_recv job_recv;
	struct doca_rdma_result rdma_result;
	struct doca_event event;
	uint32_t mmap_permissions = DOCA_ACCESS_LOCAL_READ_WRITE;
	uint32_t rdma_permissions = DOCA_ACCESS_LOCAL_READ_WRITE;
	doca_error_t result, tmp_result;

	/* Allocating resources */
	result = allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_get_error_string(result));
		return result;
	}

	/* write and read connection details to the sender */
	result = write_read_connection(cfg, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write and read connection details from sender: %s",
				doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Connect RDMA */
	result = doca_rdma_connect(resources.rdma, resources.remote_rdma_conn_details,
					resources.remote_rdma_conn_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect the receiver's DOCA RDMA to the sender's DOCA RDMA: %s",
				doca_get_error_string(result));
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

	/* Add dst buffer to DOCA buffer inventory */
	result = doca_buf_inventory_buf_by_addr(buf_inventory, resources.mmap, resources.mmap_memrange,
						MAX_BUFF_SIZE, &dst_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate DOCA buffer to DOCA buffer inventory: %s",
				doca_get_error_string(result));
		goto stop_buf_inventory;
	}

	/* Set properties of DOCA RDMA receive job */
	job_recv.base.ctx = resources.rdma_ctx;
	job_recv.base.type = DOCA_RDMA_JOB_RECV;
	job_recv.base.flags = DOCA_JOB_FLAGS_NONE;
	job_recv.dst_buff = dst_buf;
	job_recv.base.user_data.u64 = 0;

	DOCA_LOG_INFO("Submitting DOCA RDMA receive job");
	/* Submit DOCA RDMA receive job */
	result = doca_workq_submit(resources.workq, (struct doca_job *)(&job_recv));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit a job to workQ: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}

	/* Try to retrieve the results */
	DOCA_LOG_INFO("Retrieving results from the DOCA RDMA receive job");
	event.result.ptr = (void *)(&rdma_result);
	do {
		result = doca_workq_progress_retrieve(resources.workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
	} while (result == DOCA_ERROR_AGAIN);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to retrieve DOCA workQ progress: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}
	if (rdma_result.result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RDMA failed: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}
	if (job_recv.base.user_data.u64 != event.user_data.u64) {
		result = DOCA_ERROR_UNEXPECTED;
		DOCA_LOG_ERR("RDMA failed: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}

	/* Read the data that was received */
	result = doca_buf_get_data(dst_buf, &dst_buf_data);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get destination buffer data: %s", doca_get_error_string(result));
		goto destroy_dst_buf;
	}

	/* Check if dst_buf_data in null terminated and of legal size */
	if (strnlen(dst_buf_data, MAX_BUFF_SIZE) == MAX_BUFF_SIZE) {
		DOCA_LOG_ERR("The message that was received from sender exceeds buffer size %d", MAX_BUFF_SIZE);
		result = DOCA_ERROR_INVALID_VALUE;
		goto destroy_dst_buf;
	}

	DOCA_LOG_INFO("Got from sender: \"%s\"\n", (char *)dst_buf_data);
	result = write_file(cfg->sendfile_path, dst_buf_data,
                                strlen(dst_buf_data));


destroy_dst_buf:
	tmp_result = doca_buf_refcount_rm(dst_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease dst_buf count: %s", doca_get_error_string(tmp_result));
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
	destroy_rdma_resources(&resources, cfg);
	return result;
}
