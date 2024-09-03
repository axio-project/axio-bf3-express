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
#include <doca_dpa_dev_rdma.h>
#include <doca_dpa_dev_buf_array.h>

/*
 * Kernel function for rdma sample, copies the content of local buffer to remote buffer using DPA rdma
 *
 * @rdma [in]: RDMA handle to use
 * @remote_buf_array [in]: Handle for remote buffer
 * @local_buf_array [in]: Handle for local buffer
 * @length [in]: Length of local buffer
 * @remote_ev [in]: Remote event to write
 * @comp_count [in]: Event count to write
 */
__dpa_global__ void
dpa_rdma_write_and_signal(doca_dpa_dev_rdma_t rdma,
			  doca_dpa_dev_buf_arr_t remote_buf_array,
			  doca_dpa_dev_buf_arr_t local_buf_array,
			  size_t length,
			  doca_dpa_dev_sync_event_remote_net_t remote_ev,
			  uint64_t comp_count)
{
	doca_dpa_dev_buf_t remote_buf = doca_dpa_dev_buf_array_get_buf(remote_buf_array, 0);
	doca_dpa_dev_buf_t local_buf = doca_dpa_dev_buf_array_get_buf(local_buf_array, 0);

	/* Copy content of local_addr to remote_addr using rdma write */
	doca_dpa_dev_rdma_write(rdma, remote_buf, 0, local_buf, 0, length);
	doca_dpa_dev_rdma_signal_set(rdma, remote_ev, comp_count);

	/* Wait for the copy operation to be completed */
	doca_dpa_dev_rdma_synchronize(rdma);
}

/*
 * Kernel function for rdma sample, updates the value of thread_event to val.
 *
 * @thread_event_handler [in]: Event handler to update
 * @val [in]: Value to update the event with
 */
__dpa_global__ void update_event_kernel(doca_dpa_dev_sync_event_t thread_event_handler, uint64_t val)
{
	doca_dpa_dev_sync_event_update_set(thread_event_handler, val);
}
