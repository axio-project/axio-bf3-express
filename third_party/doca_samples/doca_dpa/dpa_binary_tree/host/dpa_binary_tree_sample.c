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


#define TREE_HEIGHT 3					/* Tree height */
#define NUM_OF_NODES ((1 << TREE_HEIGHT) - 1)		/* Number of nodes in tree (2^height - 1) */
#define NUM_OF_EVENTS ((1 << (TREE_HEIGHT + 1)) - 1)	/* Number of events (2^(height+1) - 1) */
#define LEFT_EVENT_IDX(INDEX) ((2 * (INDEX)) + 1)	/* Left event index */
#define RIGHT_EVENT_IDX(INDEX) ((2 * (INDEX)) + 2)	/* Right event index */

DOCA_LOG_REGISTER(BINARY_TREE::SAMPLE);

/* Struct that combines an array of events with their handlers */
struct events_export_handle {
	struct doca_sync_event **events;		/* Array of DOCA sync events */
	doca_dpa_dev_sync_event_t *event_handles;	/* Array of DOCA DPA sync events handles */
};

/* Kernel arguments struct */
struct binary_tree_kernel_args {
	doca_dpa_dev_sync_event_t wait_event;		/* Event to be waited on by the kernel */
	uint64_t wait_thresh;				/* Wait threshold */
	doca_dpa_dev_sync_event_t left_event;		/* Left event to be updated after waiting on the wait event */
	doca_dpa_dev_sync_event_t right_event;		/* Right event to be updated after waiting on the wait event */
	doca_dpa_dev_sync_event_t comp_event;		/* Completion event to be updated after updating */
							/* left and right events, can be NULL */
	uint64_t comp_val;				/* Value of the completion event after updating */
};

/* Kernel function declaration */
extern doca_dpa_func_t binary_tree_kernel;

