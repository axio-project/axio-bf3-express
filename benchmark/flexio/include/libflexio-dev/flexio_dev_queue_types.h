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
 * @file flexio_dev_queue_types.h
 * @page FlexIODevQueueTypes Flex IO SDK dev queue types
 * @defgroup FlexIODevQueueTypes Flex IO SDK dev queue types
 * Flex IO SDK device queue types for DPA programs.
 * Defines basic networking elements structure.
 * @ingroup FlexioSDKDev
 *
 * @{
 */

#ifndef _FLEXIO_DEV_QUEUE_TYPES_H_
#define _FLEXIO_DEV_QUEUE_TYPES_H_

#include <libflexio-dev/flexio_dev.h>
#include <stdint.h>

/** SQ depth (log_sq_depth) is measured in WQEBBs, each one is 64B.
 * We have to understand difference between wqe_idx and seg_idx.
 * For example wqe with index 5 built from 4 segments with indexes 20, 21, 22 and 23.
 */
#define LOG_SQE_NUM_SEGS 2

typedef uint64_t __be64;
typedef uint32_t __be32;
typedef uint16_t __be16;

/* This stuff based on content of mlx5/device.h file
 * from MFT driver
 */

/**
 * Describes Flex IO dev EQE.
 */
struct flexio_dev_eqe {
	uint8_t rsvd00;                 /**< 00h - Reserved. */
	uint8_t type;                   /**< 01h - EQE type. */
	uint8_t rsvd02;                 /**< 02h - Reserved. */
	uint8_t sub_type;               /**< 03h - Sub type. */

	uint8_t rsvd4[28];              /**< 04h..1fh - Reserved. */
	struct {
		__be32 rsvd00[6];       /**< 00h..17h - Reserved. */
		__be32 cqn;             /**< 18h 24 lsb - CQN. */
	} event_data;                   /**< 20h - Event data. */

	__be16 rsvd3c;                  /**< 3Ch - Reserved. */
	uint8_t signature;              /**< 3Eh - Signature. */
	uint8_t owner;                  /**< 3Fh - Owner. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev CQE.
 */
struct flexio_dev_cqe64 {
	__be32 rsvd00[5];          /**< 00h..04h   - Reserved. */
	__be16 csum_ok;            /**< 05h 16..31 - checksum ok bits. */
	uint8_t rsvd22[2];         /**< 05h 0..15  - Reserved. */
	__be32 rsvd24[2];          /**< 06h..07h   - Reserved. */
	__be32 srqn_uidx;          /**< 08h        - SRQ number or user index. */
	__be32 imm_inval_pkey;     /**< 09h        - immediate / invalidate key / pkey */
	__be32 rsvd40;             /**< 0Ah        - Reserved. */
	__be32 byte_cnt;           /**< 0Bh        - Byte count. */
	__be32 rsvd48;             /**< 0Ch        - Reserved. */
	uint8_t hw_err_syndrome;   /**< 0Dh 24..31 - HW error syndrome. */
	uint8_t rsvd50;            /**< 0Dh 16..23 - reserved. */
	uint8_t vendor_err_synd;   /**< 0Dh 08..15 - vendor error syndrome. */
	uint8_t syndrome;          /**< 0Dh 00..07 - syndrome. */
	union {
		struct {
			uint8_t sop_rdrop; /**< 0Eh 24..31 - send_wqe_opcode/rx_drop_counter */
			uint8_t qpn24[3];  /**< 0Eh 0..23 - qpn with 24 bits */
		};
		__be32 qpn;        /**< 0Eh - QPN. */
	};
	__be16 wqe_counter;        /**< 0Fh 16..31 - WQE counter. */
	union {
		uint8_t signature;         /**< 0Fh 8..15 - Signature/validity. */
		uint8_t validity_iteration_count;
	};
	volatile uint8_t op_own;   /**< 0Fh 0..0 - Ownership bit. 04..07 opcode*/
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev compressed CQE.
 */
struct flexio_dev_mini_cqe64 {
	__be64 mini_cqe[7];               /**< 00h..37h mini cqe array. */
	uint8_t rsvd0[6];                 /**< 38h..3dh mini cqe 7. */
	uint8_t validity_iteration_count; /**< 3eh..3eh validity iteration count. */
	uint8_t num_and_type;             /**< 3fh..3fh no' of mini cqes, mini cqe format. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE memory pointer send data segment.
 */
struct flexio_dev_wqe_mem_ptr_send_data_seg {
	__be32 byte_count;      /**< 00h - Byte count. */
	__be32 lkey;            /**< 01h - Local key. */
	__be64 addr;            /**< 02h..03h - Address. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE inline send data segment.
 */
struct flexio_dev_wqe_inline_send_data_seg {
	__be32 byte_count;              /**< 00h - Byte count. */
	__be32 data_and_padding[3];     /**< 01h..03h - Data and padding array. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE receive data segment.
 */
struct flexio_dev_wqe_rcv_data_seg {
	__be32 byte_count;      /**< 00h - Byte count. */
	__be32 lkey;            /**< 01h - Local key. */
	__be64 addr;            /**< 02h..03h - Address. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev shared receive WQE.
 */
struct flexio_dev_wqe_shared_receive_seg {
	uint8_t rsvd0[2];      /**< Reserved bits for memory alignment. */
	__be16 next_wqe_index; /**< Index (pointer) in the WQE buffer to the next WQE to be
	                        *  executed. */
	uint8_t signature;     /**< WQE signature. */
	uint8_t rsvd1[11];     /**< Reserved bits for memory alignment. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE control segment.
 */
struct flexio_dev_wqe_ctrl_seg {
	__be32 idx_opcode;              /**< 00h - WQE index and opcode. */
	__be32 qpn_ds;                  /**< 01h - QPN and number of data segments. */
	__be32 signature_fm_ce_se;      /**< 02h - Signature, fence mode, completion mode and
	                                 *        solicited event. */
	__be32 general_id;              /**< 03h - Control general ID. */
} __attribute__((packed, aligned(8)));

/* PRM section 8.9.4.2, Table 49 Send WQE Construction Summary */
/** Flex IO dev ethernet segment bitmask for CS / SWP flags */
typedef enum {
	FLEXIO_ETH_SEG_L4CS              = 0x8000,
	FLEXIO_ETH_SEG_L3CS              = 0x4000,
	FLEXIO_ETH_SEG_L4CS_INNER        = 0x2000,
	FLEXIO_ETH_SEG_L3CS_INNER        = 0x1000,
	FLEXIO_ETH_SEG_TRAILER_ALIGN     = 0x0200,
	FLEXIO_ETH_SEG_SWP_OUTER_L4_TYPE = 0x0040,
	FLEXIO_ETH_SEG_SWP_OUTER_L3_TYPE = 0x0020,
	FLEXIO_ETH_SEG_SWP_INNER_L4_TYPE = 0x0002,
	FLEXIO_ETH_SEG_SWP_INNER_L3_TYPE = 0x0001,
} flexio_dev_wqe_eth_seg_cs_swp_flags_t;

/**
 * Describes Flex IO dev WQE ethernet segment.
 */
struct flexio_dev_wqe_eth_seg {
	__be32 rsvd0;           /**< 00h - Reserved. */
	__be16 cs_swp_flags;    /**< 01h 16..31 - CS and SWP flags. */
	__be16 mss;             /**< 01h 0..15 - Max segment size. */
	__be32 rsvd2;           /**< 02h - Reserved. */
	__be16 inline_hdr_bsz;  /**< 03h 16..31 - Inline headers size (bytes). */
	uint8_t inline_hdrs[2]; /**< 03h 0..15 - Inline headers (first two bytes). */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE inline data segment.
 */
struct flexio_dev_wqe_inline_data_seg {
	uint8_t inline_data[16];        /**< 00h..03h - Inline data. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE RDMA segment.
 */
struct flexio_dev_wqe_rdma_seg {
	__be64 raddr;   /**< 00h..01h - Remote address. */
	__be32 rkey;    /**< 02h - Remote key. */
	__be32 rsvd0;   /**< 03h - Reserved. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE ATOMIC segment.
 */
struct flexio_dev_wqe_atomic_seg {
	__be64 swap_or_add_data;        /**< 00h..01h - Swap or Add operation data. */
	__be64 compare_data;            /**< 02h..03h - Compare operation data. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE transpose segment.
 */
struct flexio_dev_wqe_transpose_seg {
	uint8_t rsvd0[0x3];     /**< 00h 8..31 - Reserved. */
	uint8_t element_size;   /**< 00h 0..7 - Matrix element size. */

	uint8_t rsvd1;          /**< 01h - Reserved. */
	uint8_t num_of_cols;    /**< 01h 16..22 - Number of columns in matrix (7b). */
	uint8_t rsvd2;          /**< 01h - Reserved. */
	uint8_t num_of_rows;    /**< 01h 0..6 - Number of rows in matrix (7b). */

	uint8_t rsvd4[0x8];     /**< 02h..03h - Reserved. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev wait on data WQE.
 */
struct flexio_dev_wqe_wait_on_data_seg {
	uint8_t rsvd0[0x3];        /**< 0h 2h Reserved. */
	uint8_t op_inv;            /**< 3h 0..3 Operation.
	                            *     4..4 Invert. */
	uint32_t lkey;             /**< 4h..7h lkey. */
	uint32_t va_63_32;         /**< 8h..bh bits 32..63 of the lkey address. */
	uint32_t va_31_3_fail_act; /**< 8h..fh 0..2 Fail operation.
	                            *         3..31 bits 3..31 of the lkey address. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev wait on data WQE - data portion.
 */
struct flexio_dev_wqe_wait_on_data_data_seg {
	__be64 data;             /**< 0h..7h Actual data for comparison. */
	__be64 data_mask;        /**< 8h..fh Mask for data. */
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev WQE Address Vector (Datagram segment) (for UD packets).
 * From infiniband/mlx5dv.h
 */
struct flexio_dev_wqe_datagram_seg {
	union {
		struct {
			union {
				struct {
					__be32 qkey;     /**< 0h - Queue key */
					__be32 reserved; /**< 4h - Reserved */
				} qkey;
				__be64 dc_key;           /**< 0h - DC Access key */
			} key;

			__be32 ext_dqp_dct;    /**< 08h 31 - Address Vector extension exist
			                        *       23..0 - Destination QP number or DCT */
			uint8_t stat_rate_sl;  /**< 0Ch 31..28 - Static rate
			                        *       27..24 - Service Level */
			uint8_t fl_mlid;       /**< 0Ch 23 - Force Loopback
			                        *       22..16 - Source LID */
			__be16 rlid;           /**< 0Ch 15..0 - Remote LID */
		} s1;

		struct {
			uint8_t reserved0[4]; /**< 10h - Reserved */
			uint8_t rmac[6];      /**< 14h..19h - Remote MAC */
			uint8_t tclass;       /**< 1Ah - GRH TClass */
			uint8_t hop_limit;    /**< 1Bh - GRH Hop Limit */
			__be32 grh_gid_fl;    /**< 1Ch 30 - GRH will be placed in the packet
			                       *       27..20 source GID
			                       *       19..0 Flow Label */
		} s2;

		struct {
			uint8_t rgid[16];    /**< 20h..2Ch - Remote GID */
		} s3;
	};
} __attribute__((packed, aligned(8)));

/**
 * Describes Flex IO dev send WQE segments.
 * Only one segment can be set at a given time.
 */
union flexio_dev_sqe_seg {
	struct flexio_dev_wqe_ctrl_seg ctrl;                            /**< Control segment. */
	struct flexio_dev_wqe_eth_seg eth;                              /**< Ethernet segment. */
	struct flexio_dev_wqe_inline_data_seg inline_data;              /**< Inline data segment. */
	struct flexio_dev_wqe_rdma_seg rdma;                            /**< RDMA segment. */
	struct flexio_dev_wqe_mem_ptr_send_data_seg mem_ptr_send_data;  /**< Memory pointer send
	                                                                 *  data segment. */
	struct flexio_dev_wqe_inline_send_data_seg inline_send_data;    /**< Inline send data
	                                                                 *  segment. */
	struct flexio_dev_wqe_atomic_seg atomic;                        /**< Atomic segment. */
	struct flexio_dev_wqe_transpose_seg transpose;                  /**< Transpose segment. */
	struct flexio_dev_wqe_shared_receive_seg shared_receive;        /**< Shared receive. */
	struct flexio_dev_wqe_wait_on_data_seg wait_on_data;            /**< wait on data. */
	struct flexio_dev_wqe_wait_on_data_data_seg wait_on_data_data;  /**< wait on data, data. */
	struct flexio_dev_wqe_datagram_seg adres_vector;                /**< Datagram */
};

_Static_assert(sizeof(union flexio_dev_sqe_seg) == 16, "Broken WQE segment union");

/** @} */

#endif /* _FLEXIO_DEV_QUEUE_TYPES_H_ */
