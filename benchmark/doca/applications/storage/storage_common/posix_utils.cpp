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

#include <storage_common/posix_utils.hpp>

#include <cstring>
#include <stdexcept>

#include <sched.h>
#include <pthread.h>
#include <unistd.h>

namespace storage::common {

void set_thread_affinity(std::thread &thread, uint32_t cpu_core_idx)
{
	cpu_set_t actual_cpuset;
	cpu_set_t desired_cpuset;
	int ret;

	CPU_ZERO(&actual_cpuset);
	CPU_ZERO(&desired_cpuset);
	CPU_SET(cpu_core_idx, &desired_cpuset);

	ret = pthread_setaffinity_np(thread.native_handle(), sizeof(desired_cpuset), &desired_cpuset);
	if (ret != 0) {
		throw std::runtime_error{"Unable to set thread affinity to cpu " + std::to_string(cpu_core_idx) +
					 ". Underlying error: " + storage::common::strerror_r(ret)};
	}

	ret = pthread_getaffinity_np(thread.native_handle(), sizeof(actual_cpuset), &actual_cpuset);
	if (ret != 0) {
		throw std::runtime_error{"Unable to retrieve thread affinity to verify usage of cpu " +
					 std::to_string(cpu_core_idx) +
					 ". Underlying error: " + storage::common::strerror_r(ret)};
	}

	for (size_t ii = 0; ii != CPU_SETSIZE; ++ii) {
		if (CPU_ISSET(ii, &actual_cpuset) && CPU_ISSET(ii, &desired_cpuset)) {
			return;
		}
	}

	throw std::runtime_error{"Set affinity to cpu " + std::to_string(cpu_core_idx) +
				 ". Failed to apply successfully"};
}

std::string strerror_r(int err) noexcept
{
	char buffer[128];
	return ::strerror_r(err, buffer, 128);
}

uint32_t get_system_page_size(void)
{
	auto const result = sysconf(_SC_PAGESIZE);
	if (result < 0) {
		throw std::runtime_error{"Failed to get system page size. Error: " + strerror_r(errno)};
	}

	return static_cast<uint32_t>(result);
}

} /* namespace storage::common */