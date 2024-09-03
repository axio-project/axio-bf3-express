/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef FLEXIO_MULTITHREAD_COMMON_H_
#define FLEXIO_MULTITHREAD_COMMON_H_

#define N (5)				/* Matrix size (N*N) */

struct host_to_device_config {
	uint32_t window_id;		/* FlexIO Window ID */
	uint32_t mkey;			/* Memory key for the result matrix */
	uint64_t haddr;			/* Host address for the result matrix */
	uint32_t row;			/* Row index */
	uint32_t col;			/* Column index */
	flexio_uintptr_t mat_a_daddr;	/* Pointer to the first matrix */
	flexio_uintptr_t mat_b_daddr;	/* Pointer to the second matrix */
} __attribute__((__packed__, aligned(8)));

#endif /* FLEXIO_MULTITHREAD_COMMON_H_ */
