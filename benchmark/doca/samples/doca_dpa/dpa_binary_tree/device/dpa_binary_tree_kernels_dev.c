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

#include <doca_dpa_dev.h>
#include <doca_dpa_dev_sync_event.h>

#define SYNC_EVENT_MASK_FFS (0xFFFFFFFFFFFFFFFF)			/* Mask for doca_dpa_dev_sync_event_wait_gt() wait value */

/* binary_tree kernel arguments struct */
struct __dpa_global__ binary_tree_kernel_args {
	doca_dpa_dev_sync_event_t wait_event;		/* Event to be waited on by the kernel */
	uint64_t wait_thresh;				/* Wait threshold */
	doca_dpa_dev_sync_event_t left_event;		/* Left event to be updated after waiting on the wait event */
	doca_dpa_dev_sync_event_t right_event;		/* Right event to be updated after waiting on the wait event */
	doca_dpa_dev_sync_event_t comp_event;		/* Completion event to be updated after updating */
							/* left and right events, can be NULL */
	uint64_t comp_val;				/* Value of the completion event after updating */
};

/*
 * Kernel function for binary_tree sample.
 * This function waits on a wait event, then updates two events (left and right) and updates a completion event if it
 * exists.
 *
 * @kernel_args [in]: Kernel arguments that are passed by the host
 */
__dpa_global__ void
binary_tree_kernel(struct binary_tree_kernel_args kernel_args)
{
	/* Event val should be 1 so that it's aligned with the wait_thresh */
	uint64_t event_val = 1;

	/* Waiting on wait event */
	if (kernel_args.wait_event)
		doca_dpa_dev_sync_event_wait_gt(kernel_args.wait_event, kernel_args.wait_thresh - 1, SYNC_EVENT_MASK_FFS);

	/* Setting left event */
	doca_dpa_dev_sync_event_update_set(kernel_args.left_event, event_val);

	/* Setting right event */
	doca_dpa_dev_sync_event_update_set(kernel_args.right_event, event_val);

	/* Setting completion event */
	if (kernel_args.comp_event)
		doca_dpa_dev_sync_event_update_set(kernel_args.comp_event, kernel_args.comp_val);
}
