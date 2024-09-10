/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <doca_dpa_dev.h>
#include <doca_dpa_dev_rdma.h>
#include <doca_dpa_dev_buf.h>

#include "../common/dpa_ping_pong_common_defs.h"

/**
 * @brief Kernel function of DPA thread
 *
 * This app has 2 DPA Threads (called ping and pong threads), posting send/receive RDMA operations.
 * Both DPA Threads post send RDMA operations with buffer of values in [0-EXPECTED_NUM_RECEIVES)
 * When a completion is received with data `i`, then a local array is updated with received_values[i] = 1.
 * At the end both arrays should be [1, 1, ..., 1].
 *
 * @arg [in]: Kernel argument
 */
__dpa_global__ void thread_kernel(uint64_t arg)
{
	struct dpa_thread_arg *thread_arg = (struct dpa_thread_arg *)arg;
	doca_dpa_dev_completion_element_t comp_element;
	int found = 0;
	struct dpa_ping_pong_tls *tls = (struct dpa_ping_pong_tls *)doca_dpa_dev_thread_get_local_storage();
	uint32_t *received_values = (uint32_t *)tls->received_values_arr_ptr;

	while (1) {
		found = doca_dpa_dev_get_completion(thread_arg->dpa_comp_handle, &comp_element);
		if (!found) {
			continue;
		}

		tls->num_receives++;
		received_values[*((uint64_t *)(thread_arg->recv_addr))] = 1;

		doca_dpa_dev_rdma_post_receive(thread_arg->rdma_handle,
					       thread_arg->recv_addr_mmap_handle,
					       thread_arg->recv_addr,
					       thread_arg->recv_addr_length);
		doca_dpa_dev_rdma_post_send(thread_arg->rdma_handle,
					    thread_arg->send_addr_mmap_handle,
					    thread_arg->send_addr,
					    thread_arg->send_addr_length,
					    0 /* completion_requested */);

		DOCA_DPA_DEV_LOG_INFO("%s --> %s Iteration #%u\n",
				      (tls->is_ping_thread ? "Ping" : "Pong"),
				      (!tls->is_ping_thread ? "Ping" : "Pong"),
				      tls->num_receives);

		if (tls->num_receives == EXPECTED_NUM_RECEIVES) {
			for (uint32_t i = 0; i < EXPECTED_NUM_RECEIVES; i++) {
				if (received_values[i] != 1) {
					DOCA_DPA_DEV_LOG_ERR("%s: DPA Thread didn't receive data with value %u\n",
							     __func__,
							     i);
					doca_dpa_dev_thread_finish();
				}
			}

			doca_dpa_dev_sync_event_update_set(thread_arg->comp_sync_event_handle,
							   thread_arg->comp_sync_event_val);
			doca_dpa_dev_thread_finish();
		}

		/* prepare for next iteration */
		(*((uint64_t *)(thread_arg->send_addr)))++;

		doca_dpa_dev_completion_ack(thread_arg->dpa_comp_handle, 1);
	}

	doca_dpa_dev_thread_finish();
}

/**
 * @brief RPC function to trigger first ping pong send/receive iteration
 *
 * First ping pong send/receive iteration is done using this RPC.
 * After this iteration all remaining send/receive iterations are done within the DPA Thread kernel.
 * On the first iteration the two threads post receive operation on the expected receive addresses.
 * To trigger the first completion the ping thread posts a send operation as well.
 *
 * @ping_thread_arg [in]: Ping thread argument which includes all needed info for RDMA post send/receive
 * @pong_thread_arg [in]: Pong thread argument which includes all needed info for RDMA post receive
 * @return: RPC function always succeed and returns 0
 */
__dpa_rpc__ uint64_t trigger_first_iteration_rpc(struct dpa_thread_arg ping_thread_arg,
						 struct dpa_thread_arg pong_thread_arg)
{
	DOCA_DPA_DEV_LOG_INFO("Trigger First Ping --> Pong Iteration\n");

	doca_dpa_dev_rdma_post_receive(ping_thread_arg.rdma_handle,
				       ping_thread_arg.recv_addr_mmap_handle,
				       ping_thread_arg.recv_addr,
				       ping_thread_arg.recv_addr_length);

	doca_dpa_dev_rdma_post_receive(pong_thread_arg.rdma_handle,
				       pong_thread_arg.recv_addr_mmap_handle,
				       pong_thread_arg.recv_addr,
				       pong_thread_arg.recv_addr_length);

	doca_dpa_dev_rdma_post_send(ping_thread_arg.rdma_handle,
				    ping_thread_arg.send_addr_mmap_handle,
				    ping_thread_arg.send_addr,
				    ping_thread_arg.send_addr_length,
				    0);

	/* prepare for next ping iteration */
	(*((uint64_t *)ping_thread_arg.send_addr))++;

	return 0;
}
