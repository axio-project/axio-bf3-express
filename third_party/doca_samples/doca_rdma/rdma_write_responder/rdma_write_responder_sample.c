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

#define MAX_BUFF_SIZE	(256)	/* Maximum DOCA buffer size */

DOCA_LOG_REGISTER(RDMA_WRITE_RESPONDER::SAMPLE);

/*
 * Write the connection details and the mmap details for the requester to read,
 * and read the connection details of the requester
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

	/* Write the mmap connection details */
	result = write_file(cfg->mmap_connection_desc_path, (char *)resources->mmap_details,
				resources->mmap_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write the RDMA mmap details: %s", doca_get_error_string(result));
		return result;
	}

	DOCA_LOG_INFO("You can now copy %s and %s to the requester", cfg->local_connection_desc_path,
			cfg->mmap_connection_desc_path);
	DOCA_LOG_INFO("Please copy %s from the requester and then press enter", cfg->remote_connection_desc_path);

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
 * Responder side of the RDMA write
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
rdma_write_responder(struct rdma_config *cfg)
{
	struct rdma_resources resources = {0};
	int enter = 0;
	char buffer[MAX_BUFF_SIZE];
	const uint32_t mmap_permissions = DOCA_ACCESS_LOCAL_READ_WRITE | DOCA_ACCESS_RDMA_WRITE;
	const uint32_t rdma_permissions = DOCA_ACCESS_RDMA_WRITE;
	doca_error_t result;

	/* Allocating resources */
	result = allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_get_error_string(result));
		return result;
	}

	/* Export RDMA mmap */
	result = doca_mmap_export_rdma(resources.mmap, resources.doca_device, (const void **)&(resources.mmap_details),
					&(resources.mmap_details_size));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export DOCA mmap for RDMA: %s", doca_get_error_string(result));
		goto destroy_resources;
	}

	/* write and read connection details from the requester */
	result = write_read_connection(cfg, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write and read connection details from the requester: %s",
				doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Connect RDMA */
	result = doca_rdma_connect(resources.rdma, resources.remote_rdma_conn_details,
					resources.remote_rdma_conn_details_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect the responder's DOCA RDMA to the requester's DOCA RDMA: %s",
				doca_get_error_string(result));
		goto destroy_resources;
	}

	/* Wait for enter which means that the requester has finished reading */
	DOCA_LOG_INFO("Wait till the requester has finished writing and press enter");
	while (enter != '\r' && enter != '\n')
		enter = getchar();

	/* Read the data that was written on the mmap */
	strncpy(buffer, resources.mmap_memrange, MAX_BUFF_SIZE - 1);

	/* Check if the buffer is null terminated and of legal size */
	if (strnlen(buffer, MAX_BUFF_SIZE) == MAX_BUFF_SIZE) {
		DOCA_LOG_ERR("The message that was written by the requester exceeds buffer size %d", MAX_BUFF_SIZE);
		result = DOCA_ERROR_INVALID_VALUE;
		goto destroy_resources;
	}

	DOCA_LOG_INFO("Responder has written: \"%s\"\n", buffer);

destroy_resources:
	destroy_rdma_resources(&resources, cfg);
	return result;
}
