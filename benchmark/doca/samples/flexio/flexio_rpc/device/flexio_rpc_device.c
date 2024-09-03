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
#include <libflexio-libc/stdio.h>
#include <libflexio-dev/flexio_dev_debug.h>
#include <libflexio-dev/flexio_dev.h>

/*
 * This is the RPC function that will be called by the host
 *
 * @arg1 [in]: First argument
 * @arg2 [in]: Second argument
 * @arg3 [out]: Third argument
 * @return: Arbitrary calculated value
 */
__dpa_rpc__ uint64_t flexio_rpc_calculate_sum(uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
	return (arg1 << arg2) | arg3;
}


