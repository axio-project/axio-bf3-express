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
 * @file flexio_dev.h
 * @page FlexIODev Flex IO SDK dev
 * @defgroup FlexioSDKDev Flex IO SDK dev
 * Flex IO SDK device API for DPA programs.
 * Includes services for DPA programs.
 *
 * @{
 */

#ifndef _FLEXIO_DEV_H_
#define _FLEXIO_DEV_H_

#include <libflexio-dev/flexio_dev_ver.h>

#define FLEXIO_DEV_CURRENT_VERSION FLEXIO_DEV_VER(25, 1, 0)

#define FLEXIO_DEV_LAST_SUPPORTED_VERSION FLEXIO_DEV_VER(0, 0, 0)

#ifndef FLEXIO_DEV_VER_USED
#define FLEXIO_DEV_VER_USED FLEXIO_DEV_CURRENT_VERSION
#elif FLEXIO_DEV_VER_USED == FLEXIO_DEV_VER_LATEST
#undef FLEXIO_DEV_VER_USED
#define FLEXIO_DEV_VER_USED FLEXIO_DEV_CURRENT_VERSION
#endif

#if FLEXIO_DEV_VER_USED < FLEXIO_DEV_LAST_SUPPORTED_VERSION
#error "FLEXIO_DEV_VER_USED is less than FLEXIO_DEV_LAST_SUPPORTED_VERSION."
#elif FLEXIO_DEV_VER_USED == FLEXIO_DEV_LAST_SUPPORTED_VERSION
#warning "FLEXIO_DEV_VER_USED is equal to FLEXIO_DEV_LAST_SUPPORTED_VERSION " \
	"and will become obsolete in the next release."
#endif

#define FLEXIO_DEV_STABLE __attribute__((visibility("default"))) __attribute__((used))
#ifndef FLEXIO_DEV_ALLOW_EXPERIMENTAL_API
/**
 * @brief To set a Symbol (or specifically a function) as experimental.
 */
#define FLEXIO_DEV_EXPERIMENTAL \
	__attribute__((deprecated("Symbol is defined as experimental"), \
		       section(".text.experimental"))) \
	FLEXIO_DEV_STABLE
#else /* FLEXIO_DEV_ALLOW_EXPERIMENTAL_API */
#define FLEXIO_DEV_EXPERIMENTAL __attribute__((section(".text.experimental"))) FLEXIO_DEV_STABLE
#endif /* FLEXIO_DEV_ALLOW_EXPERIMENTAL_API */

#include <stdint.h>

/**
 * @cond NO_DOCS
 */

typedef uint64_t flexio_uintptr_t;

#ifndef FLEXIO_DEV_ALWAYS_INLINE
#define FLEXIO_DEV_ALWAYS_INLINE static inline __attribute__((always_inline))
#endif

/**
 * @endcond
 */

/** Return status of Flex IO dev API functions. */
typedef enum {
	FLEXIO_DEV_STATUS_SUCCESS = 0,
	FLEXIO_DEV_STATUS_FAILED  = 1,
} flexio_dev_status_t;

/** Flex IO UAR extension ID prototype. */
typedef uint32_t flexio_uar_device_id;

/** Flex IO dev CQ CQE creation modes. */
enum cq_ce_mode {
	MLX5_CTRL_SEG_CE_CQE_ON_CQE_ERROR       = 0x0,
	MLX5_CTRL_SEG_CE_CQE_ON_FIRST_CQE_ERROR = 0x1,
	MLX5_CTRL_SEG_CE_CQE_ALWAYS             = 0x2,
	MLX5_CTRL_SEG_CE_CQE_AND_EQE            = 0x3,
};

#if __NV_DPA == __NV_DPA_CX7
/** Flex IO dev NIC counters ID enumeration. */
enum flexio_dev_nic_counter_ids {
	FLEXIO_DEV_NIC_COUNTER_PORT0_RX_BYTES = 0x10,
	FLEXIO_DEV_NIC_COUNTER_PORT1_RX_BYTES = 0x11,
	FLEXIO_DEV_NIC_COUNTER_PORT2_RX_BYTES = 0x12,
	FLEXIO_DEV_NIC_COUNTER_PORT3_RX_BYTES = 0x13,

	FLEXIO_DEV_NIC_COUNTER_PORT0_TX_BYTES = 0x20,
	FLEXIO_DEV_NIC_COUNTER_PORT1_TX_BYTES = 0x21,
	FLEXIO_DEV_NIC_COUNTER_PORT2_TX_BYTES = 0x22,
	FLEXIO_DEV_NIC_COUNTER_PORT3_TX_BYTES = 0x23,
};
#endif

/** Flex IO dev windows entity. */
enum flexio_window_entity {
	FLEXIO_DEV_WINDOW_ENTITY_0   = 0,
	FLEXIO_DEV_WINDOW_ENTITY_1   = 1,
	FLEXIO_DEV_WINDOW_ENTITY_NUM = 2,
};

struct flexio_dev_thread_ctx;

/* Function types for NET and RPC handlers */

/**
 * RPC handler callback function type.
 *
 * Defines an RPC handler for most useful callback function.
 *
 * arg - argument of the RPC function.
 *
 * return uint64_t - result of the RPC function.
 */
typedef uint64_t (flexio_dev_rpc_handler_t)(uint64_t arg);

/**
 * Unpack the arguments and call the user function.
 *
 * This callback function is used at runtime to unpack the
 * arguments from the call on Host and then call the function on DPA.
 * This function is called internally from flexio dev.
 *
 * argbuf - Argument buffer that was written by Host.
 * func - Function pointer to user function.
 *
 * return uint64_t - result of the RPC function.
 */
typedef uint64_t (flexio_dev_arg_unpack_func_t)(void *argbuf, void *func);

/**
 * Asynchronous RPC handler callback function type.
 *
 * Defines an RPC handler callback function.
 *
 * arg - argument of the RPC function.
 *
 * return void.
 */
typedef void (flexio_dev_async_rpc_handler_t)(uint64_t arg);

/**
 * Event handler callback function type.
 *
 * Defines an event handler callback function.
 * On handler function end, need to call flexio_dev_process_finish() instead of a regular
 * return statement, in order to properly release resources back to the OS.
 *
 * thread_arg - an argument for the executing thread.
 *
 * return void.
 */
typedef void (flexio_dev_event_handler_t)(uint64_t thread_arg);

/**
 * Describes Flex IO dev spinlock.
 */
struct spinlock_s {
	uint32_t locked;        /**< Indication for spinlock lock state. */
#ifdef SPINLOCK_DEBUG
	uint32_t locker_tid;    /**< Locker thread ID. */
#endif
};

/**
 * @brief Initialize a spinlock mechanism.
 *
 * Initialize a spinlock mechanism, must be called before use.
 *
 * @param[in] lock - A pointer to spinlock_s structure.
 *
 * @return void.
 */
#ifdef SPINLOCK_DEBUG

#define spin_init(lock) \
	do { \
		__atomic_store_n(&((lock)->locked), 0, __ATOMIC_SEQ_CST); \
		__atomic_store_n(&((lock)->locker_tid), 0xffffffff, __ATOMIC_SEQ_CST); \
	} while (0)

#else

#define spin_init(lock) __atomic_store_n(&((lock)->locked), 0, __ATOMIC_SEQ_CST)

#endif

/**
 * @brief Lock a spinlock mechanism.
 *
 * Lock a spinlock mechanism.
 *
 * @param[in] lock - A pointer to spinlock_s structure.
 *
 * @return void.
 */
#ifdef SPINLOCK_DEBUG

#define spin_lock(lock) \
	do { \
		struct flexio_dev_thread_ctx *dtctx; \
		uint32_t tid; \
		flexio_dev_get_thread_ctx(&dtctx); \
		tid = flexio_dev_get_thread_id(dtctx); \
		while (__atomic_exchange_n(&((lock)->locked), 1, __ATOMIC_SEQ_CST)) {;} \
		__atomic_store_n(&((lock)->locker_tid), tid, __ATOMIC_SEQ_CST); \
	} while (0)

#else
#define spin_lock(lock) \
	do { \
		while (__atomic_exchange_n(&((lock)->locked), 1, __ATOMIC_SEQ_CST)) {;} \
	} while (0)
#endif

/**
 * @brief Unlock a spinlock mechanism.
 *
 * Unlock a spinlock mechanism.
 *
 * @param[in] lock - A pointer to spinlock_s structure.
 *
 * @return void.
 */
#ifdef SPINLOCK_DEBUG

#define spin_unlock(lock) \
	do { \
		__atomic_store_n(&((lock)->locker_tid), 0xffffffff, __ATOMIC_SEQ_CST); \
		__atomic_store_n(&((lock)->locked), 0, __ATOMIC_SEQ_CST); \
	} while (0)

#else
#define spin_unlock(lock) __atomic_store_n(&((lock)->locked), 0, __ATOMIC_SEQ_CST)
#endif

/**
 * @brief Atomic try to catch lock.
 *
 * makes attempt to take lock. Returns immediately.
 *
 * @param[in] lock - A pointer to spinlock_s structure.
 *
 * @return zero on success. Nonzero otherwise.
 */
#define spin_trylock(lock) __atomic_exchange_n(&((lock)->locked), 1, __ATOMIC_SEQ_CST)

/**
 * @brief Request thread context.
 *
 * This function requests the thread context. Should be called
 * for every start of thread.
 *
 * @param[in] dtctx - A pointer to a pointer of flexio_dev_thread_ctx structure.
 *
 * @return 0 on success negative value on failure.
 */
FLEXIO_DEV_EXPERIMENTAL int flexio_dev_get_thread_ctx(struct flexio_dev_thread_ctx **dtctx);

/**
 * @brief Exit from a thread, leave process active.
 *
 * This function releases resources back to OS.
 * For the next DUAR the thread will restart from the beginning.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_thread_reschedule(void);
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_reschedule(void) __attribute__ ((deprecated));

/**
 * @brief Exit from a thread, mark it as finished.
 *
 * This function releases resources back to OS.
 * The thread will be marked as finished so next DUAR will not trigger it.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_thread_finish(void);

/**
 * @brief Exit from a thread, and retrigger it.
 *
 * This function asks the OS to retrigger the thread.
 * The thread will not wait for the next DUAR to be triggered but will be triggered
 * immediately.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_thread_retrigger(void);

/**
 * @brief Exit flexio process (no errors).
 *
 * This function releases resources back to OS and returns '0x40' in dpa_process_status.
 * All threads for the current process will stop executing and no new threads will be able
 * to trigger for this process.
 * Threads state will NOT be changes to 'finished' (will remain as is).
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_process_finish(void);
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_finish(void) __attribute__ ((deprecated));

/**
 * @brief Put a string to messaging queue.
 *
 * This function puts a string to host's default stream messaging queue.
 * This queue has been serviced by host application.
 * Would have no effect, if the host application didn't configure device messaging stream
 * environment.
 * In order to initialize/configure device messaging environment -
 * On HOST side - after flexio_process_create, a stream should be created, therefore
 * flexio_msg_stream_create should be called, and the default stream should be created.
 * On DEV side - before using flexio_dev_puts, the thread context is needed, therefore
 * flexio_dev_get_thread_ctx should be called before.
 *
 * @param[in] dtctx - A pointer to a pointer of flexio_dev_thread_ctx structure.
 * @param[in] str - A pointer to string.
 *
 * @return length of messaged string.
 */
FLEXIO_DEV_EXPERIMENTAL int flexio_dev_puts(struct flexio_dev_thread_ctx *dtctx, char *str);

/**
 * @brief Config thread outbox object without any checks.
 *
 * This function updates the thread outbox object of the given thread
 * context, but it doesn't check for correctness or redundancy (same ID as current configured).
 *
 * @param[in] dtctx - A pointer to flexio_dev_thread_ctx structure.
 * @param[in] outbox_config_id - The outbox object config id.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_outbox_config_fast(struct flexio_dev_thread_ctx *dtctx,
							   uint16_t outbox_config_id);

/**
 * @brief Config thread outbox object.
 *
 * This function updates the thread outbox object of the given thread
 * context.
 *
 * @param[in] dtctx - A pointer to flexio_dev_thread_ctx structure.
 * @param[in] outbox_config_id - The outbox object config id.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_outbox_config(struct flexio_dev_thread_ctx *dtctx,
					     uint16_t outbox_config_id);

/**
 * @brief Config thread window object.
 *
 * This function updates the thread window object of the given thread
 * context.
 *
 * @param[in] win_entity - The window entity to configure.
 * @param[in] window_config_id - The window object id.
 * @param[in] mkey - The mkey id.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_multi_window_config(enum flexio_window_entity win_entity,
						   uint16_t window_config_id, uint32_t mkey);

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
static inline flexio_dev_status_t flexio_dev_window_config(enum flexio_window_entity win_entity,
							   uint16_t window_config_id, uint32_t mkey)
{
	return flexio_dev_multi_window_config(win_entity, window_config_id, mkey);
}
#else
static inline flexio_dev_status_t flexio_dev_window_config(struct flexio_dev_thread_ctx *dtctx,
							   uint16_t window_config_id, uint32_t mkey)
{
	(void)dtctx;
	return flexio_dev_multi_window_config(FLEXIO_DEV_WINDOW_ENTITY_0, window_config_id,
					      mkey);
}
#endif

/**
 * @brief Config thread window mkey object.
 *
 * This function updates the thread window mkey object of the given thread
 * context.
 *
 * @param[in] win_entity - The window entity to configure.
 * @param[in] mkey - The mkey id.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_multi_window_mkey_config(enum flexio_window_entity win_entity,
							uint32_t mkey);

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
static inline flexio_dev_status_t flexio_dev_window_mkey_config(
	enum flexio_window_entity win_entity, uint32_t mkey)
{
	return flexio_dev_multi_window_mkey_config(win_entity, mkey);
}
#else
static inline flexio_dev_status_t flexio_dev_window_mkey_config(struct flexio_dev_thread_ctx *dtctx,
								uint32_t mkey)
{
	(void)dtctx;
	return flexio_dev_multi_window_mkey_config(FLEXIO_DEV_WINDOW_ENTITY_0, mkey);
}
#endif

/**
 * @brief Generate device address from host allocated memory.
 *
 * This function generates a memory address to be used by device to access host side memory,
 * according to already create window object. from a host allocated address.
 *
 * @param[in]  win_entity - The window entity to configure.
 * @param[in]  haddr - Host allocated address.
 * @param[out] daddr - A pointer to write the device generated matching address.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_multi_window_ptr_acquire(enum flexio_window_entity win_entity,
							uint64_t haddr, flexio_uintptr_t *daddr);

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
static inline flexio_dev_status_t flexio_dev_window_ptr_acquire(
	enum flexio_window_entity win_entity, uint64_t haddr, flexio_uintptr_t *daddr)
{
	return flexio_dev_multi_window_ptr_acquire(win_entity, haddr, daddr);
}
#else
static inline flexio_dev_status_t flexio_dev_window_ptr_acquire(struct flexio_dev_thread_ctx *dtctx,
								uint64_t haddr,
								flexio_uintptr_t *daddr)
{
	(void)dtctx;
	return flexio_dev_multi_window_ptr_acquire(FLEXIO_DEV_WINDOW_ENTITY_0, haddr, daddr);
}
#endif

/**
 * @brief Copy a buffer from host memory to device memory.
 *
 * This function copies specified number of bytes from host memory to device memory.
 * UNSUPPORTED at this time.
 *
 * @param[in] win_entity - The window entity to configure.
 * @param[in] daddr - A pointer to the device memory buffer.
 * @param[in] haddr - A pointer to the host memory allocated buffer.
 * @param[in] size - Number of bytes to copy.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_multi_window_copy_from_host(enum flexio_window_entity win_entity,
							   void *daddr, uint64_t haddr,
							   uint32_t size);

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
static inline flexio_dev_status_t flexio_dev_window_copy_from_host(
	enum flexio_window_entity win_entity, void *daddr, uint64_t haddr, uint32_t size)
{
	return flexio_dev_multi_window_copy_from_host(win_entity, daddr, haddr, size);
}
#else
static inline
flexio_dev_status_t flexio_dev_window_copy_from_host(struct flexio_dev_thread_ctx *dtctx,
						     void *daddr, uint64_t haddr, uint32_t size)
{
	(void)dtctx;
	return flexio_dev_multi_window_copy_from_host(FLEXIO_DEV_WINDOW_ENTITY_0, daddr,
						      haddr, size);
}
#endif

/**
 * @brief Copy a buffer from device memory to host memory.
 *
 * This function copies specified number of bytes from device memory to host memory.
 *
 * @param[in] win_entity - The window entity to configure.
 * @param[in] haddr - A pointer to the host memory allocated buffer.
 * @param[in] daddr - A pointer to the device memory buffer.
 * @param[in] size - Number of bytes to copy.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_multi_window_copy_to_host(enum flexio_window_entity win_entity,
							 uint64_t haddr, const void *daddr,
							 uint32_t size);

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
static inline
flexio_dev_status_t flexio_dev_window_copy_to_host(enum flexio_window_entity win_entity,
						   uint64_t haddr, const void *daddr, uint32_t size)
{
	return flexio_dev_multi_window_copy_to_host(win_entity, haddr, daddr, size);
}
#else
static inline
flexio_dev_status_t flexio_dev_window_copy_to_host(struct flexio_dev_thread_ctx *dtctx,
						   uint64_t haddr, const void *daddr, uint32_t size)
{
	(void)dtctx;
	return flexio_dev_multi_window_copy_to_host(FLEXIO_DEV_WINDOW_ENTITY_0, haddr, daddr,
						    size);
}
#endif

/**
 * @brief Get thread ID from thread context
 *
 * This function queries a thread context for its thread ID (from thread metadata).
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return thread ID value.
 */
FLEXIO_DEV_EXPERIMENTAL uint32_t flexio_dev_get_thread_id(struct flexio_dev_thread_ctx *dtctx);

/**
 * @brief Get thread local storage address from thread context
 *
 * This function queries a thread context for its thread local storage (from thread metadata).
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return thread local storage value.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_uintptr_t flexio_dev_get_thread_local_storage(struct flexio_dev_thread_ctx *dtctx);

/**
 * @brief Get thread name pointer
 *
 * This function queries the thread context for its thread name (from thread metadata).
 *
 * @return pointer to thread name string.
 */
FLEXIO_DEV_EXPERIMENTAL const char *flexio_dev_get_current_thread_name(void);

/* Flex IO device messaging. */
#define FLEXIO_MSG_DEV_BROADCAST_STREAM 0xff /* The 256'th stream - outputs to all streams */
#define FLEXIO_MSG_DEV_MAX_STREAMS_AMOUNT 255
#define FLEXIO_MSG_DEV_DEFAULT_STREAM_ID 0
/* Flex IO device messaging levels. */
/* A usage of FLEXIO_MSG_DEV_NO_PRINT in flexio_dev_msg will terminate with no log. */
#define FLEXIO_MSG_DEV_NO_PRINT 0
#define FLEXIO_MSG_DEV_ALWAYS_PRINT 1
#define FLEXIO_MSG_DEV_ERROR 2
#define FLEXIO_MSG_DEV_WARN 3
#define FLEXIO_MSG_DEV_INFO 4
#define FLEXIO_MSG_DEV_DEBUG 5

typedef uint8_t flexio_msg_dev_level;

/**
 * @brief Creates message entry and outputs from the device to the host side.
 *
 * Same as a regular printf but with protection from simultaneous print from different threads.
 *
 * @param[in] stream_id - the relevant msg stream, created and passed from the host.
 * @param[in] level - messaging level.
 * @param[in] format, ... - same as for regular printf.
 *
 * @return - same as from regular printf.
 */
FLEXIO_DEV_EXPERIMENTAL int flexio_dev_msg(int stream_id, flexio_msg_dev_level level,
					   const char *format, ...)
__attribute__ ((format(printf, 3, 4)));


/* Device messaging streams concise MACROs */
#define flexio_dev_msg_err(strm_id, ...) flexio_dev_msg(strm_id, FLEXIO_MSG_DEV_ERROR, __VA_ARGS__)
#define flexio_dev_msg_warn(strm_id, ...) flexio_dev_msg(strm_id, FLEXIO_MSG_DEV_WARN, __VA_ARGS__)
#define flexio_dev_msg_info(strm_id, ...) flexio_dev_msg(strm_id, FLEXIO_MSG_DEV_INFO, __VA_ARGS__)
#define flexio_dev_msg_dbg(strm_id, ...) flexio_dev_msg(strm_id, FLEXIO_MSG_DEV_DEBUG, __VA_ARGS__)

/**
 * @brief Create message entry and outputs from the device to host's default stream.
 * Same as a regular printf but with protection from simultaneous print from different threads.
 *
 * @param[in] lvl - messaging level.
 * @param ... - format and the parameters. Same as for regular printf.
 *
 * @return - same as from regular printf.
 */
#define flexio_dev_msg_dflt(lvl, ...) \
	flexio_dev_msg(FLEXIO_MSG_DEV_DEFAULT_STREAM_ID, lvl, __VA_ARGS__)

/**
 * @brief Create message entry and outputs from the device to all of the host's open streams.
 * Same as a regular printf but with protection from simultaneous print from different threads.
 *
 * @param[in] lvl - messaging level.
 * @param ... - va_args parameters. Same as for regular vsprintf.
 *
 * @return - same as from regular printf.
 */
#define flexio_dev_msg_broadcast(lvl, ...) \
	flexio_dev_msg(FLEXIO_MSG_DEV_BROADCAST_STREAM, lvl, __VA_ARGS__)

/**
 * @brief Create message entry and outputs from the device to host's default stream,
 * with FLEXIO_MSG_DEV_INFO message level.
 * Same as a regular printf but with protection from simultaneous print from different threads.
 *
 * @param ... - format and the parameters. Same as for regular printf.
 *
 * @return - same as from regular printf.
 */
#define flexio_dev_print(...) \
	flexio_dev_msg(FLEXIO_MSG_DEV_DEFAULT_STREAM_ID, FLEXIO_MSG_DEV_INFO, __VA_ARGS__)

/**
 * @brief exit point for continuable event handler routine
 *
 * This function is used to mark the exit point on continuable event handler where
 * user wishes to continue execution on next event. In order to use this API the event handler must
 * be created with continuable flag enabled, otherwise call will have no effect.
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_yield(struct flexio_dev_thread_ctx *dtctx);

/**
 * @brief exit point for retriggered continuable event handler routine
 *
 * This function is used to mark the exit point on continuable event handler where
 * user wishes to continue execution on next event.
 * Before exiting, an event is generated for the calling thread to retrigger it immediately
 * upon exit.
 * In order to use this API the event handler must be created with continuable flag enabled,
 * otherwise call will have no effect.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_yield_and_retrigger(void);

/**
 * @brief get programable congestion control table base address
 *
 * This function gets the programable congestion control table base address.
 *
 * @param[in] vhca_id - PCC table VHCA_ID.
 *
 * @return PCC table base address for the given VHCA_ID.
 */
FLEXIO_DEV_EXPERIMENTAL uint64_t flexio_dev_get_pcc_table_base(uint16_t vhca_id);

/**
 * @brief set extension ID for outbox
 *
 * This function sets the device ID for the outbox to operate on.
 *
 * @param[in] dtctx - A pointer to a flexio_dev_thread_ctx structure.
 * @param[in] device_id - The device ID.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_outbox_config_uar_extension(struct flexio_dev_thread_ctx *dtctx,
							   flexio_uar_device_id device_id);

/**
 * @brief send a doorbell to a QP on another device ID
 *
 * @param[in] device_id - The device ID.
 * @param[in] qpn - QP number to send doorbell on.
 * @param[in] pi - doorbell producer index.
 *
 * @return flexio_dev_status_t
 */
FLEXIO_DEV_EXPERIMENTAL
flexio_dev_status_t flexio_dev_cross_device_ring_db(flexio_uar_device_id device_id, uint32_t qpn,
						    uint32_t pi);

/**
 * @brief Prepare a list of counters to read
 *
 * The list is stored in kernel memory. A single counters config per process is supported.
 * Note that arrays memory must be defined in global or heap memory only.
 *
 * @param[out] counter_values - buffer to store counters values (32b) read by
 *                              flexio_dev_nic_counters_sample().
 * @param[in]  counter_ids - An array of counter ids.
 * @param[in]  num_counters - number of counters in the counter_ids array.
 *
 * @return void
 *       process crashes in case of:
 *       counters_ids too large
 *       bad pointers of values, counter_ids
 *       unknown counter
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_nic_counters_config(uint32_t *counter_values,
							    uint32_t *counter_ids,
							    uint32_t num_counters);

/**
 * @brief Sample counters according to the prior configuration call
 *
 * Sample counter_ids, num_counters and values buffer provided in the last successful call to
 * flexio_dev_config_nic_counters().
 * This call ensures fastest sampling on a pre-checked counter ids and buffers.
 *
 * @return void.
 *    process crashes in case of: flexio_dev_config_nic_counters() never called
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_nic_counters_sample(void);

/**
 * @brief Activate an event handler thread
 *
 * Using activation id, activate (trigger) the event handler with that activation id.
 * Note that the activated event handler must be of same process as the activating thread.
 *
 * @param[in] activation_id - The event handler activation ID.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_event_handler_activate(uint32_t activation_id);

/**
 * @brief 2 bytes alignment memory copy
 *
 * 2 bytes alignment memory copy. User responsibility that address and size are 2 bytes aligned.
 *
 * @param[in] dest - pointer to destination buffer.
 * @param[in] src - pointer to source buffer.
 * @param[in] n - number of bytes to copy.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL static inline void flexio_dev_memcpy16(uint16_t *dest, const uint16_t *src,
							       uint64_t n)
{
	n /= sizeof(*dest);

	while (n--)
		*dest++ = *src++;
}

/**
 * @brief 4 bytes alignment memory copy
 *
 * 4 bytes alignment memory copy. User responsibility that address and size are 4 bytes aligned.
 *
 * @param[in] dest - pointer to destination buffer.
 * @param[in] src - pointer to source buffer.
 * @param[in] n - number of bytes to copy.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL static inline void flexio_dev_memcpy32(uint32_t *dest, const uint32_t *src,
							       uint64_t n)
{
	n /= sizeof(*dest);

	while (n--)
		*dest++ = *src++;
}

/**
 * @brief 8 bytes alignment memory copy
 *
 * 8 bytes alignment memory copy. User responsibility that address and size are 8 bytes aligned.
 *
 * @param[in] dest - pointer to destination buffer.
 * @param[in] src - pointer to source buffer.
 * @param[in] n - number of bytes to copy.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL static inline void flexio_dev_memcpy64(uint64_t *dest, const uint64_t *src,
							       uint64_t n)
{
	n /= sizeof(*dest);

	while (n--)
		*dest++ = *src++;
}

/** @} */

#endif /* _FLEXIO_DEV_H_ */
