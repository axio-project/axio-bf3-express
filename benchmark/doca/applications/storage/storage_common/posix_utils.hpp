/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#ifndef APPLICATIONS_STORAGE_STORAGE_COMMON_POSIX_UTILS_HPP_
#define APPLICATIONS_STORAGE_STORAGE_COMMON_POSIX_UTILS_HPP_

#include <cstdint>
#include <string>
#include <thread>

namespace storage::common {

/*
 * Set the affinity of a given thread to a specified core
 *
 * @throws std::runtime_error: if the operation fails
 *
 * @thread [in]: Thread to set affinity for
 * @cpu_core_idx [in]: Core affinity to set
 */
void set_thread_affinity(std::thread &thread, uint32_t cpu_core_idx);

/*
 * Convenience wrapper for strerror_r to make it easier to use
 *
 * @err [in]: posix status code
 * @return: String description of the error
 */
std::string strerror_r(int err) noexcept;

/*
 * Get the system page size
 *
 * @throws std::runtime_error: if the operation fails
 *
 * @return: Page size (in bytes)
 */
uint32_t get_system_page_size(void);

} /* namespace storage::common */

#endif /* APPLICATIONS_STORAGE_STORAGE_COMMON_POSIX_UTILS_HPP_ */