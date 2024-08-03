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

#include <stdlib.h>
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include <doca_error.h>
#include <doca_log.h>

#include "dpa_common.h"

#define NUM_OF_NODES 4		/* Number of nodes in the diamond tree */

DOCA_LOG_REGISTER(DIAMOND_TREE::SAMPLE);

/* Kernel function declaration */
extern doca_dpa_func_t diamond_kernel;

/*
 * Create and export NUM_OF_NODES DPA + 1 events with the appropriate update and wait flags:
 *	First event is updated by the CPU and waited on by the DPA.
 *	Second till last node_event are updated and waited on by the DPA.
 *	node_d_comp_event is updated by the DPA and waited on by the CPU.
 *
 * @doca_dpa [in]: Previously created DPA context
 * @doca_device [in]: DOCA device
 * @node_events [out]: Created events
 * @node_event_handles [out]: Created events handlers
 * @node_d_comp_event [out]: Created event
 * @node_d_comp_event_handle [out]: Created event handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dpa_events(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, struct doca_sync_event **node_events,
		  doca_dpa_dev_sync_event_t *node_event_handles, struct doca_sync_event **node_d_comp_event,
		  doca_dpa_dev_sync_event_t *node_d_comp_event_handle)
{
	doca_error_t result, tmp_result;
	int i, j;

	for (i = 0; i < NUM_OF_NODES; i++) {

		if (i == 0) {
			/* Only the first node event will be updated by the CPU and waited on by the DPA */
			result = create_doca_dpa_wait_sync_event(doca_dpa, doca_device, &(node_events[i]));
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to create node event %d: %s", i, doca_get_error_string(result));
				goto destroy_events;
			}
		} else {
			result = create_doca_dpa_kernel_sync_event(doca_dpa, &(node_events[i]));
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to create node event %d: %s", i, doca_get_error_string(result));
				goto destroy_events;
			}
		}

		result = doca_sync_event_export_to_dpa(node_events[i], doca_dpa, &(node_event_handles[i]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export node event %d: %s", i, doca_get_error_string(result));
			doca_sync_event_destroy(node_events[i]);
			goto destroy_events;
		}
	}

	result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, node_d_comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create host completion event: %s", doca_get_error_string(result));
		goto destroy_events;
	}

	result = doca_sync_event_export_to_dpa(*node_d_comp_event, doca_dpa, node_d_comp_event_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export host completion event: %s", doca_get_error_string(result));
		goto destroy_node_d_event;
	}

	return result;

destroy_node_d_event:
	tmp_result = doca_sync_event_destroy(*node_d_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy node_d_comp_event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_events:
	for (j = 0; j < i; j++) {
		tmp_result = doca_sync_event_destroy(node_events[j]);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy node_events[%d]: %s", j, doca_get_error_string(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}
	return result;
}

/*
 * Run diamond_tree sample.
 *
 * @resources [in]: DOCA DPA resources that the DPA sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
diamond_tree(struct dpa_resources *resources)
{
	struct doca_sync_event *node_events[NUM_OF_NODES] = {NULL};
	doca_dpa_dev_sync_event_t node_event_handles[NUM_OF_NODES] = {0};
	struct doca_sync_event *node_d_comp_event = NULL;
	doca_dpa_dev_sync_event_t node_d_comp_event_handle = 0;
	/*
	 * Each kernel node (except kernel node D which has two parents) waits for the wait event threshold
	 * to become 1 because each kernel node in binary tree has one parent only
	 */
	uint64_t one_parent_wait_thresh = 1;
	/* Kernel node D which has two parents waits for the wait event threshold to become 2 */
	uint64_t two_parents_wait_thresh = 2;
	/* event val should be 1 so that it's aligned with the wait_thresh */
	uint64_t event_val = 1;
	/* completion event should have 1 as completion value so that it's aligned with the event_val */
	uint64_t comp_val = 1;
	/* Number of DPA threads */
	const unsigned int num_dpa_threads = 1;
	doca_error_t result;
	doca_error_t tmp_result;
	int i;

	/* Creating DOCA DPA events and exporting handles */
	result = create_dpa_events(resources->doca_dpa, resources->doca_device, node_events, node_event_handles,
						&node_d_comp_event, &node_d_comp_event_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA events: %s", doca_get_error_string(result));
		return result;
	}

	DOCA_LOG_INFO("    A");
	DOCA_LOG_INFO("   / \\");
	DOCA_LOG_INFO("  C   B");
	DOCA_LOG_INFO("  \\  /");
	DOCA_LOG_INFO("    D");

	DOCA_LOG_INFO("All DPA resources have been created");

	/* kernels launch */
	DOCA_LOG_INFO("Launching kernel %c", 'A');

	result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, node_events[0], 0, NULL, 0, num_dpa_threads, &diamond_kernel,
					NULL, 0, node_event_handles[1], node_event_handles[2]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch kernel %c: %s", 'A', doca_get_error_string(result));
		goto destroy_events;
	}

	for (i = 1; i < NUM_OF_NODES - 1; i++) {
		DOCA_LOG_INFO("Launching kernel %c", 'A' + i);
		result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, NULL, 0, NULL, 0, num_dpa_threads,
						&diamond_kernel, node_event_handles[i], one_parent_wait_thresh,
						node_event_handles[NUM_OF_NODES - 1], NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to launch kernel %c: %s", 'A' + i, doca_get_error_string(result));
			goto destroy_events;
		}
	}

	DOCA_LOG_INFO("Launching Kernel D");
	result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, NULL, 0, NULL, 0, num_dpa_threads, &diamond_kernel,
					node_event_handles[NUM_OF_NODES - 1], two_parents_wait_thresh,
					node_d_comp_event_handle, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch kernel D: %s", doca_get_error_string(result));
		goto destroy_events;
	}

	DOCA_LOG_INFO("Signaling Kernel A wait event");
	result = doca_sync_event_update_set(node_events[0], event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_events;
	}

	DOCA_LOG_INFO("Waiting for Kernel D completion event to be signaled");
	/* Wait until Node D completion event reach completion val */
	result = doca_sync_event_wait_gt(node_d_comp_event, comp_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for Kernel D completion event: %s", doca_get_error_string(result));
		goto destroy_events;
	}
	DOCA_LOG_INFO("Kernel D completion event has been signaled");

destroy_events:
	/* destroy events */
	tmp_result = doca_sync_event_destroy(node_d_comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy kernel D completion event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	for (i = 0; i < NUM_OF_NODES; i++) {
		tmp_result = doca_sync_event_destroy(node_events[i]);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy Kernel %c wait event: %s", 'A' + i,
					doca_get_error_string(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}

	return result;
}
