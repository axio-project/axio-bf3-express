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
 * @file flexio_dev_tracer.h
 * @page FlexIODevTracer Flex IO SDK message stream
 * @defgroup FlexioSDKDevTraces Flex IO SDK message stream
 * Flex IO SDK message stream device API for DPA programs.
 * Includes message stream services for DPA programs.
 * @ingroup FlexioSDKDev
 *
 * @{
 */

#ifndef _FLEXIO_DEV_TRACER_H_
#define _FLEXIO_DEV_TRACER_H_

#include <stdint.h>
#include <libflexio-dev/flexio_dev.h>
#include <dpaintrin.h>

#ifndef __FLEXIO_COMMON_STRUCTS_H__
/* flexio_tracer_msg struct below MUST be aligned to the corresponding
 * struct defined in the (internal) flexio_common_structs.h header.
 */

/**
 * Describes Flex IO trace message.
 * This struct is used to communicate the tracer raw data from device to host.
 */
struct flexio_tracer_msg {
	uint32_t format_id;   /**< Format ID for trace string template to use. */
	uint32_t reserved[3]; /**< Reserved bits for memory alignment. */
	uint64_t arg0;        /**< Argument 0 for trace string format. */
	uint64_t arg1;        /**< Argument 1 for trace string format. */
	uint64_t arg2;        /**< Argument 2 for trace string format. */
	uint64_t arg3;        /**< Argument 3 for trace string format. */
	uint64_t arg4;        /**< Argument 4 for trace string format. */
	uint64_t arg5;        /**< Argument 5 for trace string format. */
} __attribute__((__packed__, aligned(8)));
#endif

struct flexio_tracer_streams_data {
	uint8_t valid_lvl;                        /**< Supported tracer print level. Any trace entry
	                                           *  with level <= tracer level will be printed */
	uint8_t transport_mode;                   /**< deprecated/unused field, saved for
	                                           *  compatibility. */
	uint16_t log_num_buf_msgs;                /**< log number of messages in single buffer. */
	uint32_t current_ix;                      /**< Current trace message index. */
	uint32_t max_entries_per_buf;             /**< Max number of trace entries in a buffer. */
	volatile uint32_t writing;                /**< Sync flag between threads to indicate a
	                                           *  buffer is being written to. */
	uint16_t buf_idx;                         /**< Consecutive buffer index to calculate next
	                                           *  available buffer. */
	uint16_t buf_mask;                        /**< Buffer index mask. */
	struct flexio_tracer_msg *current_buffer; /**< Current buffer being used for recording trace
	                                           *  entries. Either buffer_0 or buffer_1. */
	struct flexio_tracer_msg *buffer;         /**< Buffer for tracing. */
};

/**
 * Describes Flex IO process trace context.
 * This struct is used for managing different tracers for a DPA process.
 */
struct flexio_dev_process_tracer_ctx {
	/**< Process trace contexts. */
	struct flexio_tracer_streams_data *tracer_ctx[FLEXIO_MSG_DEV_MAX_STREAMS_AMOUNT];
};

/**
 * Global process tracer context struct instance.
 */
extern struct flexio_dev_process_tracer_ctx *g_dev_p_tracer_ctx;

/**
 * @brief Flush not full buffer.
 *
 * As soon as a buffer is fully occupied it is internal sent to host, however
 * user can ask partially occupied buffer to be sent to host. Its intended use
 * is at end of run to flush whatever messages left.
 * Flush is also performed by the host stream destroy call.
 *
 * NOTE: this call is not thread safe, user responsibility to avoid calling it
 * while any device trace APIs are in use. Frequent call to this API might cause
 * performance issues.
 *
 * @param[in] tracer_id - ID of tracer to flush.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_tracer_flush(uint8_t tracer_id);

/**
 * @brief Send a tracer buffer to host side.
 *
 * Send current used buffer to the host. Main usage is to send a full buffer to not
 * risk writing to the buffer from other threads while sending.
 *
 * NOTE: this call is not thread safe, user responsibility to avoid calling it
 * while any device trace APIs are in use.
 *
 * @param[in] tracer_id - ID of tracer to send a notification for.
 * @param[in] num_msg - number of messages.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_tracer_notify_host(uint8_t tracer_id, uint32_t num_msg);

#define TRACE_SWAP_BUFFERS(ctx) \
	(ctx->buffer + (((++ctx->buf_idx) & ctx->buf_mask) << ctx->log_num_buf_msgs))

#define TRACER_CHECK_LOGING_LEVEL(trc_ctx, lvl) \
	do { \
		if (trc_ctx->valid_lvl < lvl) { \
			return; \
		} \
	} while (0)

#define TRACER_CHECK_SWAP_BUFFER(trc_ctx, id, ix) \
	do { \
		while (ix >= trc_ctx->max_entries_per_buf) { \
			if (ix == trc_ctx->max_entries_per_buf) { \
				while (trc_ctx->writing) {;} \
				flexio_dev_tracer_notify_host(id, ix); \
				trc_ctx->current_buffer = TRACE_SWAP_BUFFERS(trc_ctx); \
				__atomic_store_n(&trc_ctx->current_ix, 0, __ATOMIC_RELAXED); \
			} \
			ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_RELAXED); \
		} \
	} while (0)

/**
 * @brief Creates trace message entry with no arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_0(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	trc_ctx->current_buffer[ix].format_id = format_id;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}
/**
 * @brief Creates trace message entry with 1 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_1(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}
/**
 * @brief Creates trace message entry with 2 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 * @param[in] arg1 - argument #1 to format into the template.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_2(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0, uint64_t arg1)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	cur_msg->arg1 = arg1;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}
/**
 * @brief Creates trace message entry with 3 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 * @param[in] arg1 - argument #1 to format into the template.
 * @param[in] arg2 - argument #2 to format into the template.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_3(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0, uint64_t arg1,
						 uint64_t arg2)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	cur_msg->arg1 = arg1;
	cur_msg->arg2 = arg2;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}
/**
 * @brief Creates trace message entry with 4 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 * @param[in] arg1 - argument #1 to format into the template.
 * @param[in] arg2 - argument #2 to format into the template.
 * @param[in] arg3 - argument #3 to format into the template.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_4(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0, uint64_t arg1,
						 uint64_t arg2, uint64_t arg3)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	cur_msg->arg1 = arg1;
	cur_msg->arg2 = arg2;
	cur_msg->arg3 = arg3;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}

/**
 * @brief Creates trace message entry with 5 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 * @param[in] arg1 - argument #1 to format into the template.
 * @param[in] arg2 - argument #2 to format into the template.
 * @param[in] arg3 - argument #3 to format into the template.
 * @param[in] arg4 - argument #4 to format into the template.
 *
 * @return Function does not return.
 */

FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_5(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0, uint64_t arg1,
						 uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	cur_msg->arg1 = arg1;
	cur_msg->arg2 = arg2;
	cur_msg->arg3 = arg3;
	cur_msg->arg4 = arg4;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}

/**
 * @brief Creates trace message entry with 6 arguments.
 *
 * Using the trace mechanism for fast logging. Call the appropriate function according to
 * number of needed arguments.
 *
 * @param[in] tracer_id - the relevant msg stream id.
 * @param[in] level - messaging level.
 * @param[in] format_id -the template format id to print message accordingly.
 * @param[in] arg0 - argument #0 to format into the template.
 * @param[in] arg1 - argument #1 to format into the template.
 * @param[in] arg2 - argument #2 to format into the template.
 * @param[in] arg3 - argument #3 to format into the template.
 * @param[in] arg4 - argument #4 to format into the template.
 * @param[in] arg5 - argument #5 to format into the template.
 *
 * @return Function does not return.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_trace_6(uint8_t tracer_id,  flexio_msg_dev_level level,
						 int format_id, uint64_t arg0, uint64_t arg1,
						 uint64_t arg2, uint64_t arg3, uint64_t arg4,
						 uint64_t arg5)
{
	struct flexio_tracer_streams_data *trc_ctx = g_dev_p_tracer_ctx->tracer_ctx[tracer_id];
	struct flexio_tracer_msg *cur_msg;
	uint64_t ix;

	TRACER_CHECK_LOGING_LEVEL(trc_ctx, level);

	ix = __atomic_fetch_add(&trc_ctx->current_ix, 1, __ATOMIC_SEQ_CST);
	TRACER_CHECK_SWAP_BUFFER(trc_ctx, tracer_id, ix);

	__atomic_fetch_add(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
	cur_msg = trc_ctx->current_buffer + ix;
	cur_msg->format_id = format_id;
	cur_msg->arg0 = arg0;
	cur_msg->arg1 = arg1;
	cur_msg->arg2 = arg2;
	cur_msg->arg3 = arg3;
	cur_msg->arg4 = arg4;
	cur_msg->arg5 = arg5;
	__atomic_fetch_sub(&trc_ctx->writing, 1, __ATOMIC_SEQ_CST);
}

/** @} */

#endif /* _FLEXIO_DEV_TRACER_H_ */
