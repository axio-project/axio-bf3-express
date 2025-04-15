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

#ifndef DOCA_TRANSPORT_COMMON_H_
#define DOCA_TRANSPORT_COMMON_H_

#include <stdint.h>

#define RPC_RETURN_STATUS_SUCCESS 0
#define RPC_RETURN_STATUS_ERROR 1
#define MAX_NUM_COMCH_MSGS 1024

enum comch_msg_type {
	COMCH_MSG_TYPE_BIND_SQ_DB,
	COMCH_MSG_TYPE_BIND_SQ_DB_DONE,
	COMCH_MSG_TYPE_UNBIND_SQ_DB,
	COMCH_MSG_TYPE_UNBIND_SQ_DB_DONE,
	COMCH_MSG_TYPE_RAISE_MSIX,
	COMCH_MSG_TYPE_HOST_DB,
};

struct comch_msg {
	enum comch_msg_type type;
	union {
		struct {
			uint64_t db;
			uint64_t cookie;
		} bind_sq_db_data;
		struct {
			uint64_t cookie;
		} bind_sq_db_done_data;
		struct {
			uint64_t db;
			uint64_t cookie;
		} unbind_sq_db_data;
		struct {
			uint64_t cookie;
		} unbind_sq_db_done_data;
		struct {
			uint64_t db_user_data;
			uint32_t db_value;
		} host_db_data;
	};
} __attribute__((__packed__, aligned(8)));

struct io_thread_arg {
	uint64_t dpa_consumer_comp;
	uint64_t dpa_producer_comp;
	uint64_t dpa_producer;
	uint64_t dpa_consumer;
	uint64_t dpa_db_comp;
	uint64_t dpa_msix;
} __attribute__((__packed__, aligned(8)));

#endif /* DOCA_TRANSPORT_COMMON_H_ */
