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
/* Source file for device part of rpc sample.
 * Contain function add arguments, print them and result and
 * return result.
 */

/* Flex IO SDK device side version API header. */
#include <libflexio-dev/flexio_dev_ver.h>

/* Set current version of FLEXIO_DEV_VER_USED. */
#define FLEXIO_DEV_VER_USED FLEXIO_DEV_VER(25, 1, 0)

/* Flex IO SDK device side API header. */
#include <libflexio-dev/flexio_dev.h>

/* Prevent "missing prototype" warning */
uint64_t rpc_calculate(uint64_t arg1, uint64_t arg2);

/* Entry point function that the host side calls for execution.
 *  arg1 and arg2 are arguments passed from the host side using
 * the flexio_process_call function.
 */
__dpa_rpc__ uint64_t rpc_calculate(uint64_t arg1, uint64_t arg2)
{
	uint64_t res;

	res = arg1 + arg2;

	flexio_dev_print("Calculate: %lu + %lu = %lu\n", arg1, arg2, res);

	return res;
}
