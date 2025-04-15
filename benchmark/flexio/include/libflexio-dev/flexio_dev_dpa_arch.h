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

#ifndef __FLEXIO_DEV_DPA_ARCH_H__
#define __FLEXIO_DEV_DPA_ARCH_H__

/*
 * WARNING: Do not reference anything in this header directly.
 *
 * This header contains internal architecture-specific definitions that
 * are subject to change and are not guaranteed to be backward-compatible (BWC).
 * Directly accessing or relying on the contents of this file can expose
 * your code to non-BWC changes in future releases.
 *
 * Always use the provided APIs, inline functions, or macros to ensure
 * proper compatibility and maintainability of your code.
 */

#include <stdint.h>

#define FLEXIO_DPA_ARCH_THREAD_CTX_SIZE 64
#define FLEXIO_DPA_ARCH_CTX_RESERVED_SIZE 13

#ifndef __FLEXIO_OS_H__

struct flexio_os_thread_ctx {
	const uint32_t version;
	const uint16_t default_outbox_config_id;
	const uint16_t default_primary_window_config_id;
	const uint64_t outbox_base;
	const uint64_t per_app_table_base;
	const uint64_t window_config_base;
	uint64_t metadata_parameter;
	uint32_t os_errno;
	const uint8_t per_app_table_vhca_id_shift;
	const uint8_t window_config_log_size;
	const uint16_t default_secondary_window_config_id;
	const uint8_t window_msbs;    /* includes window id bits size */
	const uint8_t window_log_size;
	const uint8_t window_ix_log_size;
	uint8_t reserved[13];
};
#endif

typedef uint64_t dpa_outbox_value_t;
typedef uint64_t dpa_window_value_t;

#define _OUTBOX_NULL_PTR(type)          ((struct dpa_outbox_v9_reg_ ## type ## _rbits *)0)
#define _OUTBOX_BIT_SIZE(type, field)   (sizeof(_OUTBOX_NULL_PTR(type)->field))
#define _OUTBOX_OFFSET(type, field)     ((uint64_t)&((_OUTBOX_NULL_PTR(type)->field)))

#define _OUTBOX_P_BGN(type, field)      (_OUTBOX_OFFSET(type, field))

#define OUTBOX_VALUE1(__v1, __s1) (((dpa_outbox_value_t)__v1) << (__s1))
#define OUTBOX_VALUE2(__v1, __s1, __v2, \
		      __s2) (OUTBOX_VALUE1(__v1, __s1) | OUTBOX_VALUE1(__v2, __s2))
#define OUTBOX_VALUE3(__v1, __s1, __v2, __s2, __v3, __s3)       \
	(OUTBOX_VALUE2(__v1, __s1, __v2, __s2) |                 \
	 OUTBOX_VALUE1(__v3, __s3))
#define OUTBOX_VALUE4(__v1, __s1, __v2, __s2, __v3, __s3, __v4, __s4)   \
	(OUTBOX_VALUE3(__v1, __s1, __v2, __s2, __v3, __s3) |            \
	 OUTBOX_VALUE1(__v4, __s4))

FLEXIO_DEV_ALWAYS_INLINE void __outbox_write(dpa_outbox_value_t *ptr, dpa_outbox_value_t value)
{
	*ptr = value;
}

#define OUTBOX_PAGE_FIELD_PTR(__outbox_page_ptr, \
			      __name) ((dpa_outbox_value_t *)(&(((dpa_outbox_u_page_t *) \
								 __outbox_page_ptr)->__name)))

#define _OUTBOX_WAIT_ON_UCTL_BUSY(__outbox_page_ptr) \
	while (((volatile dpa_outbox_u_page_t *)__outbox_page_ptr)->UCTL & 0x1)

#define outbox_write(__outbox_page_ptr, __name, __value) \
	do { \
		_OUTBOX_WAIT_ON_UCTL_BUSY(__outbox_page_ptr); \
		__outbox_write((OUTBOX_PAGE_FIELD_PTR(__outbox_page_ptr, __name)), \
			       ((dpa_outbox_value_t)(__value))); \
	} while (0)

#define RXC_RD_ADDRESS_SHIFT (_OUTBOX_P_BGN(RXC_RD, address))
#define OUTBOX_V_RXC_RD(_address) (OUTBOX_VALUE1(_address, RXC_RD_ADDRESS_SHIFT))

#define outbox_read_db(__outbox_page_ptr, __emu_ctx_id, __output) \
	do { \
		_OUTBOX_WAIT_ON_UCTL_BUSY(__outbox_page_ptr); \
		__outbox_write((OUTBOX_PAGE_FIELD_PTR(__outbox_page_ptr, RXC_RD)), \
			       ((dpa_outbox_value_t)(OUTBOX_V_RXC_RD(emu_ctx_id)))); \
		_OUTBOX_WAIT_ON_UCTL_BUSY(__outbox_page_ptr); \
		__output = ((dpa_outbox_u_page_t *)__outbox_page_ptr)->BUFFER; \
	} while (0)

