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

#ifndef FLEXIO_WINDOW_COMMON_H_
#define FLEXIO_WINDOW_COMMON_H_

#define HOST_BUFFER_SIZE 512	/* Size of the host buffer */

struct host_to_device_config {
	uint32_t window_id;     /* FlexIO Window ID */
	uint32_t mkey;          /* Memory key for the host data */
	uint64_t haddr;         /* Host address for the buffer */
} __attribute__((__packed__, aligned(8)));

#endif /* FLEXIO_WINDOW_COMMON_H_ */
