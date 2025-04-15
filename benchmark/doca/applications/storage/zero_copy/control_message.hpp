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

#ifndef APPLICATIONS_STORAGE_ZERO_COPY_CONTROL_MESSAGE_HPP_
#define APPLICATIONS_STORAGE_ZERO_COPY_CONTROL_MESSAGE_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include <doca_error.h>
#include <doca_types.h>

#include <storage_common/definitions.hpp>
#include <storage_common/tcp_socket.hpp>

#include <zero_copy/definitions.hpp>

namespace storage::zero_copy {

/*
 * Expected maximum size of a control message
 */
auto constexpr control_message_buffer_size = 1024;

/*
 * Set of available message types
 */
enum class control_message_type : uint16_t {
	response,
	configure_data_path,
	create_rdma_connection_request,
	create_rdma_connection_response,
	start_data_path_connections,
	start_storage,
	destroy_objects,
};

/*
 * Structure of a control message
 */
struct control_message {
	/* Abstract details as they vary based on message type */
	struct message_details {
		virtual ~message_details() = default;
	};

	uint32_t correlation_id;   /* A correlation id to allow the user to match responses with requests */
	uint16_t wire_size;	   /* The full wire size this message so the receiver can know it has all the data */
	control_message_type type; /* Type of message */
	std::unique_ptr<message_details> details; /* Type dependant additional details */

	/*
	 * Response message details
	 */
	struct response : public message_details {
		doca_error_t result; /* Result code */
		std::string message; /* A message to give the caller some context / understanding of the error */
	};

	/*
	 * Configure data path message details
	 */
	struct configure_data_path : public message_details {
		uint32_t buffer_count;		       /* Number of buffers to use */
		uint32_t buffer_size;		       /* Size of buffer to use */
		uint32_t batch_size;		       /* Batch size to use */
		std::vector<uint8_t> mmap_export_blob; /* Initiator memory details to create a mmap from */
	};

	/*
	 * Create RDMA connection request message details
	 */
	struct create_rdma_connection_request : public message_details {
		zero_copy::rdma_connection_role role;	 /* The Role the RDMA connection will play */
		std::vector<uint8_t> connection_details; /* Sender connection details */
	};

	/*
	 * Create RDMA connection response message details (Failures will return response instead of this type)
	 */
	struct create_rdma_connection_response : public message_details {
		zero_copy::rdma_connection_role role;	 /* The Role the RDMA connection will play */
		std::vector<uint8_t> connection_details; /* Responder connection details */
	};
};

/*
 * Helper function to produce a populated success response message
 *
 * @correlation_id [in]: Correlation id to use
 * @return populated message
 */
control_message make_response_control_message(uint32_t correlation_id);

/*
 * Helper function to produce a populated error response message
 *
 * @correlation_id [in]: Correlation id to use
 * @result [in]: Error code
 * @message [in]: Description of the error
 * @return populated message
 */
control_message make_response_control_message(uint32_t correlation_id, doca_error_t result, std::string message);

/*
 * Helper function to produce a configure data path message
 *
 * @correlation_id [in]: Correlation id to use
 * @buffer_count [in]: Number of data buffers
 * @buffer_size [in]: Size of buffers
 * @batch_size [in]: Batch size
 * @mmap_export_blob [in]: Initiator memory details to create a mmap from
 * @return populated message
 */
control_message make_configure_data_path_control_message(uint32_t correlation_id,
							 uint32_t buffer_count,
							 uint32_t buffer_size,
							 uint32_t batch_size,
							 std::vector<uint8_t> mmap_export_blob);

/*
 * Helper function to produce a start storage message
 *
 * @correlation_id [in]: Correlation id to use
 * @return populated message
 */
control_message make_start_storage_control_message(uint32_t correlation_id);

/*
 * Helper function to produce a destroy objects message
 *
 * @correlation_id [in]: Correlation id to use
 * @return populated message
 */
control_message make_destroy_objects_control_message(uint32_t correlation_id);

/*
 * Helper function to produce a create rdma connection request message
 *
 * @correlation_id [in]: Correlation id to use
 * @role [in]: Role for this connection
 * @connection_details [in]: Sender connection details
 * @return populated message
 */
control_message make_create_rdma_connection_request_control_message(uint32_t correlation_id,
								    zero_copy::rdma_connection_role role,
								    std::vector<uint8_t> connection_details);

/*
 * Helper function to produce a create rdma connection response message
 *
 * @correlation_id [in]: Correlation id to use
 * @role [in]: Role for this connection
 * @connection_details [in]: Responder connection details
 * @return populated message
 */
control_message make_create_rdma_connection_response_control_message(uint32_t correlation_id,
								     zero_copy::rdma_connection_role role,
								     std::vector<uint8_t> connection_details);

/*
 * Helper function to produce a start data path connections message
 *
 * @correlation_id [in]: Correlation id to use
 * @return populated message
 */
control_message make_start_data_path_connections_control_message(uint32_t correlation_id);

/*
 * Format a control_message as a binary buffer suitable for transfer via TCP, comch, rdma, etc
 *
 * @message [in]: Message to format
 * @buffer [in,out]: buffer to write formatted message into
 * @buffer_capacity [in]: Number of bytes available for use in buffer
 * @return: number of bytes written to buffer or 0 if the buffer was not big enough
 */
uint32_t to_buffer(zero_copy::control_message const &message, char *buffer, uint32_t buffer_capacity) noexcept;

/*
 * Extract a control_message from a binary buffer
 *
 * @buffer [in]: buffer to read formatted message from
 * @buffer_size [in]: Number of bytes remaining in buffer
 * @message [out]: Message extracted from buffer
 * @return: number of bytes read from buffer or 0 if the buffer was not big enough
 */
uint32_t from_buffer(char const *buffer, uint32_t buffer_size, zero_copy::control_message &message) noexcept;

/*
 * Convert a control_message to a string a user can read
 *
 * @message [in]: Message to convert
 * @return: User readable string
 */
std::string to_string(zero_copy::control_message const &message);

/*
 * A context that can be used to reassemble fragmented control messages
 */
struct control_message_reassembler {
	std::vector<char> recombined_buffer{}; /* Storage for combined bytes */
	uint16_t message_byte_count = 0; /* Expected size of the current message or 0 if recombined_buffer is empty */

	/*
	 * Append bytes to the recombined message
	 *
	 * @fragment [in]: New bytes
	 * @fragment_size [in]: Number of new bytes
	 * @return: true if a full message is now held false otherwise.
	 */
	bool append(char const *fragment, uint32_t fragment_size);

	/*
	 * Extract a message
	 *
	 * @throws std::runtime_error: if no message could be extracted
	 * @return: Extracted message
	 */
	zero_copy::control_message extract_message(void);
};

} /* namespace storage::zero_copy */

#endif /* APPLICATIONS_STORAGE_ZERO_COPY_CONTROL_MESSAGE_HPP_ */