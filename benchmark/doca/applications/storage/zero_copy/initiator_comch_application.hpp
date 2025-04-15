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

#ifndef APPLICATIONS_STORAGE_ZERO_COPY_INITIATOR_COMCH_APPLICATION_HPP_
#define APPLICATIONS_STORAGE_ZERO_COPY_INITIATOR_COMCH_APPLICATION_HPP_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <storage_common/definitions.hpp>

namespace storage::zero_copy {

class initiator_comch_application {
public:
	/*
	 * Application configuration
	 */
	struct configuration {
		std::string device_id;
		std::string operation_type;
		std::string command_channel_name;
		uint32_t buffer_size;
		uint32_t buffer_count;
		uint32_t run_limit_operation_count;
		uint32_t batch_size;
		std::vector<uint32_t> cpu_set;
		bool validate_writes;
		std::chrono::seconds control_timeout;
	};

	static_assert(sizeof(bool) == 1, "Expected bool to occupy one byte");

	/*
	 * Application stats
	 */
	struct stats {
		std::chrono::microseconds duration;
		uint32_t operation_count;
		uint32_t latency_min;
		uint32_t latency_max;
		uint32_t latency_mean;
		uint64_t pe_hit_count;
		uint64_t pe_miss_count;
	};

	/*
	 * Default destructor
	 */
	virtual ~initiator_comch_application() = default;

	/*
	 * Run the application
	 *
	 * @return: true on success and false otherwise
	 */
	virtual bool run(void) = 0;

	/*
	 * Abort execution
	 *
	 * @reason [in]: The reason
	 */
	virtual void abort(std::string const &reason) = 0;

	/*
	 * Get end of run statistics
	 *
	 * @return: Statistics
	 */
	virtual storage::zero_copy::initiator_comch_application::stats get_stats(void) const noexcept = 0;
};

/*
 * Create a ComCh initiator application instance
 *
 * @throws std::bad_alloc if memory allocation fails
 * @throws std::runtime_error if any other error occurs
 *
 * @cfg [in]: Application configuration
 * @return: Application instance
 */
storage::zero_copy::initiator_comch_application *make_initiator_comch_application(
	const storage::zero_copy::initiator_comch_application::configuration &cfg);

} /* namespace storage::zero_copy */

#endif /* APPLICATIONS_STORAGE_ZERO_COPY_INITIATOR_COMCH_APPLICATION_HPP_ */
