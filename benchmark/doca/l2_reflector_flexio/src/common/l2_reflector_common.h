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

#ifndef L2_REFLECTOR_COMMON_H_
#define L2_REFLECTOR_COMMON_H_


/* Logarithm ring size */
#define DPA_LOG_SQ_RING_DEPTH 7 /* 2^7 entries */
#define DPA_LOG_RQ_RING_DEPTH 7 /* 2^7 entries */
#define DPA_LOG_CQ_RING_DEPTH 7 /* 2^7 entries */

#define DPA_LOG_WQ_DATA_ENTRY_BSIZE 11 /* WQ buffer logarithmic size */

/* Queues index mask, represents the index of the last CQE/WQE in the queue */
#define DPA_CQ_IDX_MASK ((1 << DPA_LOG_CQ_RING_DEPTH) - 1)
#define DPA_RQ_IDX_MASK ((1 << DPA_LOG_RQ_RING_DEPTH) - 1)
#define DPA_SQ_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
#define DPA_DATA_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH)) - 1)

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
	struct app_transfer_wq rq_data;	/* device RQ */
	struct app_transfer_cq sq_cq_data; /* device SQ's CQ */
	struct app_transfer_wq sq_data;	/* device SQ */
} __attribute__((__packed__, aligned(8)));

#endif