#define CQ_DB_CQN_SHIFT                 _OUTBOX_P_BGN(CQ_DB, cqn)
#define CQ_DB_CQ_CI_SHIFT               _OUTBOX_P_BGN(CQ_DB, cq_ci)
#define OUTBOX_V_CQ_DB(__cqn, \
		       __cq_ci) (OUTBOX_VALUE2(__cqn, CQ_DB_CQN_SHIFT, __cq_ci, CQ_DB_CQ_CI_SHIFT))

#define RXT_DB_CQN_SHIFT                _OUTBOX_P_BGN(RXT_DB, cqn)
#define OUTBOX_V_RXT_DB(__cqn)          (OUTBOX_VALUE1(__cqn, RXT_DB_CQN_SHIFT))

#define CC_DB_RES_SHIFT         (_OUTBOX_P_BGN(CC_DB, ccq_res))
#define CC_DB_ID_SHIFT          (_OUTBOX_P_BGN(CC_DB, ccq_id))
#define CC_DB_NXT_ACT_SHIFT     (_OUTBOX_P_BGN(CC_DB, nxt_act))
#define CC_DB_RTTRQ_SHIFT       (_OUTBOX_P_BGN(CC_DB, rttrq))
#define OUTBOX_V_CC_DB(_res, _id, _nxt_act, _rttrq) \
	(OUTBOX_VALUE4(_res, CC_DB_RES_SHIFT, _id, CC_DB_ID_SHIFT, _nxt_act, CC_DB_NXT_ACT_SHIFT, \
		       _rttrq, CC_DB_RTTRQ_SHIFT))

#define EQ_DB_REARM_EQN_SHIFT           _OUTBOX_P_BGN(EQ_DB_REARM, eqn)
#define EQ_DB_REARM_EQ_CI_SHIFT         _OUTBOX_P_BGN(EQ_DB_REARM, eq_ci)
#define OUTBOX_V_EQ_DB_REARM(__eqn, \
			     __eq_ci) (OUTBOX_VALUE2(__eqn, EQ_DB_REARM_EQN_SHIFT, __eq_ci, \
						     EQ_DB_REARM_EQ_CI_SHIFT))

#define EQ_DB_NO_REARM_EQN_SHIFT        _OUTBOX_P_BGN(EQ_DB_NO_REARM, eqn)
#define EQ_DB_NO_REARM_EQ_CI_SHIFT      _OUTBOX_P_BGN(EQ_DB_NO_REARM, eq_ci)
#define OUTBOX_V_EQ_DB_NO_REARM(__eqn, \
				__eq_ci) (OUTBOX_VALUE2(__eqn, EQ_DB_NO_REARM_EQN_SHIFT, __eq_ci, \
							EQ_DB_NO_REARM_EQ_CI_SHIFT))

#define SXD_DB_WQE_INDEX_SHIFT          _OUTBOX_P_BGN(SXD_DB, wqe_index)
#define SXD_DB_QP_OR_SQ_SHIFT           _OUTBOX_P_BGN(SXD_DB, qp_or_sq)
#define OUTBOX_V_SXD_DB(__wqe_index, \
			__qp_or_sq) (OUTBOX_VALUE2(__wqe_index, SXD_DB_WQE_INDEX_SHIFT, __qp_or_sq, \
						   SXD_DB_QP_OR_SQ_SHIFT))

/* From lines 78-84 of apu_kernel/src/kernel/include/outbox/outbox_m.h  */
#define EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT       (_OUTBOX_P_BGN(EMU_CAP_TRIGGER, dummy_qpn))
#define EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT (_OUTBOX_P_BGN(EMU_CAP_TRIGGER, emulation_index))
#define OUTBOX_V_EMU_CAP_TRIGGER(_dummy_qpn, \
				 _emulation_index) (OUTBOX_VALUE2(_dummy_qpn, \
								  EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT, \
								  _emulation_index, \
								  EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT))

#define EMU_CAP_DUMMY_QPN_SHIFT               (_OUTBOX_P_BGN(EMU_CAP, dummy_qpn))
#define EMU_CAP_EMULATION_INDEX_SHIFT         (_OUTBOX_P_BGN(EMU_CAP, emulation_index))
#define OUTBOX_V_EMU_CAP(_dummy_qpn, \
			 _emulation_index) (OUTBOX_VALUE2(_dummy_qpn, \
							  EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT, \
							  _emulation_index, \
							  EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT))

