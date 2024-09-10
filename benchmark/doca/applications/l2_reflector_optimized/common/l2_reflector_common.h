/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#ifndef L2_REFLECTOR_COMMON_H_
#define L2_REFLECTOR_COMMON_H_

/* Logarithm ring size */
#define L2_LOG_SQ_RING_DEPTH 9 /* 2^7 entries */
#define L2_LOG_RQ_RING_DEPTH 9 /* 2^7 entries */
#define L2_LOG_CQ_RING_DEPTH 9 /* 2^7 entries */

#define L2_LOG_WQ_DATA_ENTRY_BSIZE 11 /* WQ buffer logarithmic size */

/* Queues index mask, represents the index of the last CQE/WQE in the queue */
#define L2_CQ_IDX_MASK ((1 << L2_LOG_CQ_RING_DEPTH) - 1)
#define L2_RQ_IDX_MASK ((1 << L2_LOG_RQ_RING_DEPTH) - 1)
#define L2_SQ_IDX_MASK ((1 << (L2_LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
#define L2_DATA_IDX_MASK ((1 << (L2_LOG_SQ_RING_DEPTH)) - 1)

struct app_transfer_cq {
	uint32_t cq_num;
	uint32_t log_cq_depth;
	flexio_uintptr_t cq_ring_daddr;
	flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));

struct app_transfer_wq {
	uint32_t wq_num;
	uint32_t wqd_mkey_id;
	flexio_uintptr_t wq_ring_daddr;
	flexio_uintptr_t wq_dbr_daddr;
	flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));

/* Transport data from HOST application to DEV application */
struct l2_reflector_data {
	struct app_transfer_cq rq_cq_data; /* device RQ's CQ */
	struct app_transfer_wq rq_data;	   /* device RQ */
	struct app_transfer_cq sq_cq_data; /* device SQ's CQ */
	struct app_transfer_wq sq_data;	   /* device SQ */
} __attribute__((__packed__, aligned(8)));

#endif
