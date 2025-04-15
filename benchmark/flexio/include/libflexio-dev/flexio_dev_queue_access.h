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
 * @file flexio_dev_queue_access.h
 * @page FlexIODevQueueAccess Flex IO SDK dev queue access
 * @defgroup FlexioSDKDevQueueAccess Flex IO SDK dev queue access
 * Flex IO SDK device API for DPA programs queue access.
 * Provides an API for handling networking queues (WQs/CQs).
 * @ingroup FlexioSDKDev
 *
 * @{
 */

#ifndef _FLEXIO_DEV_QUEUE_ACCESS_H_
#define _FLEXIO_DEV_QUEUE_ACCESS_H_

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_queue_types.h>
#include <libflexio-dev/flexio_dev_dpa_arch.h>
#include <libflexio-dev/flexio_dev_endianity.h>
#include <stdint.h>

/**
 * @brief QP/SQ ring doorbell function
 *
 * Rings the doorbell of a QP or SQ in order to alert the HW of pending work.
 *
 * @param[in] pi - Current queue producer index.
 * @param[in] qnum - Number of the queue to update.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_qp_sq_ring_db(uint16_t pi, uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_qp_sq_ring_db(struct flexio_dev_thread_ctx *dtctx, uint16_t pi,
					     uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, SXD_DB, OUTBOX_V_SXD_DB(pi, qnum));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Read the outbox DB
 *
 * This function reads the outbox doorbell.
 *
 * @param[in] emu_ctx_id - the context id to read
 *
 * @return the doorbell value.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_read_db_value(uint64_t emu_ctx_id)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
	uint64_t value;

	outbox_read_db(otctx->outbox_base, emu_ctx_id, value);

	return (uint32_t)value;
}

/**
 * @brief arm the emulation context
 *
 * @param[in] qnum - Number of the queue provided by host.
 * @param[in] emu_ctx_id - Emulation context ID, provided by a call on the host to
 *			   flexio_emu_db_to_cq_ctx_get_id.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_db_ctx_arm(uint32_t qnum, uint32_t emu_ctx_id)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_db_ctx_arm(struct flexio_dev_thread_ctx *dtctx, uint32_t qnum,
					  uint32_t emu_ctx_id)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, EMU_CAP, OUTBOX_V_EMU_CAP(qnum, emu_ctx_id));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief force trigger of emulation context
 *
 * @param[in] cqn - CQ number provided by host.
 * @param[in] emu_ctx_id - Emulation context ID, provided by a call on the host to
 *			   flexio_emu_db_to_cq_ctx_get_id.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_db_ctx_force_trigger(uint32_t cqn, uint32_t emu_ctx_id)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_db_ctx_force_trigger(struct flexio_dev_thread_ctx *dtctx,
						    uint32_t cqn, uint32_t emu_ctx_id)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, EMU_CAP_TRIGGER,
		     OUTBOX_V_EMU_CAP_TRIGGER(cqn, emu_ctx_id));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Update an EQ consumer index function
 *
 * Updates the consumer index of an EQ after handling an EQE.
 *
 * @param[in] ci - Current EQ consumer index.
 * @param[in] qnum - Number of the EQ to update.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_eq_update_ci(uint32_t ci, uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_eq_update_ci(struct flexio_dev_thread_ctx *dtctx, uint32_t ci,
					    uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, EQ_DB_NO_REARM,
		     OUTBOX_V_EQ_DB_NO_REARM(qnum, ci));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Arm CQ function
 *
 * Moves a CQ to 'armed' state.
 * This means that next CQE created for this CQ will result in an EQE on the relevant EQ.
 *
 * @param[in] ci - Current CQ consumer index.
 * @param[in] qnum - Number of the CQ to arm.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_cq_arm(uint32_t ci, uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_cq_arm(struct flexio_dev_thread_ctx *dtctx, uint32_t ci,
				      uint32_t qnum)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, CQ_DB, OUTBOX_V_CQ_DB(qnum, ci));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Generate a zero CQE on the given CQ
 *
 * This function trigger the given CQ by creating a zero CQE on it. In turn, this may activate any
 * handler connected to the CQ, most commonly another thread or an EQ (and MSIX).
 *
 * @param[in] cqn - CQ number to trigger. Trigger is done via currently configured outbox,
 *                  this can be changed with outbox config API according to CQ.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_zcqe_gen(uint32_t cqn)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_zcqe_gen(struct flexio_dev_thread_ctx *dtctx, uint32_t cqn)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, RXT_DB, OUTBOX_V_RXT_DB(cqn));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Generate an MSI-X from the given CQ
 *
 * This function trigger an MSI-X interrupt connected to the given CQ.
 *
 * @param[in] cqn - CQ number to trigger. Trigger is done via currently configured outbox,
 *                  this can be changed with outbox config API according to CQ.
 *
 * @return void.
 */
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#define flexio_dev_msix_send(cqn) flexio_dev_zcqe_gen(cqn)
#else
#define flexio_dev_msix_send(dtctx, cqn) flexio_dev_zcqe_gen(dtctx, cqn)
#endif

/** Flex IO dev congestion control next action types. */
enum flexio_dev_cc_db_next_act {
	CC_DB_NEXT_ACT_SINGLE   = 0x0,
	CC_DB_NEXT_ACT_MULTIPLE = 0x1,
	CC_DB_NEXT_ACT_FW       = 0x2
};

/**
 * @brief Rings CC doorbell
 *
 * This function rings CC doorbell for the requested CC queue, which
 * sets the requested rate, RTT request and next action.
 *
 * @param[in] ccq_id - CC queue ID to update.
 * @param[in] rate - Rate to set.
 * @param[in] rtt_req - RTT measure request to set.
 * @param[in] next_act - Next action to set.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE
#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
void flexio_dev_cc_ring_db(uint16_t ccq_id, uint32_t rate, uint32_t rtt_req,
			   enum flexio_dev_cc_db_next_act next_act)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();
#else
flexio_dev_status_t flexio_dev_cc_ring_db(struct flexio_dev_thread_ctx *dtctx, uint16_t ccq_id,
					  uint32_t rate, uint32_t rtt_req,
					  enum flexio_dev_cc_db_next_act next_act)
{
	struct flexio_os_thread_ctx *otctx = (void *)dtctx;

#endif
	outbox_write(otctx->outbox_base, CC_DB,
		     OUTBOX_V_CC_DB(rate, ccq_id, next_act, rtt_req));

#if FLEXIO_DEV_VER_USED >= FLEXIO_DEV_VER(24, 10, 0)
#else
	return FLEXIO_DEV_STATUS_SUCCESS;
#endif
}

/**
 * @brief Write to low 64 bits of CC doorbell register
 *
 * This function writes to low 64 bits of CC doorbell register. Register value should be
 * prepared by caller according to register layout.
 *
 * @param[in] value - 64 bits value to write.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_cc_ring_db_low64bit(uint64_t value)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();

	outbox_write(otctx->outbox_base, CC_DB, value);
}

/**
 * @brief Write to high 64 bits of CC doorbell register
 *
 * This function writes to high 64 bits of CC doorbell register. Register value should be
 * prepared by caller according to register layout.
 *
 * @param[in] value - 64 bits value to write.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_cc_ring_db_high64bit(uint64_t value)
{
	struct flexio_os_thread_ctx *otctx = (void *)__dpa_os_get_thread_ctx();

	outbox_write(otctx->outbox_base, CC_DB_MSB, value);
}

/**
 * @brief Get csum OK field from CQE function
 *
 * Parse a CQE for its csum OK field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint16_t - csum_ok field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint16_t flexio_dev_cqe_get_csum_ok(struct flexio_dev_cqe64 *cqe)
{
	return be16_to_cpu((volatile __be16)cqe->csum_ok);
}

/**
 * @brief Get QP number field from CQE function
 *
 * Parse a CQE for its QP number field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - QP number field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_qpn(struct flexio_dev_cqe64 *cqe)
{
	return be32_to_cpu((volatile __be32)cqe->qpn) & ((1 << 24) - 1);
}

/**
 * @brief Get WQE counter filed from CQE function
 *
 * Parse a CQE for its WQE counter field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint16_t - WQE counter field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint16_t flexio_dev_cqe_get_wqe_counter(struct flexio_dev_cqe64 *cqe)
{
	return be16_to_cpu((volatile __be16)cqe->wqe_counter);
}

/**
 * @brief Get byte count field from CQE function
 *
 * Parse a CQE for its byte count field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - Byte count field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_byte_cnt(struct flexio_dev_cqe64 *cqe)
{
	return be32_to_cpu((volatile __be32)cqe->byte_cnt);
}

/**
 * @brief Get error syndrome field from CQE function
 *
 * Parse a CQE for its error syndrome field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - Error syndrome.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_err_synd(struct flexio_dev_cqe64 *cqe)
{
	return (volatile uint8_t)cqe->syndrome;
}

/**
 * @brief Get HW error syndrome field from CQE function
 *
 * Parse a CQE for its HW error syndrome field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - HE error syndrome.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_hw_err_synd(struct flexio_dev_cqe64 *cqe)
{
	return (volatile uint8_t)cqe->hw_err_syndrome;
}

/**
 * @brief Get vendor error syndrome field from CQE function
 *
 * Parse a CQE for its vendor error syndrome field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - Vendor error syndrome.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_vendor_err_synd(struct flexio_dev_cqe64 *cqe)
{
	return (volatile uint8_t)cqe->vendor_err_synd;
}

/**
 * @brief Get the opcode field from CQE function
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - Opcode field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_cqe_get_opcode(struct flexio_dev_cqe64 *cqe)
{
	return ((volatile uint8_t)cqe->op_own) >> 4;
}

/**
 * @brief Get the type of CQE function
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - the type of the CQE.
 *     0 - no inline data
 *     1 - inline data in the data 32 segment
 *     2 - inline data in the data 64 segment
 *     3 - Compressed CQE
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_cqe_get_type(struct flexio_dev_cqe64 *cqe)
{
	return ((cqe->op_own >> 2) & 0x3);
}

/**
 * @brief Get the user index field from CQE function
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint32_t - User index field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_cqe_get_user_index(struct flexio_dev_cqe64 *cqe)
{
	return be32_to_cpu((volatile __be32)cqe->srqn_uidx) & ((1 << 24) - 1);
}

/**
 * @brief init CQEs according to compressed feature requirement.
 *
 * @param[in] cqe - first CQE in range to init.
 * @param[in] num_cqes - Number of CQEs in range.
 * @param[in] validity_iteration_count - validity_iteration_count field value to init.
 *
 * @return void.
 */
FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_comp_cq_init(struct flexio_dev_cqe64 *cqe, int num_cqes,
						      uint8_t validity_iteration_count)
{
	int i;

	for (i = 0; i < num_cqes; i++) {
		((struct flexio_dev_mini_cqe64 *)cqe + i)->validity_iteration_count =
			validity_iteration_count;
	}
}

/**
 * @brief Get the validity iteration count byte value from CQE.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - validity byte value.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_comp_cqe_get_validity_byte(struct flexio_dev_cqe64 *cqe)
{
	return cqe->validity_iteration_count;
}

/**
 * @brief returns true if CQE is a mini CQE array
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - true for mini cqe array.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_comp_cqe_is_comp_cqe_array(struct flexio_dev_cqe64 *cqe)
{
	return ((((struct flexio_dev_mini_cqe64 *)cqe)->num_and_type >> 2) & 0x3) == 0x3;
}

/**
 * @brief Get number of mini CQEs in CQE
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - number of mini CQEs in array.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_comp_cqe_get_num_comp_cqes(struct flexio_dev_cqe64 *cqe)
{
	return (((struct flexio_dev_mini_cqe64 *)cqe)->num_and_type >> 4) + 1;
}

/**
 * @brief Get a mini CQE from CQE
 *
 * @param[in] cqe - CQE to parse.
 * @param[in] comp_cqe_index - index of mini CQE to return from array.
 *
 * @return uint64_t - the mini CQE value.
 */
FLEXIO_DEV_ALWAYS_INLINE uint64_t flexio_dev_comp_cqe_get_comp_cqe(struct flexio_dev_cqe64 *cqe,
								   int comp_cqe_index)
{
	return ((struct flexio_dev_mini_cqe64 *)cqe)->mini_cqe[comp_cqe_index];
}

/**
 * @brief Get the RX hash result from a mini CQE
 *
 * @param[in] _x - mini CQE value.
 *
 * @return uint32_t - the RX hash result.
 */
#define FLEXIO_DEV_COMP_CQE_GET_RX_HASH_RESULT(_x) be32_to_cpu((uint32_t)((_x) & 0xFFFFFFFFULL))

/**
 * @brief Get CQ number field from EQE function
 *
 * Parse an EQE for its CQ number field.
 *
 * @param[in] eqe - EQE to parse.
 *
 * @return uint32_t - CQ number field value of the EQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint32_t flexio_dev_eqe_get_cqn(struct flexio_dev_eqe *eqe)
{
	return be32_to_cpu((volatile __be32)eqe->event_data.cqn) & ((1 << 24) - 1);
}

/**
 * @brief Get address field from receive WQE function
 *
 * Parse a receive WQE for its address field.
 *
 * @param[in] rwqe - WQE to parse.
 *
 * @return void* - Address field value of the receive WQE.
 */
FLEXIO_DEV_ALWAYS_INLINE void *flexio_dev_rwqe_get_addr(struct flexio_dev_wqe_rcv_data_seg *rwqe)
{
	return (void *)be64_to_cpu((volatile __be64)rwqe->addr);
}

/**
 * @brief Get owner field from CQE function
 *
 * Parse a CQE for its owner field.
 *
 * @param[in] cqe - CQE to parse.
 *
 * @return uint8_t - Owner field value of the CQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_cqe_get_owner(struct flexio_dev_cqe64 *cqe)
{
	return ((volatile uint8_t)cqe->op_own) & 0x1;
}

/**
 * @brief Get owner field from EQE function
 *
 * Parse an EQE for its owner field.
 *
 * @param[in] eqe - EQE to parse.
 *
 * @return uint32_t - owner field value of the EQE.
 */
FLEXIO_DEV_ALWAYS_INLINE uint8_t flexio_dev_eqe_get_owner(struct flexio_dev_eqe *eqe)
{
	return ((volatile uint8_t)eqe->owner) & 0x1;
}

/** WQE control segment local mmo op_mod options. */
enum {
	MLX5_CTRL_SEGMENT_OPC_MOD_LOCAL_MMO_TRANSPOSE = 0x0,
	MLX5_CTRL_SEGMENT_OPC_MOD_LOCAL_MMO_LOCAL_DMA = 0x1,
};

/** WQE control segment wait op_mod options. */
enum {
	MLX5_CTRL_SEGMENT_OPC_MOD_WAIT_ON_DATA = 0x1,
};

/** Flex IO dev WQE control segment types. */
enum flexio_ctrl_seg {
	FLEXIO_CTRL_SEG_SEND_EN                 = 0,
	FLEXIO_CTRL_SEG_SEND_RC                 = 1, /* No difference for control segments to */
	FLEXIO_CTRL_SEG_SEND_UC                 = 1, /* sending data through connected QPs */
	FLEXIO_CTRL_SEG_SEND_UD                 = 2,
	FLEXIO_CTRL_SEG_LDMA                    = 3,
	FLEXIO_CTRL_SEG_RDMA_WRITE              = 4,
	FLEXIO_CTRL_SEG_RDMA_READ               = 5,
	FLEXIO_CTRL_SEG_LSO                     = 6,
	FLEXIO_CTRL_SEG_NOP                     = 7,
	FLEXIO_CTRL_SEG_RDMA_WRITE_IMM          = 8,
	FLEXIO_CTRL_SEG_TRANSPOSE               = 9,
	FLEXIO_CTRL_SEG_WAIT_ON_DATA            = 10,
	FLEXIO_CTRL_SEG_ATOMIC_COMPARE_AND_SWAP = 11,
	FLEXIO_CTRL_SEG_ATOMIC_FETCH_AND_ADD    = 12,
};

/** WQE control segment opcode options. */
enum {
	MLX5_CTRL_SEG_OPCODE_NOP                            = 0x0,
	MLX5_CTRL_SEG_OPCODE_SND_INV                        = 0x1,
	MLX5_CTRL_SEG_OPCODE_RDMA_WRITE                     = 0x8,
	MLX5_CTRL_SEG_OPCODE_RDMA_WRITE_WITH_IMMEDIATE      = 0x9,
	MLX5_CTRL_SEG_OPCODE_SEND                           = 0xa,
	MLX5_CTRL_SEG_OPCODE_SEND_WITH_IMMEDIATE            = 0xb,
	MLX5_CTRL_SEG_OPCODE_LSO                            = 0xe,
	MLX5_CTRL_SEG_OPCODE_WAIT                           = 0xf,
	MLX5_CTRL_SEG_OPCODE_RDMA_READ                      = 0x10,
	MLX5_CTRL_SEG_OPCODE_ATOMIC_COMPARE_AND_SWAP        = 0x11,
	MLX5_CTRL_SEG_OPCODE_ATOMIC_FETCH_AND_ADD           = 0x12,
	MLX5_CTRL_SEG_OPCODE_ATOMIC_MASKED_COMPARE_AND_SWAP = 0x14,
	MLX5_CTRL_SEG_OPCODE_ATOMIC_MASKED_FETCH_AND_ADD    = 0x15,
	MLX5_CTRL_SEG_OPCODE_RECEIVE_EN                     = 0x16,
	MLX5_CTRL_SEG_OPCODE_SEND_EN                        = 0x17,
	MLX5_CTRL_SEG_OPCODE_SET_PSV                        = 0x20,
	MLX5_CTRL_SEG_OPCODE_GET_PSV                        = 0x21,
	MLX5_CTRL_SEG_OPCODE_CHECK_PSV                      = 0x22,
	MLX5_CTRL_SEG_OPCODE_DUMP                           = 0x23,
	MLX5_CTRL_SEG_OPCODE_UMR                            = 0x25,
	MLX5_CTRL_SEG_OPCODE_RGET_PSV                       = 0x26,
	MLX5_CTRL_SEG_OPCODE_RCHECK_PSV                     = 0x27,
	MLX5_CTRL_SEG_OPCODE_TAG_MATCHING                   = 0x28,
	MLX5_CTRL_SEG_OPCODE_ENHANCED_MPSW                  = 0x29,
	MLX5_CTRL_SEG_OPCODE_QOS_REMAP                      = 0x2a,
	MLX5_CTRL_SEG_OPCODE_FLOW_TABLE_ACCESS              = 0x2c,
	MLX5_CTRL_SEG_OPCODE_ACCESS_ASO                     = 0x2d,
	MLX5_CTRL_SEG_OPCODE_MMO                            = 0x2f,
	MLX5_CTRL_SEG_OPCODE_LOAD_REMOTE_MICRO_APP          = 0x30,
	MLX5_CTRL_SEG_OPCODE_STORE_REMOTE_MICRO_APP         = 0x31,
	MLX5_CTRL_SEG_OPCODE_LOCAL_MMO                      = 0x32,
};

/**
 * @brief Fill out a control send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with control segment information.
 * This should always be the 1st segment of the WQE.
 * Note: For RDMA write immediate WQE - user should fill the immediate data information in the
 * control segment.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] sq_pi - Producer index of the send WQE.
 * @param[in] sq_number - SQ number that holds the WQE.
 * @param[in] ce - wanted CQ policy for CQEs. Value is taken from cq_ce_mode enum.
 * @param[in] ctrl_seg_type - Type of control segment.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_ctrl_set(
	union flexio_dev_sqe_seg *swqe, uint32_t sq_pi, uint32_t sq_number, uint32_t ce,
	enum flexio_ctrl_seg ctrl_seg_type)
{
	uint32_t ds_count;
	uint32_t opcode;
	uint32_t mod;

	/* default for common case */
	mod = 0;

	switch (ctrl_seg_type) {
	case FLEXIO_CTRL_SEG_SEND_EN:
		opcode = MLX5_CTRL_SEG_OPCODE_SEND;
		ds_count = 3;
		break;

	case FLEXIO_CTRL_SEG_LSO:
		opcode = MLX5_CTRL_SEG_OPCODE_LSO;
		ds_count = 4;
		break;

	/* FLEXIO_CTRL_SEG_SEND_UC has the same value */
	case FLEXIO_CTRL_SEG_SEND_RC:
		opcode = MLX5_CTRL_SEG_OPCODE_SEND;
		ds_count = 2;
		break;

	case FLEXIO_CTRL_SEG_SEND_UD:
		opcode = MLX5_CTRL_SEG_OPCODE_SEND;
		/* ctl 1 av 1 (compact mode. AV field ext must be 0) data 1
		 *
		 * If ext = 1 - ds_count can be variable up to 7. Use dedicated API instead
		 * of flexio_dev_swqe_seg_ctrl_set() */
		ds_count = 3;
		break;

	case FLEXIO_CTRL_SEG_LDMA:
		opcode = MLX5_CTRL_SEG_OPCODE_LOCAL_MMO;
		mod = MLX5_CTRL_SEGMENT_OPC_MOD_LOCAL_MMO_LOCAL_DMA;
		ds_count = 4;
		break;

	case FLEXIO_CTRL_SEG_TRANSPOSE:
		opcode = MLX5_CTRL_SEG_OPCODE_LOCAL_MMO;
		mod = MLX5_CTRL_SEGMENT_OPC_MOD_LOCAL_MMO_TRANSPOSE;
		ds_count = 4;
		break;

	case FLEXIO_CTRL_SEG_RDMA_WRITE:
		opcode = MLX5_CTRL_SEG_OPCODE_RDMA_WRITE;
		ds_count = 3;
		break;

	case FLEXIO_CTRL_SEG_RDMA_WRITE_IMM:
		opcode = MLX5_CTRL_SEG_OPCODE_RDMA_WRITE_WITH_IMMEDIATE;
		ds_count = 3;
		break;

	case FLEXIO_CTRL_SEG_RDMA_READ:
		opcode = MLX5_CTRL_SEG_OPCODE_RDMA_READ;
		ds_count = 3;
		break;

	case FLEXIO_CTRL_SEG_NOP:
		opcode = MLX5_CTRL_SEG_OPCODE_NOP;
		ds_count = 1;
		break;

	case FLEXIO_CTRL_SEG_ATOMIC_COMPARE_AND_SWAP:
		opcode = MLX5_CTRL_SEG_OPCODE_ATOMIC_COMPARE_AND_SWAP;
		ds_count = 4;
		break;

	case FLEXIO_CTRL_SEG_ATOMIC_FETCH_AND_ADD:
		opcode = MLX5_CTRL_SEG_OPCODE_ATOMIC_FETCH_AND_ADD;
		ds_count = 4;
		break;

	case FLEXIO_CTRL_SEG_WAIT_ON_DATA:
		opcode = MLX5_CTRL_SEG_OPCODE_WAIT;
		mod = MLX5_CTRL_SEGMENT_OPC_MOD_WAIT_ON_DATA;
		ds_count = 3;
		break;
	}

	/* Fill out 1-st segment (Control) */
	swqe->ctrl.idx_opcode = cpu_to_be32((mod << 24) | ((sq_pi & 0xffff) << 8) | opcode);
	swqe->ctrl.qpn_ds = cpu_to_be32((sq_number << 8) | ds_count);
	swqe->ctrl.signature_fm_ce_se = cpu_to_be32(ce << 2);
	swqe->ctrl.general_id = 0;

	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out an ethernet send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with Ethernet segment information.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] cs_swp_flags - Flags for checksum and swap, see PRM section 8.9.4.2,
 *			     Send WQE Construction Summary.
 * @param[in] mss - Maximum Segment Size - For LSO WQEs - the number of bytes in the
 *		    TCP payload to be transmitted in each packet. Must be 0 on non LSO WQEs.
 * @param[in] inline_hdr_bsz - Length of inlined packet headers in bytes.
 *			       This includes the headers in the inline_data segment as well.
 * @param[in] inline_hdrs - First 2 bytes of the inlined packet headers.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_eth_set(
	union flexio_dev_sqe_seg *swqe, uint16_t cs_swp_flags, uint16_t mss,
	uint16_t inline_hdr_bsz, uint8_t inline_hdrs[2])
{
	swqe->eth.rsvd0 = 0;
	swqe->eth.rsvd2 = 0;
	swqe->eth.cs_swp_flags = cpu_to_be16(cs_swp_flags);
	swqe->eth.mss = cpu_to_be16(mss);
	swqe->eth.inline_hdr_bsz = cpu_to_be16(inline_hdr_bsz);
	if (inline_hdrs) {
		swqe->eth.inline_hdrs[0] = inline_hdrs[0];
		swqe->eth.inline_hdrs[1] = inline_hdrs[1];
	}

	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out an RDMA send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with RDMA segment information.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] rkey - Remote memory access key for the RDMA operation.
 * @param[in] raddr - Address of the data for the RDMA operation.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_rdma_set(
	union flexio_dev_sqe_seg *swqe, uint32_t rkey, uint64_t raddr)
{
	swqe->rdma.raddr = cpu_to_be64(raddr);
	swqe->rdma.rkey = cpu_to_be32(rkey);
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out a memory pointer data send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with memory pointer data segment information.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] data_sz - Size of the data.
 * @param[in] lkey - Local memory access key for the data operation.
 * @param[in] data_addr - Address of the data for the data operation.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_mem_ptr_data_set(
	union flexio_dev_sqe_seg *swqe, uint32_t data_sz, uint32_t lkey, uint64_t data_addr)
{
	swqe->mem_ptr_send_data.byte_count = cpu_to_be32(data_sz);
	swqe->mem_ptr_send_data.lkey = cpu_to_be32(lkey);
	swqe->mem_ptr_send_data.addr = cpu_to_be64(data_addr);
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out an inline data send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with inline data segment information.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] data_sz - Size of the data.
 * @param[in] data - Inline data array (3 DWORDs).
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_inline_data_set(
	union flexio_dev_sqe_seg *swqe, uint32_t data_sz, uint32_t *data)
{
	/* byte_count bit 31 is indication for inline wqe */
	swqe->inline_send_data.byte_count = cpu_to_be32(0x80000000 | data_sz);
	swqe->inline_send_data.data_and_padding[0] = *(data);
	swqe->inline_send_data.data_and_padding[1] = *(data + 1);
	swqe->inline_send_data.data_and_padding[2] = *(data + 2);

	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out an Atomic send queue wqe segment function
 *
 * Fill the fields of a send WQE segment (2 DWORDs) with Atomic segment information.
 * This segment can service a compare & swap or fetch & add operation.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] swap_or_add_data - The data that will be swapped in or the data that will be added.
 * @param[in] compare_data - The data that will be compared with. Unused in fetch & add operation.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_atomic_set(
	union flexio_dev_sqe_seg *swqe, uint64_t swap_or_add_data, uint64_t compare_data)
{
	swqe->atomic.swap_or_add_data = cpu_to_be64(swap_or_add_data);
	swqe->atomic.compare_data = cpu_to_be64(compare_data);
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out a Shared receive queue wqe segment function
 *
 * Fill the fields of a linked list shared receive WQE segment.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] next_wqe_index - The next wqe index.
 * @param[in] signature - The signature.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE
