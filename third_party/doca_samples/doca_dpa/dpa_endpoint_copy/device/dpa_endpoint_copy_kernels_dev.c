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

/*
 * Kernel function for endpoint sample, copies the content of local buffer to remote buffer using DPA endpoint
 *
 * @ep [in]: Endpoint to use
 * @local_addr [in]: Address of local buffer
 * @local_mem [in]: Memory handle for local memory
 * @length [in]: Length of local buffer
 * @remote_addr [in]: Address of remote buffer
 * @remote_rkey [in]: Access key for remote buffer
 * @remote_ev [in]: Remote event to write
 * @comp_count [in]: Event count to write
 * @comp_op [in]: Operation to apply on the completion event
 */
__dpa_global__ void
dpa_put_signal_nb(doca_dpa_dev_ep_t ep, uint64_t local_addr, doca_dpa_dev_mem_t local_mem, size_t length,
			uint64_t remote_addr, uint32_t remote_rkey, doca_dpa_dev_sync_event_remote_t remote_ev,
			uint64_t comp_count)
{
	/* Copy content of local_addr to remote_addr */
	doca_dpa_dev_put_signal_set_nb(ep, local_addr, local_mem, length, remote_addr, remote_rkey,
					remote_ev, comp_count);

	/* Wait for the copy operation to be completed */
	doca_dpa_dev_ep_synchronize(ep);
}

/*
 * Kernel function for endpoint sample, updates the value of thread_event to val.
 *
 * @thread_event_handler [in]: Event handler to update
 * @val [in]: Value to update the event with
 */
__dpa_global__ void update_event_kernel(doca_dpa_dev_sync_event_t thread_event_handler, uint64_t val)
{
	doca_dpa_dev_sync_event_update_set(thread_event_handler, val);
}
