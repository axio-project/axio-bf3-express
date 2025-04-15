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

/**
 * @file flexio_dev_err.h
 * @page FlexIODevErr Flex IO SDK dev error handling
 * @defgroup FlexioSDKDevErr Flex IO SDK dev error handling
 * Flex IO SDK device API for DPA programs error handling.
 * @ingroup FlexioSDKDev
 *
 * @{
 */

#ifndef _FLEXIO_DEV_ERR_H_
#define _FLEXIO_DEV_ERR_H_

/** Flex IO dev errors. */
typedef enum flexio_dev_errors {
	FLEXIO_DEV_ERROR_ILLEGAL_ERR = 0x42, /**< Illegal user error code */
} flexio_dev_error_t;

/**
 * @brief Exit the process and return a user (fatal) error code
 *
 * Error codes returned to the host in the dpa_process_status field of the DPA_PROCESS
 * object are defined as follows:
 * 0:       OK
 * 1-63:    RTOS or Firmware errors
 * 64-127:  Flexio-SDK errors
 * 128-255: User defined
 *
 * @param[in] error - A user defined error in the range of 0x80 (128) to 0xFF (255)
 *
 * @return - function does not return
 */
FLEXIO_DEV_EXPERIMENTAL __attribute__((__noreturn__)) void flexio_dev_error(uint8_t error);

/**
 * @brief Get thread error flag (errno) of recoverable (non fatal) error.
 *
 * This function queries an errno field from thread context.
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return thread error code.
 */
FLEXIO_DEV_EXPERIMENTAL uint64_t flexio_dev_get_errno(struct flexio_dev_thread_ctx *dtctx);

/**
 * @brief Reset thread error flag (errno) of recoverable (non fatal) error.
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return - void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_rst_errno(struct flexio_dev_thread_ctx *dtctx);

/**
 * @brief Get and Reset thread error flag (errno) of recoverable (non fatal) error.
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return - thread error code.
 */
FLEXIO_DEV_EXPERIMENTAL uint64_t flexio_dev_get_and_rst_errno(struct flexio_dev_thread_ctx *dtctx);

/** @} */

#endif /* _FLEXIO_DEV_ERR_H_ */
