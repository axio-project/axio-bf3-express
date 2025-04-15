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

#ifndef APPLICATIONS_STORAGE_STORAGE_COMMON_TCP_SOCKET_HPP_
#define APPLICATIONS_STORAGE_STORAGE_COMMON_TCP_SOCKET_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <storage_common/ip_address.hpp>

namespace storage::common {

/*
 * TCP socket helper class
 */
class tcp_socket {
public:
	/*
	 * TCP connection status codes
	 */
	enum class connection_status {
		connected = 0, /* Connected */
		establishing,  /* Trying to connect but not done yet */
		refused,       /* Remote refused the connection */
		failed,	       /* Connection failed for some reason */
	};

	/*
	 * Destroy and disconnect the socket
	 */
	~tcp_socket();

	/*
	 * Create a default (invalid) socket
	 */
	tcp_socket();

	/*
	 * Create a socket from a given fd number
	 *
	 * @fd [in]: Underlying socket fd number to use
	 *
	 * @throws std::runtime_error: If unable to set socket options
	 */
	explicit tcp_socket(uint32_t fd);

	/*
	 * Disabled copy constructor
	 */
	tcp_socket(tcp_socket const &) = delete;

	/*
	 * Move constructor
	 *
	 * @other [in]: Object to move from
	 */
	tcp_socket(tcp_socket &&other) noexcept;

	/*
	 * Disabled copy assignment operator
	 */
	tcp_socket &operator=(tcp_socket const &) = delete;

	/*
	 * Move assignment operator
	 *
	 * @other [in]: Object to move from
	 * @return: Reference to assigned object
	 */
	tcp_socket &operator=(tcp_socket &&other) noexcept;

	/*
	 * Set the socket to blocking or non-blocking
	 *
	 * @blocking [in]: Set blocking(true) or non-blocking(false)
	 */
	void set_blocking(bool blocking);

	/*
	 * Close the socket
	 */
	void close(void);

	/*
	 * Connect the socket to a server at the given address
	 *
	 * @address [in]: Address to connect to
	 */
	void connect(storage::common::ip_address const &address);

	/*
	 * Listen for connections on the specified port
	 *
	 * @port [in]: Port to listen on
	 */
	void listen(uint16_t port);

	/*
	 * Pool and check if the socket is fully connected
	 *
	 * @return: The current connection status
	 */
	connection_status poll_is_connected(void);

	/*
	 * Start accepting incoming connections
	 *
	 * @return: A tcp_socket object. The user must check using is_valid to know if the returned socket is connected
	 * to anything or not
	 */
	tcp_socket accept(void);

	/*
	 * Write bytes to the socket
	 *
	 * @buffer [in]: Pointer to an array of bytes
	 * @byte_count [in]: The number of bytes to write from the buffer pointed to by buffer
	 *
	 * @return: The number of bytes written to the socket
	 */
	size_t write(char const *buffer, size_t byte_count);

	/*
	 * Read bytes from the socket
	 *
	 * @param [in] buffer pointer to an array of bytes
	 * @param [in] buffer_capacity number of bytes available in the array pointed to by buffer
	 *
	 * @return: The number of valid bytes placed into the array pointed to by buffer
	 */
	size_t read(char *buffer, size_t buffer_capacity);

	/*
	 * Check if the socket is valid or not, invalid sockets can be simply disposed of at negligible cost
	 *
	 * @return: true if socket is valid (refers to something) otherwise false
	 */
	bool is_valid(void) const noexcept;

private:
	uint32_t m_fd; /* internal socket number */

	/*
	 * Set socket options
	 */
	void set_socket_options(void);
};

} /* namespace storage::common */

#endif /* APPLICATIONS_STORAGE_STORAGE_COMMON_TCP_SOCKET_HPP_ */
