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

#ifndef APPLICATIONS_STORAGE_STORAGE_COMMON_IP_ADDRESS_HPP_
#define APPLICATIONS_STORAGE_STORAGE_COMMON_IP_ADDRESS_HPP_

#include <cstdint>
#include <string>

namespace storage::common {

/*
 * Helper class to hold an ip address and port pair
 */
class ip_address {
public:
	/*
	 * Default destructor
	 */
	~ip_address() = default;

	/*
	 * Default constructor
	 */
	ip_address() = default;

	/*
	 * Construct an ip address from the given values
	 *
	 * @throws std::bad_alloc: If memory cannot be allocated
	 *
	 * @value [in]: String to be parsed into an ip address and port
	 */
	ip_address(std::string address, uint16_t port);

	/*
	 * Copy constructor
	 *
	 * @throws std::bad_alloc: if memory cannot be allocated
	 */
	ip_address(ip_address const &) = default;

	/*
	 * Move constructor
	 */
	ip_address(ip_address &&) noexcept = default;

	/*
	 * Copy Assignment operator
	 *
	 * @throws std::bad_alloc: if memory cannot be allocated
	 */
	ip_address &operator=(ip_address const &) = default;

	/*
	 * Move Assignment operator
	 */
	ip_address &operator=(ip_address &&) noexcept = default;

	/*
	 * Get the IP address
	 *
	 * @return: IP address
	 */
	std::string const &get_address() const noexcept;

	/*
	 * Get the port number
	 *
	 * @return: port number
	 */
	uint16_t get_port() const noexcept;

private:
	std::string m_addr{}; // IP address
	uint16_t m_port{};    // Port number
};

/*
 * Parse an IP address and port from the given string representation
 *
 * @value [in]: the string to be parsed
 * @return: The parsed IP address and port
 *
 * @throws std::runtime_error: if the provided string cannot be parsed
 */
ip_address parse_ip_v4_address(char const *value);

} /* namespace storage::common */

#endif /* APPLICATIONS_STORAGE_STORAGE_COMMON_IP_ADDRESS_HPP_ */