flexio_dev_status_t flexio_dev_swqe_seg_shared_receive_set(union flexio_dev_sqe_seg *swqe,
							   uint16_t next_wqe_index,
							   uint8_t signature)
{
	swqe->shared_receive.next_wqe_index = cpu_to_be16(next_wqe_index);
	swqe->shared_receive.signature = signature;
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out a Transpose send wqe segment function
 *
 * Fill the fields of a send WQE segment (4 DWORDs) with Transpose segment information.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] element_size - The Matrix element_size.
 * @param[in] num_of_cols - Number of columns in the matrix.
 * @param[in] num_of_rows - Number of rows in the matrix.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_transpose_set(
	union flexio_dev_sqe_seg *swqe, uint8_t element_size, uint8_t num_of_cols,
	uint8_t num_of_rows)
{
	swqe->transpose.element_size = element_size;
	swqe->transpose.num_of_cols = num_of_cols;
	swqe->transpose.num_of_rows = num_of_rows;
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out a Datagram wqe first segment function
 *
 * Datagram segment can be built from one or three segments. This function aimed to fill the
 * first one.
 *
 * @param[in] swqe         - Send WQE segment to fill.
 * @param[in] qkey         - Queue key (q_key) of destination QP.
 * @param[in] dqp          - Destination QP number for UD and DCT for DC.
 * @param[in] ext          - Address vector extension (0x0 or 0x1).
 * @param[in] stat_rate_sl - Maximum static rate control, SL/ethernet priority.
 * @param[in] fl_mlid      - Force loopback (MSB) and source LID for IB.
 * @param[in] rlid         - Remote LID
 *
 */
static inline void flexio_dev_swqe_seg_av1_set(union flexio_dev_sqe_seg *swqe, uint32_t qkey,
					       uint32_t dqp, uint8_t ext, uint8_t stat_rate_sl,
					       uint8_t fl_mlid, uint16_t rlid)
{
	swqe->adres_vector.s1.key.qkey.qkey = cpu_to_be32(qkey);
	swqe->adres_vector.s1.key.qkey.reserved = 0;
	swqe->adres_vector.s1.ext_dqp_dct = cpu_to_be32(((uint32_t)ext << 31) | dqp);
	swqe->adres_vector.s1.stat_rate_sl = stat_rate_sl;
	swqe->adres_vector.s1.fl_mlid = fl_mlid;
	swqe->adres_vector.s1.rlid = cpu_to_be16(rlid);
}

/**
 * @brief Fill out a Datagram wqe second segment function
 *
 * Extended datagram segment built from three segments. This function aimed to fill the
 * second one.
 *
 * @param[in] swqe       - Send WQE segment to fill.
 * @param[in] rmac	 - Remote MAC
 * @param[in] tclass	 - GRH tclass/IPv6 tclass/IPv4 ToS
 * @param[in] hop_limit	 - GRH hop limit/IPv6 hop limit/IPv4 TTL
 * @param[in] grh_gid_fl - GRH, source GID address and IPv6 flow label.
 */
static inline void flexio_dev_swqe_seg_av2_set(union flexio_dev_sqe_seg *swqe, uint8_t *rmac,
					       uint8_t tclass, uint8_t hop_limit,
					       uint32_t grh_gid_fl)
{
	/* This is analog of memcpy(swqe->adres_vector.s2.rmac, rmac, 6);
	 * Usage memcpy() here can impact performance */
	*(uint32_t *)swqe->adres_vector.s2.rmac = *(uint32_t *)rmac;
	*(uint16_t *)(swqe->adres_vector.s2.rmac + 4) = *(uint32_t *)(rmac + 4);

	swqe->adres_vector.s2.tclass = tclass;
	swqe->adres_vector.s2.hop_limit = hop_limit;
	swqe->adres_vector.s2.grh_gid_fl = cpu_to_be32(grh_gid_fl);
}

/**
 * @brief Fill out a Datagram wqe third segment function
 *
 * Extended datagram segment built from three segments. This function aimed to fill the last one.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] rgid - Remote GID/IP address.
 */
static inline void flexio_dev_swqe_seg_av3_set(union flexio_dev_sqe_seg *swqe, uint8_t *rgid)
{
	/* This is analog of memcpy(swqe->adres_vector.s3.rgid, rgid, 16);
	 * Usage of memcpy can impact performance */
	*(uint64_t *)swqe->adres_vector.s3.rgid = *(uint64_t *)rgid;
	*(uint64_t *)(swqe->adres_vector.s3.rgid + 8) = *(uint64_t *)(rgid + 8);
}

/**
 * @brief Fill out a wait on data wqe segment function, control portion
 *
 * Fill the fields of the control portion of a wait on data WQE segment (4 DWORDs).
 * Supported when wait_on_data cap is set, it is user responsibility to validate it.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] operation - operation to perform of the data.
 * @param[in] inv - invert the operation.
 * @param[in] lkey - lkey of address.
 * @param[in] addr - address to perform operation on.
 * @param[in] action_on_fail - action on fail.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_wait_on_data_set(
	union flexio_dev_sqe_seg *swqe, uint8_t operation, uint8_t inv, uint32_t lkey,
	uint64_t addr, uint8_t action_on_fail)
{
	swqe->wait_on_data.op_inv = (inv << 4) | operation;
	swqe->wait_on_data.lkey = cpu_to_be32(lkey);
	swqe->wait_on_data.va_63_32 = cpu_to_be32(addr >> 32);
	swqe->wait_on_data.va_31_3_fail_act = cpu_to_be32((addr & 0xfffffff8) | action_on_fail);

	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Fill out a wait on data wqe segment function, data portion
 *
 * Fill the fields of the data portion of a wait on data WQE segment (4 DWORDs)
 * Supported when wait_on_data cap is set, it is user responsibility to validate it.
 * Note, data should be provided according to host endianness and wait_on_data_big_endian
 * capability.
 *
 * @param[in] swqe - Send WQE segment to fill.
 * @param[in] data - Data value to use for data comparison.
 * @param[in] data_mast - Data mask.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_swqe_seg_wait_on_data_data_set(
	union flexio_dev_sqe_seg *swqe, uint64_t data, uint64_t data_mask)
{
	swqe->wait_on_data_data.data = data;
	swqe->wait_on_data_data.data_mask = cpu_to_be64(data_mask);

	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Increment producer index of an RQ by 1 function
 *
 * Mark a WQE for reuse by incrementing the relevant RQ producer index by 1
 *
 * @param[in] rq_dbr - A pointer to the CQ's doorbell record address.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_dbr_rq_inc_pi(uint32_t *rq_dbr)
{
	uint32_t rq_rcv_counter;

	rq_rcv_counter = be32_to_cpu(*rq_dbr);
	*rq_dbr = cpu_to_be32((rq_rcv_counter + 1) & 0xffff);
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/**
 * @brief Set consumer index value for a CQ function
 *
 * Writes an updated consumer index number to a CQ's doorbell record
 *
 * @param[in] cq_dbr - A pointer to the CQ's doorbell record address.
 * @param[in] ci - The consumer index value to update.
 *
 * @return flexio_dev_status_t.
 */
FLEXIO_DEV_ALWAYS_INLINE flexio_dev_status_t flexio_dev_dbr_cq_set_ci(uint32_t *cq_dbr, uint32_t ci)
{
	*cq_dbr = cpu_to_be32(ci & ((1 << 24) - 1));
	return FLEXIO_DEV_STATUS_SUCCESS;
}

/*********************************************************************************************
 * Necropolis of deprecated code starts here
 * Stuff here need for backward compatibility only
 * Never use it in modern code
 *********************************************************************************************/

/* Deprecated type definitions */
typedef enum flexio_dev_cc_db_next_act flexio_dev_cc_db_next_act_t;
typedef enum flexio_ctrl_seg flexio_ctrl_seg_t;

/** @} */

#endif /* _FLEXIO_DEV_QUEUE_ACCESS_H_ */
