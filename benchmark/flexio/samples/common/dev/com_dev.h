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

/* Header file with utilities for DPA.
 */

#ifndef __COM_DEV_H__
#define __COM_DEV_H__

/* Flex IO SDK device side version API header. */
#include <libflexio-dev/flexio_dev_ver.h>

/* Set current version of FLEXIO_DEV_VER_USED. */
#define FLEXIO_DEV_VER_USED FLEXIO_DEV_VER(25, 1, 0)

/* Flex IO SDK device side API header. */
#include <libflexio-dev/flexio_dev.h>

#ifndef NULL
#define NULL (void *)0
#endif

#define STUCK() \
	do { \
		flexio_dev_thread_reschedule(); \
	} while (1)

/* Convert logarithm to value */
#define L2V(l) (1UL << (l))
/* Convert logarithm to mask */
#define L2M(l) (L2V(l) - 1)

#define CQE_OPCODE_REQUESTER 0x0

/* Sample DPA CQ metadata structure.
 * Contains all data needed to DPA to work with CQ.
 */
typedef struct {
	uint32_t cq_number;
	struct flexio_dev_cqe64 *cq_ring, *cqe;
	uint32_t cq_idx;
	uint8_t cq_hw_owner_bit;
	uint32_t *cq_dbr;
	uint32_t log_cq_depth;
} cq_ctx_t;

/* Sample DPA RQ metadata structure.
 * Contains all data needed to DPA to work with RQ.
 */
typedef struct {
	uint32_t rq_number;
	struct flexio_dev_wqe_rcv_data_seg *rq_ring;
	uint32_t *rq_dbr;
} rq_ctx_t;

/* Sample DPA SQ metadata structure.
 * Contains all data needed to DPA to work with SQ.
 */
typedef struct {
	uint32_t sq_number;
	union flexio_dev_sqe_seg *sq_ring;
	uint32_t sq_wqe_seg_idx;
	uint32_t sq_pi;
} sq_ctx_t;

/* Sample DPA send metadata structure.
 * Contains SQ ring and data buffer for sending.
 */
typedef struct {
	void *sq_tx_buff;
	uint32_t tx_buff_idx;
} dt_ctx_t;

/* Sample DPA EQ metadata structure.
 * Contains all data needed to DPA to work with EQ.
 */
typedef struct {
	uint32_t eq_number;
	struct flexio_dev_eqe *eq_ring, *eqe;
	uint32_t eq_idx;
	uint8_t eq_hw_owner_bit;
} eq_ctx_t;

/* Fill the cq_ctx_t structure with input parameters
 *  cq_ctx - structure to be filled
 *  num - cq_number
 *  log_depth - CQ log depth
 *  ring_addr - address of the ring
 *  dbr_addr - address of the DBR
 */
void com_cq_ctx_init(cq_ctx_t *cq_ctx, uint32_t num, uint32_t log_depth,
		     flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr);

/* Fill the rq_ctx_t structure with input parameters
 *  rq_ctx - structure to be filled
 *  num - rq_number
 *  ring_addr - address of the ring
 *  dbr_addr - address of the DBR
 */
void com_rq_ctx_init(rq_ctx_t *rq_ctx, uint32_t num, flexio_uintptr_t ring_addr,
		     flexio_uintptr_t dbr_addr);

/* Fill the sq_ctx_t structure with input parameters
 *  sq_ctx - structure to be filled
 *  num - sq_number
 *  ring_addr - address of the ring
 */
void com_sq_ctx_init(sq_ctx_t *sq_ctx, uint32_t num, flexio_uintptr_t ring_addr);

/* Fill the eq_ctx_t structure with input parameters
 *  eq_ctx - structure to be filled
 *  num - sq_number
 *  ring_addr - address of the ring
 */
void com_eq_ctx_init(eq_ctx_t *eq_ctx, uint32_t num, flexio_uintptr_t ring_addr);

/* Fill structure dt_ctx_t with input parameters
 *  dt_ctx - structure to be filled
 *  buff_addr - address of the buffer
 */
void com_dt_ctx_init(dt_ctx_t *dt_ctx, flexio_uintptr_t buff_addr);

/* Update the cq_ctx_t structure and set the DBR for the CQE.
 *  cq_ctx - pointer to cq_ctx_t structure to update
 */
void com_step_cq(cq_ctx_t *cq_ctx);

/* Get the pointer to the next data entry.
 *  dt_ctx - the pointer to the dt_ctx_t structure.
 *  dt_idx_mask - mask of indexes.
 *  log_dt_entry_sz - logarithm size of the data entry
 */
void *get_next_dte(dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz);

/* Get the pointer to the next SQ entry.
 *  sq_ctx - the pointer to the sq_ctx_t structure.
 *  sq_idx_mask - mask of indexes.
 */
void *get_next_sqe(sq_ctx_t *sq_ctx, uint32_t sq_idx_mask);

/* Update the eq_ctx_t structure and EQ with no DBR.
 *  eq_ctx - pointer to the eq_ctx_t structure.
 *  eq_idx_mask - mask of indexes.
 */
void com_step_eq(eq_ctx_t *eq_ctx, uint32_t eq_idx_mask);

/* Swap source and destination MAC in the packet.
 *  packet - pointer to the packet.
 */
void swap_macs(char *packet);

#endif
