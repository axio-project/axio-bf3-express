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

#define SYNC_EVENT_MASK_FFS (0xFFFFFFFFFFFFFFFF)	/* Mask for doca_dpa_dev_sync_event_wait_gt() wait value */

/*
 * Kernel function for diamond_tree sample.
 * This function waits on a wait event, then updates two completion event (if exists).
 *
 * @wait_event [in]: DOCA DPA event to wait on
 * @wait_thresh [in]: The threshold of the wait for the wait event
 * @comp_event1 [in]: First completion event
 * @comp_event2 [in]: Second completion event (optional)
 * @comp_op [in]: Operation to apply on the completion event
 */
__dpa_global__ void
diamond_kernel(doca_dpa_dev_sync_event_t wait_event, uint64_t wait_thresh, doca_dpa_dev_sync_event_t comp_event1,
		doca_dpa_dev_sync_event_t comp_event2)
{
	/* Event val should be 1 so that it's aligned with the wait_thresh */
	uint64_t event_val = 1;

	/* Waiting on wait event */
	if (wait_event)
		doca_dpa_dev_sync_event_wait_gt(wait_event, wait_thresh - 1, SYNC_EVENT_MASK_FFS);

	/* Setting first completetion event */
	doca_dpa_dev_sync_event_update_add(comp_event1, event_val);

	/* Setting second completetion event (if exists) */
	if (comp_event2)
		doca_dpa_dev_sync_event_update_add(comp_event2, event_val);
}
