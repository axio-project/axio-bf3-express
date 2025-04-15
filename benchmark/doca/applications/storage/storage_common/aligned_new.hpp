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
#ifndef APPLICATIONS_STORAGE_STORAGE_COMMON_ALIGNED_NEW_HPP_
#define APPLICATIONS_STORAGE_STORAGE_COMMON_ALIGNED_NEW_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

namespace storage::common {

template <typename T>
class make_aligned {
public:
	make_aligned<T> &aligned_to(size_t alignment)
	{
		if (alignment < std::alignment_of<T>::value) {
			throw std::logic_error{
				"Specified alignment must be at least the natural alignment of the object"};
		}

		if ((alignment % std::alignment_of<T>::value) != 0) {
			throw std::logic_error{
				"Specified alignment must be a multiple the natural alignment of the object"};
		}

		m_alignment = alignment;

		return *this;
	}

	/*
	 * Create a single aligned object
	 *
	 * @args [in]: variadic argument list to be forwarded to the constructor of T
	 * @return: Pointer to aligned object
	 */
	template <typename... Args>
	T *object(Args &&...args) const
	{
		auto *storage = static_cast<T *>(aligned_alloc(m_alignment, sizeof(T)));
		if (storage == nullptr) {
			throw std::bad_alloc{};
		}

		try {
			static_cast<void>(new (storage) T{std::forward<Args>(args)...});
		} catch (...) {
			free(storage);
			throw;
		}

		return storage;
	}

	/*
	 * Create an array of aligned objects
	 *
	 * @object_count [in]: Number of objects to create in the array
	 * @args [in]: variadic argument list to be forwarded to the constructor of T
	 * @return: Pointer to aligned object
	 */
	template <typename... Args>
	T *object_array(size_t object_count, Args &&...args) const
	{
		auto *storage = static_cast<T *>(aligned_alloc(m_alignment, sizeof(T) * object_count));
		if (storage == nullptr) {
			throw std::bad_alloc{};
		}

		size_t valid_count = 0;
		try {
			for (size_t ii = 0; ii != object_count; ++ii) {
				static_cast<void>(new (std::addressof(storage[ii])) T{args...});
				++valid_count;
			}
		} catch (...) {
			while (valid_count) {
				storage[--valid_count].~T();
			}

			free(storage);
			throw;
		}

		return storage;
	}

private:
	size_t m_alignment = std::alignment_of<T>::value;
};

} // namespace storage::common

#endif // APPLICATIONS_STORAGE_STORAGE_COMMON_ALIGNED_NEW_HPP_