#ifndef __BYTE_ORDER__
# error "__BYTE_ORDER__ is not defined"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
struct dpa_outbox9_u_page_le { /* Little Endian */
	uint64_t UCTL;                          /* TBD */
	unsigned char reserved_at_40[56];
	uint64_t BUFFER;                        /* TBD */
	unsigned char reserved_at_240[184];
	uint64_t CQ_DB;                         /* TBD */
	unsigned char reserved_at_840[56];
	uint64_t EQ_DB_REARM;                   /* TBD */
	unsigned char reserved_at_a40[56];
	uint64_t EQ_DB_NO_REARM;                /* TBD */
	unsigned char reserved_at_c40[56];
	uint64_t SXD_DB;                        /* TBD */
	unsigned char reserved_at_e40[56];
	uint64_t EMU_CAP;                       /* TBD */
	unsigned char reserved_at_1040[56];
	uint64_t EMU_CAP_TRIGGER;               /* TBD */
	unsigned char reserved_at_1240[56];
	uint64_t RXT_DB;                        /* TBD */
	unsigned char reserved_at_1440[56];
	uint64_t RXT_DONE0;                     /* TBD */
	uint64_t RXT_DONE1;                     /* TBD */
	unsigned char reserved_at_1680[48];
	uint64_t RXC_RD;                        /* TBD */
	unsigned char reserved_at_1840[56];
	uint64_t CC_DB;                         /* TBD */
	uint64_t CC_DB_MSB;                     /* TBD */
	unsigned char reserved_at_1a40[3184];
	uint64_t SW_RESERVE;                    /* TBD */
	unsigned char reserved_at_7e40[56];
};
typedef struct dpa_outbox9_u_page_le dpa_outbox_u_page_t;

struct dpa_outbox_v9_reg_UCTL_rbits {
	unsigned char busy[0x00001];            /* Busy: 0 - no db pending in this page;
	                                         *       1 more than zero DB pending */
	unsigned char err[0x00001];             /* Error status : 0 - OK ; 1 Error.*/
	unsigned char reserved_at_2[0x0001e];

	unsigned char version[0x00008];         /* Version = 0 */
	unsigned char reserved_at_28[0x00018];
};

struct dpa_outbox_v9_reg_CQ_DB_rbits {          /* Little Endian */
	unsigned char cqn[0x00018];             /* Re-arm CQ, update CQ ci */
	unsigned char reserved_at_18[0x00008];

	unsigned char cq_ci[0x00018];
	unsigned char reserved_at_38[0x00008];
};

struct dpa_outbox_v9_reg_EQ_DB_NO_REARM_rbits { /* Little Endian */
	unsigned char eqn[0x00018];             /* update EQ ci */
	unsigned char reserved_at_18[0x00008];

	unsigned char eq_ci[0x0001a];
	unsigned char reserved_at_3a[0x00006];
};

struct dpa_outbox_v9_reg_SXD_DB_rbits {         /* Little Endian */
	unsigned char wqe_index[0x00010];       /* Send Packet, SQ doorbell */
	unsigned char reserved_at_10[0x00010];

	unsigned char qp_or_sq[0x00018];
	unsigned char reserved_at_38[0x00008];
};

struct dpa_outbox_v9_reg_RXT_DB_rbits {         /* Little Endian */
	/* Trigger CQE with zeros in order to trigger EQE MSIx to Host */
	unsigned char cqn[0x00018];
	unsigned char reserved_at_18[0x00008];

	unsigned char reserved_at_20[0x00020];
};

struct dpa_outbox_v9_reg_RXC_RD_rbits {         /* Little Endian */
	unsigned char reserved_at_0[0x00002];
	unsigned char address[0x0001a]; /* On write sample emulation doorbell address, read 4 bytes
	                                 * into BUFFER low and clear busy */
	unsigned char reserved_at_1c[0x00004];
	unsigned char reserved_at_20[0x00020];
};

struct dpa_outbox_v9_reg_CC_DB_rbits {          /* Little Endian */
	unsigned char ccq_res[0x00020];         /* 1/rate */

	unsigned char rttrq[0x00001];           /* rtt measure request */
	unsigned char reserved_at_21[0x00001];
	unsigned char nxt_act[0x00002];         /* slice nxt action. 0 - single response,
	                                         * 1 - multiple response, 2 - fw */
	unsigned char reserved_at_24[0x0000c];
	unsigned char ccq_id[0x00010];          /* CCQ ID mapped to HW risc_id wire */
};

/* from ./build/fw_debug/src/kernel/include/adabe/apu_outbox_regs_adb_rbits.h */
typedef unsigned char pseudo_bit_t;
struct dpa_outbox_v9_reg_EMU_CAP_rbits {    /* Little Endian */
	pseudo_bit_t dummy_qpn[0x00018];                /* Re-arm for the Emulation event */
	pseudo_bit_t reserved_at_18[0x00008];
	/*----------------------------------------------------------*/
	pseudo_bit_t emulation_index[0x0001a];
	pseudo_bit_t reserved_at_3a[0x00006];
/* --------------------------------------------------------- */
};

struct dpa_outbox_v9_reg_EMU_CAP_TRIGGER_rbits {    /* Little Endian */
	pseudo_bit_t dummy_qpn[0x00018];                /* Re-arm for the SXW event */
	pseudo_bit_t reserved_at_18[0x00008];
	/*----------------------------------------------------------*/
	pseudo_bit_t emulation_index[0x0001a];
	pseudo_bit_t reserved_at_3a[0x00006];
/* --------------------------------------------------------- */
};

#else  /* if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ */
#error "incorrect BYTE_ORDER (big endian not supported)"
#endif

#endif
