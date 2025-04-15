/*
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES.
 * Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Source file with utilities for DPA.
 */

#include "com_dev.h"
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <stddef.h>
#include <dpaintrin.h>

#define SWAP(a, b) \
	do { \
		__typeof__(a) tmp;  \
		tmp = a;            \
		a = b;              \
		b = tmp;            \
	} while (0)

/* Swap source and destination MAC addresses in the packet.
 *  packet - pointer to the packet.
 */
void swap_macs(char *packet)
{
	char *dmac, *smac;
	int i;

	dmac = packet;
	smac = packet + 6;
	for (i = 0; i < 6; i++, dmac++, smac++)
		SWAP(*smac, *dmac);
}

/* Fill the cq_ctx_t structure with input parameters
 *  cq_ctx - structure to be filled
 *  num - cq_number
 *  log_depth - CQ log depth
 *  ring_addr - address of the ring
 *  dbr_addr - address of the DBR
 */
void com_cq_ctx_init(cq_ctx_t *cq_ctx, uint32_t num, uint32_t log_depth, flexio_uintptr_t ring_addr,
		     flexio_uintptr_t dbr_addr)
{
	cq_ctx->cq_number = num;
	cq_ctx->cq_ring = (struct flexio_dev_cqe64 *)ring_addr;
	cq_ctx->cq_dbr = (uint32_t *)dbr_addr;

	cq_ctx->cqe = cq_ctx->cq_ring;
	cq_ctx->cq_idx = 0;
	cq_ctx->cq_hw_owner_bit = 0x1;
	cq_ctx->log_cq_depth = log_depth;
}

/* Fill the rq_ctx_t structure with input parameters
 *  rq_ctx - structure to be filled
 *  num - rq_number
 *  ring_addr - address of the ring
 *  dbr_addr - address of the DBR
 */
void com_rq_ctx_init(rq_ctx_t *rq_ctx, uint32_t num, flexio_uintptr_t ring_addr,
		     flexio_uintptr_t dbr_addr)
{
	rq_ctx->rq_number = num;
	rq_ctx->rq_ring = (struct flexio_dev_wqe_rcv_data_seg *)ring_addr;
	rq_ctx->rq_dbr = (uint32_t *)dbr_addr;
}

/* Fill the sq_ctx_t structure with input parameters
 *  sq_ctx - structure to be filled
 *  num - sq_number
 *  ring_addr - address of the ring
 */
void com_sq_ctx_init(sq_ctx_t *sq_ctx, uint32_t num, flexio_uintptr_t ring_addr)
{
	sq_ctx->sq_number = num;
	sq_ctx->sq_ring = (union flexio_dev_sqe_seg *)ring_addr;

	sq_ctx->sq_wqe_seg_idx = 0;
}

/* Fill the eq_ctx_t structure with input parameters
 *  eq_ctx - structure to be filled
 *  num - sq_number
 *  ring_addr - address of the ring
 */
void com_eq_ctx_init(eq_ctx_t *eq_ctx, uint32_t num, flexio_uintptr_t ring_addr)
{
	eq_ctx->eq_number = num;
	eq_ctx->eq_ring = (struct flexio_dev_eqe *)ring_addr;

	eq_ctx->eqe = eq_ctx->eq_ring;
	eq_ctx->eq_idx = 0;
	eq_ctx->eq_hw_owner_bit = 0x1;
}

/* Fill structure dt_ctx_t with input parameters
 *  dt_ctx - structure to be filled
 *  buff_addr - address of the buffer
 */
void com_dt_ctx_init(dt_ctx_t *dt_ctx, flexio_uintptr_t buff_addr)
{
	dt_ctx->sq_tx_buff = (void *)buff_addr;
	dt_ctx->tx_buff_idx = 0;
}

/* Update the cq_ctx_t structure and set the DBR for the CQE.
 *  cq_ctx - pointer to cq_ctx_t structure to update
 */
void com_step_cq(cq_ctx_t *cq_ctx)
{
	uint32_t cq_idx_mask = L2M(cq_ctx->log_cq_depth);

	cq_ctx->cq_idx++;
	cq_ctx->cqe = &cq_ctx->cq_ring[cq_ctx->cq_idx & cq_idx_mask];
	/* check for wrap around */
	if (!(cq_ctx->cq_idx & cq_idx_mask))
		cq_ctx->cq_hw_owner_bit = !cq_ctx->cq_hw_owner_bit;

	flexio_dev_dbr_cq_set_ci(cq_ctx->cq_dbr, cq_ctx->cq_idx);
}

/* Update the eq_ctx_t structure and EQ with no DBR.
 *  eq_ctx - pointer to the eq_ctx_t structure.
 *  eq_idx_mask - mask of indexes.
 */
void com_step_eq(eq_ctx_t *eq_ctx, uint32_t eq_idx_mask)
{
	uint32_t eq_ci;

	eq_ci = eq_ctx->eq_idx++;
	eq_ctx->eqe = &eq_ctx->eq_ring[eq_ctx->eq_idx & eq_idx_mask];
	/* check for wrap around */
	if (!(eq_ctx->eq_idx & eq_idx_mask))
		eq_ctx->eq_hw_owner_bit = !eq_ctx->eq_hw_owner_bit;

	/* No DBR */
	flexio_dev_eq_update_ci(eq_ci, eq_ctx->eq_number);
}

/* Get the pointer to the next data entry.
 *  dt_ctx - the pointer to the dt_ctx_t structure.
 *  dt_idx_mask - mask of indexes.
 *  log_dt_entry_sz - logarithm size of the data entry
 */
void *get_next_dte(dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz)
{
	return (char *)(dt_ctx->sq_tx_buff) +
	       ((dt_ctx->tx_buff_idx++ & dt_idx_mask) << log_dt_entry_sz);
}

/* Get the pointer to the next SQ entry.
 *  sq_ctx - the pointer to the sq_ctx_t structure.
 *  sq_idx_mask - mask of indexes.
 */
void *get_next_sqe(sq_ctx_t *sq_ctx, uint32_t sq_idx_mask)
{
	return &sq_ctx->sq_ring[sq_ctx->sq_wqe_seg_idx++ & sq_idx_mask];
}
