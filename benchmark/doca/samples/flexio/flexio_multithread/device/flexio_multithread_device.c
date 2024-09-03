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

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>

#include "../flexio_multithread_common.h"

flexio_dev_async_rpc_handler_t flexio_multithread_rpc;

/*
 * This is the RPC function that will be called by the host
 *
 * @arg1 [in]: Pointer to device config set by the host application
 */
__dpa_global__ void flexio_multithread_rpc(uint64_t arg1)
{
	struct host_to_device_config *device_cfg;
	struct flexio_dev_thread_ctx *dtctx;
	int32_t *mat_c = NULL;
	int32_t *mat_a, *mat_b;
	flexio_dev_status_t result;

	device_cfg = (struct host_to_device_config *) arg1;
	if (flexio_dev_get_thread_ctx(&dtctx) < 0)
		return;

	mat_a = (int *)device_cfg->mat_a_daddr;
	mat_b = (int *)device_cfg->mat_b_daddr;

	/* Configure FlexIO Window */
	result = flexio_dev_window_config(dtctx, device_cfg->window_id, device_cfg->mkey);
	if (result != FLEXIO_DEV_STATUS_SUCCESS)
		return;

	/* Acquire device pointer to host memory */
	result = flexio_dev_window_ptr_acquire(dtctx, device_cfg->haddr, ((flexio_uintptr_t *) &mat_c));
	if (result != FLEXIO_DEV_STATUS_SUCCESS)
		return;

	for (int i = 0; i < N; i++) {
		int a = mat_a[device_cfg->row*N + i];
		int b = mat_b[i*N + device_cfg->col];

		mat_c[device_cfg->row*N + device_cfg->col] += a*b;
	}
}
