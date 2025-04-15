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
/* Shared header file with defines and structures that used in
 * both host and device sides samples.
 */

#ifndef __FLEXIO_PACKET_PROCESSOR_COM_H__
#define __FLEXIO_PACKET_PROCESSOR_COM_H__

#include <stdint.h>

/* Depth of CQ is (1 << LOG_CQ_DEPTH) */
#define LOG_CQ_DEPTH 7
/* Depth of RQ is (1 << LOG_RQ_DEPTH) */
#define LOG_RQ_DEPTH 7
/* Depth of SQ is (1 << LOG_SQ_DEPTH) */
#define LOG_SQ_DEPTH 7

/* Size of WQD is (1 << LOG_WQD_CHUNK_BSIZE) */
#define LOG_WQD_CHUNK_BSIZE 11

/* Structure for transfer CQ data */
struct app_transfer_cq {
	/* CQ number */
	uint32_t cq_num;
	/* Depth of CQ in the logarithm */
	uint32_t log_cq_depth;
	/* CQ ring DPA address */
	flexio_uintptr_t cq_ring_daddr;
	/* CQ DBR DPA address */
	flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));

/* Structure for transfer WQ data */
struct app_transfer_wq {
	/* WQ number */
	uint32_t wq_num;
	/* WQ MKEY Id */
	uint32_t wqd_mkey_id;
	/* WQ ring DPA address */
	flexio_uintptr_t wq_ring_daddr;
	/* WQ ring DBR address */
	flexio_uintptr_t wq_dbr_daddr;
	/* WQ data address */
	flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));

/* Collateral structure for transfer host data to device */
struct host2dev_packet_processor_data {
	/* RQ's CQ transfer information. */
	struct app_transfer_cq rq_cq_transf;
	/* RQ transfer information. */
	struct app_transfer_wq rq_transf;
	/* SQ's CQ transfer information. */
	struct app_transfer_cq sq_cq_transf;
	/* SQ transfer information. */
	struct app_transfer_wq sq_transf;
	uint8_t not_first_run;
} __attribute__((__packed__, aligned(8)));

#endif /* __FLEXIO_PACKET_PROCESSOR_COM_H__ */
