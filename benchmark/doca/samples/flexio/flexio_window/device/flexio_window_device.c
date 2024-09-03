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

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-libc/stdio.h>

#include "../flexio_window_common.h"

/*
 * Copy the first n bytes from src to dst.
 *
 * @dst [in]: Destination buffer
 * @src [in]: Source buffer
 * @n [in]: Number of bytes to copy
 */
static void
strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		dst[i] = src[i];
		if (src[i] == '\0')
			break;
	}
}

/*
 * This is the RPC function that will be called by the host
 *
 * @arg1 [in]: Pointer to device config set by the host application
 * @return: 0 on success negative value on failure.
 */
__dpa_rpc__ uint64_t flexio_window_rpc(uint64_t arg1)
{
	struct host_to_device_config *device_cfg;
	struct flexio_dev_thread_ctx *dtctx;
	char *host_buffer;
	flexio_dev_status_t result;

	device_cfg = (struct host_to_device_config *) arg1;
	if (flexio_dev_get_thread_ctx(&dtctx) < 0)
		return -1;

	/* Configure FlexIO Window */
	result = flexio_dev_window_config(dtctx, device_cfg->window_id, device_cfg->mkey);
	if (result != FLEXIO_DEV_STATUS_SUCCESS)
		return -1;

	/* Acquire device pointer to host memory */
	result = flexio_dev_window_ptr_acquire(dtctx, device_cfg->haddr, (flexio_uintptr_t *)&host_buffer);
	if (result != FLEXIO_DEV_STATUS_SUCCESS)
		return -1;

	/* Write directly to host memory */
	strncpy(host_buffer, "FlexIO Window Sample - DPA", HOST_BUFFER_SIZE);

	return 0;
}
