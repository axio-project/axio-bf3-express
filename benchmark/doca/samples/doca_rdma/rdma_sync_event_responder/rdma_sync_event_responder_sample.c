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

DOCA_LOG_REGISTER(RDMA_READ_RESPONDER::SAMPLE);

#define EXAMPLE_SET_VALUE (0xD0CA)  /* Example value to use for setting sync event */

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
	result = write_file(cfg->local_connection_desc_path, (char *)resources->rdma_conn_descriptor,
				resources->rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write the RDMA connection details: %s", doca_error_get_descr(result));
		return result;
	}

	/* Write the RDMA connection details */
	result = write_file(cfg->remote_resource_desc_path, (char *)resources->sync_event_descriptor,
				resources->sync_event_descriptor_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to write sync event export blob: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("You can now copy %s and %s to the requester", cfg->local_connection_desc_path,
			cfg->remote_resource_desc_path);
	DOCA_LOG_INFO("Please copy %s from the requester and then press enter", cfg->remote_connection_desc_path);

	/* Wait for enter */
	while (enter != '\r' && enter != '\n')
		enter = getchar();

	/* Read the remote RDMA connection details */
	result = read_file(cfg->remote_connection_desc_path, (char **)&resources->remote_rdma_conn_descriptor,
				&resources->remote_rdma_conn_descriptor_size);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to read the remote RDMA connection details: %s", doca_error_get_descr(result));

	return result;
}

/*
 * Responder side
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
rdma_sync_event_responder(struct rdma_config *cfg)
{
	struct rdma_resources resources = {0};
	enum doca_ctx_states ctx_state;
	const uint32_t mmap_permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE;
	const uint32_t rdma_permissions = DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_WRITE;
	doca_error_t result, tmp_result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* Allocating resources */
	result = allocate_rdma_resources(cfg, mmap_permissions, rdma_permissions, NULL, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	/* Start RDMA context */
	result = doca_ctx_start(resources.rdma_ctx);
	if (result == DOCA_ERROR_IN_PROGRESS) {
		/* Export DOCA RDMA */
		result = doca_rdma_export(resources.rdma, &(resources.rdma_conn_descriptor),
						&(resources.rdma_conn_descriptor_size));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export DOCA RDMA: %s", doca_error_get_descr(result));
			goto request_stop_ctx;
		}

		result = doca_sync_event_create(&resources.sync_event);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_error_get_descr(result));
			goto request_stop_ctx;
		}

		result = doca_sync_event_add_publisher_location_remote_net(resources.sync_event);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set publisher for DOCA sync event: %s", doca_error_get_descr(result));
			goto destroy_se;
		}

		result = doca_sync_event_add_subscriber_location_cpu(resources.sync_event, resources.doca_device);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set subscriber for DOCA sync event: %s", doca_error_get_descr(result));
			goto destroy_se;
		}

		result = doca_sync_event_start(resources.sync_event);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_error_get_descr(result));
			goto destroy_se;
		}

		/* Export RDMA sync event */
		result = doca_sync_event_export_to_remote_net(resources.sync_event,
								(const uint8_t **)&(resources.sync_event_descriptor),
								&(resources.sync_event_descriptor_size));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export DOCA sync event for RDMA: %s", doca_error_get_descr(result));
			goto stop_se;
		}

		/* write and read connection details from the requester */
		result = write_read_connection(cfg, &resources);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to write and read connection details from the requester: %s",
					doca_error_get_descr(result));
			goto stop_se;
		}

		/* Connect RDMA */
		result = doca_rdma_connect(resources.rdma, resources.remote_rdma_conn_descriptor,
						resources.remote_rdma_conn_descriptor_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to connect the responder's DOCA RDMA to the requester's DOCA RDMA: %s",
					doca_error_get_descr(result));
			goto stop_se;
		}

		/* Move ctx to running state */
		do {
			if (doca_pe_progress(resources.pe) == 0)
				nanosleep(&ts, &ts);

			result = doca_ctx_get_state(resources.rdma_ctx, &ctx_state);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to retrieve RDMA context state: %s",
						doca_error_get_descr(result));
				goto stop_se;
			}
		} while (ctx_state != DOCA_CTX_STATE_RUNNING);
	} else if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start RDMA context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	DOCA_LOG_INFO("Waiting for sync event to be signaled from remote");
	doca_sync_event_wait_gt(resources.sync_event, EXAMPLE_SET_VALUE - 1, UINT64_MAX);

	DOCA_LOG_INFO("Signaling sync event");
	doca_sync_event_update_set(resources.sync_event, EXAMPLE_SET_VALUE + 1);

	DOCA_LOG_INFO("Waiting for sync event to be notified for completion");
	doca_sync_event_wait_gt(resources.sync_event, UINT64_MAX - 1, UINT64_MAX);

	DOCA_LOG_INFO("Done");

stop_se:
	tmp_result = doca_sync_event_stop(resources.sync_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_se:
	tmp_result = doca_sync_event_destroy(resources.sync_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	resources.sync_event_descriptor = NULL;
request_stop_ctx:
	tmp_result = request_stop_ctx(resources.pe, resources.rdma_ctx);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA RDMA context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_resources:
	tmp_result = destroy_rdma_resources(&resources, cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}