/*
 * Create and export NUM_OF_NODES DPA wait events and NUM_OF_NODES DPA completion events with the appropriate update
 * and wait flags:
 *	First wait event is updated by the CPU and waited on by the DPA.
 *	The rest of the wait events are updated and waited on by the DPA.
 *	The completion events are updated by the worker and waited on by CPU.
 *
 * @doca_dpa [in]: Previously created DPA context
 * @doca_device [in]: DOCA device
 * @wait_events [out]: Created wait events and wait events handlers
 * @comp_events [out]: Created completion events and completion events handlers
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dpa_events(struct doca_dpa *doca_dpa, struct doca_dev *doca_device,
		  struct events_export_handle *wait_events, struct events_export_handle *comp_events)
{
	doca_error_t result;
	int i, j;

	for (i = 0; i < NUM_OF_EVENTS; i++) {
		/* Only the first node event will be updated by the CPU */
		if (i == 0) {
			result = create_doca_dpa_wait_sync_event(doca_dpa, doca_device, &(wait_events->events[i]));
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to create node sync event %d: %s", i, doca_get_error_string(result));
				goto destroy_events;
			}
		} else {
			result = create_doca_dpa_kernel_sync_event(doca_dpa, &(wait_events->events[i]));
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to create node sync event %d: %s", i, doca_get_error_string(result));
				goto destroy_events;
			}
		}

		result = doca_sync_event_export_to_dpa(wait_events->events[i], doca_dpa, &(wait_events->event_handles[i]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export node sync event %d: %s", i, doca_get_error_string(result));
			doca_sync_event_destroy(wait_events->events[i]);
			goto destroy_events;
		}

		/* Create and export completion event */
		result = create_doca_dpa_completion_sync_event(doca_dpa, doca_device, &(comp_events->events[i]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create completion event %d: %s", i, doca_get_error_string(result));
			doca_sync_event_destroy(wait_events->events[i]);
			goto destroy_events;
		}

		result = doca_sync_event_export_to_dpa(comp_events->events[i], doca_dpa, &(comp_events->event_handles[i]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to export completion event %d: %s", i, doca_get_error_string(result));
			doca_sync_event_destroy(wait_events->events[i]);
			doca_sync_event_destroy(comp_events->events[i]);
			goto destroy_events;
		}
	}

	return result;

destroy_events:
	for (j = 0; j < i; j++) {
		doca_sync_event_destroy(wait_events->events[j]);
		doca_sync_event_destroy(comp_events->events[j]);
	}
	return result;
}

/*
 * Run binary_tree sample
 *
 * @resources [in]: DOCA DPA resources that the DPA sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
binary_tree(struct dpa_resources *resources)
{
	struct doca_sync_event *wait_events[NUM_OF_EVENTS] = {NULL};
	doca_dpa_dev_sync_event_t wait_event_handles[NUM_OF_EVENTS] = {0};
	struct doca_sync_event *comp_events[NUM_OF_EVENTS] = {NULL};
	doca_dpa_dev_sync_event_t comp_event_handles[NUM_OF_EVENTS] = {0};
	struct events_export_handle wait_events_export_handle = {
		.events = wait_events,
		.event_handles = wait_event_handles,
	};
	struct events_export_handle comp_events_export_handle = {
		.events = comp_events,
		.event_handles = comp_event_handles,
	};
	/*
	 * Each kernel node waits for the wait event threshold to become 1 because each kernel node in binary tree has
	 * one parent only
	 */
	uint64_t wait_thresh = 1;
	/* Each completion event should have 1 as completion value so that it's aligned with the wait_thresh  */
	uint64_t comp_val = 1;
	/* Event val should be 1 so that it's aligned with the wait_thresh */
	uint64_t event_val = 1;
	/* Arguments for kernels */
	struct binary_tree_kernel_args kernel_args[NUM_OF_NODES];
	/* Number of DPA threads */
	const unsigned int num_dpa_threads = 1;
	doca_error_t result;
	doca_error_t tmp_result;
	int i;

	/* Create DOCA sync events for the DPA */
	result = create_dpa_events(resources->doca_dpa, resources->doca_device, &wait_events_export_handle, &comp_events_export_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA events: %s", doca_get_error_string(result));
		return result;
	}

	for (i = 0; i < NUM_OF_NODES; i++) {
		kernel_args[i].wait_event = wait_event_handles[i];
		kernel_args[i].wait_thresh = wait_thresh;
		kernel_args[i].left_event = wait_event_handles[LEFT_EVENT_IDX(i)];
		kernel_args[i].right_event = wait_event_handles[RIGHT_EVENT_IDX(i)];
		kernel_args[i].comp_event = comp_event_handles[i];
		kernel_args[i].comp_val = comp_val;
	}

	DOCA_LOG_INFO("All DPA resources have been created");

	DOCA_LOG_INFO("     A  ");
	DOCA_LOG_INFO("   /   \\");
	DOCA_LOG_INFO("  B     C  ");
	DOCA_LOG_INFO(" / \\   / \\ ");
	DOCA_LOG_INFO("D   E F   G");

	/* kernels launch */
	for (i = 0; i < NUM_OF_NODES; i++) {
		DOCA_LOG_INFO("Launching kernel %c", 'A' + i);
		result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, NULL, 0, NULL, 0, num_dpa_threads,
								&binary_tree_kernel, kernel_args[i]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to launch kernel %c: %s", 'A' + i, doca_get_error_string(result));
			goto destroy_events;
		}
	}

	DOCA_LOG_INFO("Triggering kernel A wait event in 2 seconds");
	sleep(2);
	result = doca_sync_event_update_set(wait_events[0], event_val);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update kernel A wait event: %s", doca_get_error_string(result));
		goto destroy_events;
	}

	for (i = 0; i < NUM_OF_NODES; i++) {
		DOCA_LOG_INFO("Waiting for completion event of kernel %c to be signaled", 'A' + i);
		/* Wait until completion event reach completion val */
		result = doca_sync_event_wait_gt(comp_events[i], comp_val - 1, SYNC_EVENT_MASK_FFS);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to wait for host completion event %d: %s", i,
					doca_get_error_string(result));
			goto destroy_events;
		}
		DOCA_LOG_INFO("Completion event kernel %c has been signaled", 'A' + i);
		sleep(1);
	}

destroy_events:
	/* destroy events */
	for (i = 0; i < NUM_OF_EVENTS; i++) {
		tmp_result = doca_sync_event_destroy(wait_events[i]);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy wait event %d: %s", i, doca_get_error_string(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
		tmp_result = doca_sync_event_destroy(comp_events[i]);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy completion event %d: %s", i, doca_get_error_string(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}

	return result;
}
