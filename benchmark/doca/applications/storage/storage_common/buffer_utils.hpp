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

#ifndef APPLICATIONS_STORAGE_STORAGE_COMMON_MESSAGE_UTILS_HPP_
#define APPLICATIONS_STORAGE_STORAGE_COMMON_MESSAGE_UTILS_HPP_

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <endian.h>

namespace storage::common {

/*
 * Emplace an uint8_t value into a byte array
 *
 * @buffer [in]: buffer storage
 * @value [in]: value to set
 * @return: Pointer to next byte after the written value
 */
inline char *to_buffer(char *buffer, uint8_t value)
{
	*buffer = static_cast<char>(value);
	return buffer + sizeof(value);
}

/*
 * Emplace an uint16_t value as a big endian byte order value into a byte array
 *
 * @buffer [in]: buffer storage
 * @value [in]: value to set
 * @return: Pointer to next byte after the written value
 */
inline char *to_buffer(char *buffer, uint16_t value)
{
	value = htobe16(value);
	std::copy(reinterpret_cast<char const *>(&value),
		  reinterpret_cast<char const *>(&value) + sizeof(value),
		  buffer);
	return buffer + sizeof(value);
}

/*
 * Emplace an uint32_t value as a big endian byte order value into a byte array
 *
 * @buffer [in]: buffer storage
 * @value [in]: value to set
 * @return: Pointer to next byte after the written value
 */
inline char *to_buffer(char *buffer, uint32_t value) noexcept
{
	value = htobe32(value);
	std::copy(reinterpret_cast<char const *>(&value),
		  reinterpret_cast<char const *>(&value) + sizeof(value),
		  buffer);
	return buffer + sizeof(value);
}

/*
 * Emplace an uint64_t value as a big endian byte order value into a byte array
 *
 * @buffer [in]: buffer storage
 * @value [in]: value to set
 * @return: Pointer to next byte after the written value
 */
inline char *to_buffer(char *buffer, uint64_t value) noexcept
{
	value = htobe64(value);
	std::copy(reinterpret_cast<char const *>(&value),
		  reinterpret_cast<char const *>(&value) + sizeof(value),
		  buffer);
	return buffer + sizeof(value);
}

/*
 * Emplace a std::string into a byte array
 *
 * @buffer [in]: buffer storage
 * @value [in]: value to set
 * @return: Pointer to next byte after the written value
 */
inline char *to_buffer(char *buffer, std::string const &value) noexcept
{
	buffer = to_buffer(buffer, static_cast<uint32_t>(value.size()));
	std::copy(value.data(), value.data() + value.size(), buffer);

	return buffer + value.size();
}

/*
 * Emplace a std::vector<uint8_t> into a byte array
 *
 * @buffer [in]: buffer to read from
 * @value [out]: Extracted value in the native byte ordering
 * @return: Pointer to next byte after the read value
 */
inline char *to_buffer(char *buffer, std::vector<uint8_t> const &value) noexcept
{
	buffer = to_buffer(buffer, static_cast<uint32_t>(value.size()));
	std::copy(value.data(), value.data() + value.size(), buffer);

	return buffer + value.size();
}

/*
 * Function template for decode functions
 *
 * @buffer [in]: buffer to read from
 * @value [out]: Extracted value in the native byte ordering
 * @return: Pointer to next byte after the read value
 */
template <typename T>
inline char const *from_buffer(char const *buffer, T &value);

/*
 * Complete specialization of from_buffer that extracts a uint8_t from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @value [out]: Extracted value in the native byte ordering
 * @return: Pointer to next byte after the read value
 */
template <>
inline char const *from_buffer(char const *buffer, uint8_t &value)
{
	value = *buffer;
	return buffer + sizeof(value);
}

/*
 * Complete specialization of from_buffer that extracts a uint16_t from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @return: Extracted value in the native byte ordering
 */
template <>
inline char const *from_buffer(char const *buffer, uint16_t &value)
{
	std::copy(buffer, buffer + sizeof(value), reinterpret_cast<char *>(&value));
	value = be16toh(value);
	return buffer + sizeof(value);
}

/*
 * Complete specialization of from_buffer that extracts a uint32_t from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @return: Extracted value in the native byte ordering
 */
template <>
inline char const *from_buffer(char const *buffer, uint32_t &value)
{
	std::copy(buffer, buffer + sizeof(value), reinterpret_cast<char *>(&value));
	value = be32toh(value);
	return buffer + sizeof(value);
}

/*
 * Complete specialization of from_buffer that extracts a uint64_t from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @return: Extracted value in the native byte ordering
 */
template <>
inline char const *from_buffer(char const *buffer, uint64_t &value)
{
	std::copy(buffer, buffer + sizeof(value), reinterpret_cast<char *>(&value));
	value = be64toh(value);
	return buffer + sizeof(value);
}

/*
 * Complete specialization of from_buffer that extracts a std::string from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @return: Extracted string
 */
template <>
inline char const *from_buffer(char const *buffer, std::string &value)
{
	uint32_t byte_count = 0;
	buffer = from_buffer(buffer, byte_count);
	value = std::string{buffer, buffer + byte_count};
	return buffer + byte_count;
}

/*
 * Complete specialization of from_buffer that extracts a std::vector<uint8_t> from the given buffer
 *
 * @buffer [in]: buffer to read from
 * @return: Extracted value in the native byte ordering
 */
template <>
inline char const *from_buffer(char const *buffer, std::vector<uint8_t> &value)
{
	uint32_t byte_count = 0;
	buffer = from_buffer(buffer, byte_count);
	value = std::vector<uint8_t>{reinterpret_cast<uint8_t const *>(buffer),
				     reinterpret_cast<uint8_t const *>(buffer) + byte_count};
	return buffer + byte_count;
}

/*
 * Convert an array of bytes into a string representing their hexadecimal ASCII values ie: [137, 250] =>
 * ['8', '9', 'F', 'A']
 *
 * @bytes [in]: Pointer to array of bytes to convert
 * @byte_count [in]: Number of bytes in array
 * @return: string representation of the byte buffer
 */
std::string bytes_to_hex_str(char const *bytes, size_t byte_count);

/*
 * Convert an array of bytes into a string representing their hexadecimal ASCII values ie: [137, 250] =>
 * ['8', '9', 'F', 'A']
 *
 * @bytes [in]: Pointer to array of bytes to convert
 * @byte_count [in]: Number of bytes in array
 * @str [out]: String to append to
 */
void bytes_to_hex_str(char const *bytes, size_t byte_count, std::string &str);

/*
 * Scale the provided size to be a multiple of alignment to satisfy aligned_alloc
 *
 * @alignment [in]: Alignment size
 * @size [in]: Requested size
 * @return Requested size or the next greater value which is a multiple of alignment
 */
size_t aligned_size(size_t alignment, size_t size);

} /* namespace storage::common */

#endif /* APPLICATIONS_STORAGE_STORAGE_COMMON_MESSAGE_UTILS_HPP_ */
