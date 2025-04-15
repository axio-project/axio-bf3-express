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
 * @file flexio.h
 * @page FlexIOHost Flex IO SDK host
 * @defgroup FlexioSDKHost Flex IO SDK host
 * Flex IO SDK host API for DPA programs.
 * Mostly used for DPA resource management and invocation of DPA programs.
 *
 * @{
 */

#ifndef _FLEXIO_SDK_H_
#define _FLEXIO_SDK_H_

#include <libflexio/flexio_ver.h>

#define FLEXIO_CURRENT_VERSION FLEXIO_VER(25, 1, 0)

#define FLEXIO_LAST_SUPPORTED_VERSION FLEXIO_VER(0, 0, 0)

#ifndef FLEXIO_VER_USED
#define FLEXIO_VER_USED FLEXIO_CURRENT_VERSION
#elif FLEXIO_VER_USED == FLEXIO_VER_LATEST
#undef FLEXIO_VER_USED
#define FLEXIO_VER_USED FLEXIO_CURRENT_VERSION
#endif

#if FLEXIO_VER_USED < FLEXIO_LAST_SUPPORTED_VERSION
#pragma GCC error "FLEXIO_VER_USED is less than FLEXIO_LAST_SUPPORTED_VERSION"
#elif FLEXIO_VER_USED == FLEXIO_LAST_SUPPORTED_VERSION
#pragma \
	GCC warning "FLEXIO_VER_USED is equal to FLEXIO_LAST_SUPPORTED_VERSION " \
	"and will become obsolete in the next release."
#endif

#define FLEXIO_STABLE __attribute__((visibility("default"))) __attribute__((used))
#ifndef FLEXIO_ALLOW_EXPERIMENTAL_API
/**
 * @brief To set a Symbol (or specifically a function) as experimental.
 */
#define FLEXIO_EXPERIMENTAL \
	__attribute__((deprecated("Symbol is defined as experimental"), \
		       section(".text.experimental"))) \
	FLEXIO_STABLE
#else /* FLEXIO_ALLOW_EXPERIMENTAL_API */
#define FLEXIO_EXPERIMENTAL __attribute__((section(".text.experimental"))) FLEXIO_STABLE
#endif /* FLEXIO_ALLOW_EXPERIMENTAL_API */

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* flexio SDK types and structs */

/** Flex IO address type. */
typedef uint64_t flexio_uintptr_t;

/* Flex IO device messaging. */
#define FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE 9
#define FLEXIO_MSG_DEV_MAX_STREAMS_AMOUNT 255
#define FLEXIO_MSG_DEV_DEFAULT_STREAM_ID 0
/* Flex IO device messaging levels. */
#define FLEXIO_MSG_DEV_NO_PRINT 0
#define FLEXIO_MSG_DEV_ALWAYS_PRINT 1
#define FLEXIO_MSG_DEV_ERROR 2
#define FLEXIO_MSG_DEV_WARN 3
#define FLEXIO_MSG_DEV_INFO 4
#define FLEXIO_MSG_DEV_DEBUG 5

typedef uint8_t flexio_msg_dev_level;

/** Element types for Flex IO CQ. */
enum {
	FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD = 0x0,
	FLEXIO_CQ_ELEMENT_TYPE_EXT_EQ     = 0x2,
	/* value 0x1 reserved */
	FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ           = 0x8,
	FLEXIO_CQ_ELEMENT_TYPE_DPA_MSIX_EMULATED_CQ = 0x9,
};

/** Flex IO API function return codes. */
typedef enum {
	FLEXIO_STATUS_SUCCESS   = 0,
	FLEXIO_STATUS_FAILED    = 1,
	FLEXIO_STATUS_TIMEOUT   = 2,
	FLEXIO_STATUS_FATAL_ERR = 3,
} flexio_status;

/** Flex IO QP transport service type. */
enum flexio_qp_transport_type {
	FLEXIO_QPC_ST_RC           = 0x0,
	FLEXIO_QPC_ST_UC           = 0x1,
	FLEXIO_QPC_ST_UD           = 0x2,
	FLEXIO_QPC_ST_XRC          = 0x3,
	FLEXIO_QPC_ST_IBL2         = 0x4,
	FLEXIO_QPC_ST_DCI          = 0x5,
	FLEXIO_QPC_ST_QP0          = 0x7,
	FLEXIO_QPC_ST_QP1          = 0x8,
	FLEXIO_QPC_ST_RAW_DATAGRAM = 0x9,
	FLEXIO_QPC_ST_REG_UMR      = 0xc,
	FLEXIO_QPC_ST_DC_CNAK      = 0x10,
};

enum flexio_cqe_comp_format {
	FLEXIO_CQE_FORMAT_RESP_BYTECOUNT_HASH               = 0x0,
	FLEXIO_CQE_FORMAT_RESP_BYTECOUNT_CKSUM              = 0x1,
	FLEXIO_CQE_FORMAT_RESP_FLOW_TAG                     = 0x2,
	FLEXIO_CQE_FORMAT_RESP_BYTECOUNT_CKSUM_STRIDE_INDEX = 0x3,
	FLEXIO_CQE_FORMAT_RESP_L3_L4_INFO                   = 0x4
};

/** Flex IO CQ CQE compression modes. */
enum flexio_cqe_comp_type {
	FLEXIO_CQE_COMP_NONE   = 0x0,
	FLEXIO_CQE_COMP_ENABLE = 0x2,
};

/** Flex IO CQ CQE compression period modes. */
enum flexio_cq_period_mode {
	FLEXIO_CQ_PERIOD_MODE_EVENT = 0x0,
	FLEXIO_CQ_PERIOD_MODE_CQE   = 0x1,
};

/** Flex IO UAR extension ID prototype. */
typedef uint32_t flexio_uar_device_id;

/** Flex IO application function prototype. */
typedef void (flexio_func_t) (void);

/** Flex IO process (opaque). */
struct flexio_process;
/** Flex IO window (opaque). */
struct flexio_window;
/** Flex IO outbox (opaque). */
struct flexio_outbox;
/** Flex IO event handler (opaque). */
struct flexio_event_handler;
/** Flex IO thread (opaque). */
struct flexio_thread;
/** Flex IO CQ (opaque). */
struct flexio_cq;
/** Flex IO RQ (opaque). */
struct flexio_rq;
/** Flex IO RMP (opaque). */
struct flexio_rmp;
/** Flex IO SQ (opaque). */
struct flexio_sq;
/** Flex IO SQ TIS (opaque). */
struct flexio_sq_tis;
/** Flex IO QP (opaque). */
struct flexio_qp;
/** Flex IO MKey (opaque). */
struct flexio_mkey;
/** Flex IO UAR (opaque). */
struct flexio_uar;
/** Flex IO application (opaque). */
struct flexio_app;
/** Flex IO command queue (opaque). */
struct flexio_cmdq;
/** Flex IO msg stream (opaque). */
struct flexio_msg_stream;
/** Flex IO transport domain (opaque). */
struct flexio_transport_domain;

/** Flex IO memory types. */
enum flexio_memtype {
	FLEXIO_MEMTYPE_DPA  = 0, /* Usage of zero value for DPA memory works as a default type */
	FLEXIO_MEMTYPE_HOST = 1,
};

/**
 * Describes queue memory, which may be either host memory or DPA memory
 */
struct flexio_qmem {
	enum flexio_memtype memtype;    /**< Type of memory to use (FLEXIO_MEMTYPE_DPA or
	                                 *  FLEXIO_MEMTYPE_HOST). */
	union {
		flexio_uintptr_t daddr; /**< DPA address of the queue memory (only valid for memtype
		                         *  FLEXIO_MEMTYPE_DPA). */
		uint64_t humem_offset;  /**< Address offset in the umem of the queue memory
		                         *  (only valid for memtype FLEXIO_MEMTYPE_HOST). */
	};
	uint32_t umem_id;               /**< UMEM ID of the queue memory. */
};

/**
 * Describes memory address
 */
struct flexio_mem {
	enum flexio_memtype mem_type;   /**< Type of memory to use (FLEXIO_MEMTYPE_DPA or
	                                 *  FLEXIO_MEMTYPE_HOST). */

	union {
		flexio_uintptr_t daddr; /**< DPA address (only valid for memtype
		                         *  FLEXIO_MEMTYPE_DPA). */
		uintptr_t haddr;        /**< Host address (only valid for memtype
		                         *  FLEXIO_MEMTYPE_HOST). */
	};
};

/**
 * Describes process heap memory information
 */
struct flexio_heap_mem_info {
	uint64_t base_addr; /**< Process heap memory base address. */
	size_t size;        /**< Process heap memory size in bytes. */
	size_t requested;   /**< Process heap memory requested in bytes. */
	size_t allocated;   /**< Process heap memory allocated in bytes. */
};

/**
 * Describes attributes for creating a Flex IO CQ.
 */
struct flexio_cq_attr {
	uint8_t log_cq_depth;                    /**< Log number of entries for the created CQ. */
	uint8_t element_type;                    /**< Type of the element attached to the created CQ
	                                          *  (thread, EQ, none, emulated EQ). */
	union {
		uint32_t emulated_eqn;           /**< Emulated EQ number to attach to the created
		                                  *  CQ */
		struct flexio_thread *thread;    /**< Thread object to attach to the created CQ
		                                  *  (only valid for element type thread). */
	};
	uint32_t uar_id;                         /**< CQ UAR ID (devx UAR ID for host queues,
	                                          *  otherwise flexio_uar). */
	void *uar_base_addr;                     /**< CQ UAR base address, relevant for devx UAR
	                                          *  only, otherwise must be NULL. */
	enum flexio_cqe_comp_type cqe_comp_type; /**< CQE compression type to use for the CQ. */
	enum flexio_cqe_comp_format cqe_comp_format; /**< CQE compression mini CQE format. */
	enum flexio_cq_period_mode cq_period_mode;   /**< CQE moderation period mode
	                                              *  (by CQE or by event). */
	uint16_t cq_period;                      /**< CQE moderation period
	                                          *  (number of usecs before creating an event). */
	uint16_t cq_max_count;                   /**< CQE moderation max count
	                                          *  (number of CQEs before creating an event). */
	bool no_arm;                             /**< Indication to not arm the CQ on creation. */
	bool cc;                                 /**< Indication to enable collapsed CQE for the
	                                          *  created CQ. */
	uint8_t overrun_ignore;                  /**< Indication to ignore overrun for the
	                                          *  created CQ. */
	uint8_t always_armed;                    /**< Indication to always arm for the created CQ */

	/* memory allocated for queue */
	struct flexio_qmem cq_ring_qmem;         /**< Ring memory info for the created CQ. */
	flexio_uintptr_t cq_dbr_daddr;           /**< DBR memory address for the created CQ. */
};

/**
 * Describes attributes for creating a Flex IO SQ.
 */
struct flexio_wq_sq_attr {
	uint8_t allow_multi_pkt_send_wqe;       /**< Indication enable multi packet send WQE for the
	                                         *  created SQ. */
	struct flexio_sq_tis *tis;              /**< TIS (transport interface send) object created
	                                         *  for the SQ. If null, a TIS object will be
	                                         *  created on SQ creation. */
};

/** Flex IO supported WQ types. */
enum flexio_wq_type {
	FLEXIO_WQ_TYPE_LINKED_LIST = 0x0,
	FLEXIO_WQ_TYPE_CYCLIC      = 0x1,
};

/** Flex IO supported WQ types. */
enum flexio_wq_end_pad_mode {
	FLEXIO_WQ_END_PAD_NONE  = 0x0,
	FLEXIO_WQ_END_PAD_ALIGN = 0x1,
};

/**
 * Describes attributes for creating a Flex IO RQ.
 */
struct flexio_wq_rq_attr {
	enum flexio_wq_type wq_type;              /**< RQ's WQ type. */
	enum flexio_wq_end_pad_mode end_pad_mode; /**< RQ's WQ end padding mode. */
	struct flexio_transport_domain *td;       /**< TD (transport domain) object created for the
	                                           *  RQ. If null, a TD object will be created on RQ
	                                           *  creation. TD is used for TIR creation. */
	uint8_t vlan_strip_disable;               /**< When set, the RQ vlan strip is disables. */
};

/**
 * Describes attributes for creating a Flex IO WQ.
 */
struct flexio_wq_attr {
	uint8_t log_wq_stride;                  /**< Log size of entry for the created WQ.
	                                         *  If this parameter is not provided,
	                                         *  it will be set to default value 4. */
	uint8_t log_wq_depth;                   /**< Log number of entries for the created WQ. */
	uint32_t uar_id;                        /**< WQ UAR ID. */
	uint32_t user_index;                    /**< User defined user_index for the created WQ. */
	struct ibv_pd *pd;                      /**< IBV protection domain struct to use for
	                                         *  creating the WQ. */
	union {
		struct flexio_wq_sq_attr sq;    /**< SQ attributes (used only for SQs). */
		struct flexio_wq_rq_attr rq;    /**< RQ attributes (used only for RQs). */
	};

	/* Memory allocated for queue. */
	struct flexio_qmem wq_ring_qmem;        /**< Ring memory info for the created WQ. */
	struct flexio_qmem wq_dbr_qmem;         /**< DBR memory address for the created WQ. */
};

/**
 * Describes attributes for creating a Flex IO process.
 */
struct flexio_process_attr {
	struct ibv_pd *pd;           /**< IBV protection domain information for the created
	                              *  process. Passing NULL will result in an internal
	                              *  PD being created and used for the process. */
	int en_pcc;                  /**< Enable PCC configuration for the created process. */
	const char *name;            /**< Name of the process. */
};

/** Flex IO thread affinity types. */
enum flexio_affinity_type {
	FLEXIO_AFFINITY_NONE = 0,
	FLEXIO_AFFINITY_STRICT,
	FLEXIO_AFFINITY_GROUP,
};

/**
 * Describes Flex IO thread affinity information.
 */
struct flexio_affinity {
	enum flexio_affinity_type type; /**< Affinity type to use for a Flex IO thread
	                                 *  (none, strict or group). */
	uint32_t id;                    /**< ID of the chosen resource (EU / DPA EU group).
	                                 *  Reserved if affinity type none is set. */
};

/**
 * Describes attributes for creating a Flex IO event handler.
 */
struct flexio_event_handler_attr {
	flexio_func_t *host_stub_func;                  /**< Stub for the entry function of the
	                                                 *  thread. */
	int continuable;                                /**< Thread continuable flag. */
	uint64_t arg;                                   /**< Thread argument. */
	flexio_uintptr_t thread_local_storage_daddr;    /**< Address of the local storage buffer of
	                                                 *  the thread. */
	struct flexio_affinity affinity;                /**< Thread's affinity information. */
	const char *name;                               /**< Name of event handler. */
};

/** Flex IO QP operation types. */
enum flexio_qp_op_types {
	FLEXIO_QP_WR_RDMA_WRITE          = 0x4,
	FLEXIO_QP_WR_RDMA_READ           = 0x8,
	FLEXIO_QP_WR_ATOMIC_CMP_AND_SWAP = 0x10,
};

/**
 * Describes QP modify operation mask.
 */
struct flexio_qp_attr_opt_param_mask {
	bool qp_access_mask;    /**< Indication to modify the QP's qp_access_mask field. */
	bool min_rnr_nak_timer; /**< Indication to modify the QP's min_rnr_nak_timer field. */
};

/** Flex IO QP possible MTU values. */
enum flexio_qp_qpc_mtu {
	FLEXIO_QP_QPC_MTU_BYTES_256 = 0x1,
	FLEXIO_QP_QPC_MTU_BYTES_512 = 0x2,
	FLEXIO_QP_QPC_MTU_BYTES_1K  = 0x3,
	FLEXIO_QP_QPC_MTU_BYTES_2K  = 0x4,
	FLEXIO_QP_QPC_MTU_BYTES_4K  = 0x5,
};

/** Flex IO QP RQ types. */
enum {
	FLEXIO_QP_QPC_RQ_TYPE_REGULAR             = 0x0,
	FLEXIO_QP_QPC_RQ_TYPE_SRQ_RMP_XRC_SRQ_XRQ = 0x1,
	FLEXIO_QP_QPC_RQ_TYPE_ZERO_SIZE_RQ        = 0x3,
};

/** Flex IO QP states. */
enum flexio_qp_state {
	FLEXIO_QP_STATE_RST  = 0x0,
	FLEXIO_QP_STATE_INIT = 0x1,
	FLEXIO_QP_STATE_RTR  = 0x2,
	FLEXIO_QP_STATE_RTS  = 0x3,
	FLEXIO_QP_STATE_ERR  = 0x6,
};

/**
 * Describes attributes for creating a Flex IO QP.
 */
struct flexio_qp_attr {
	enum flexio_qp_transport_type transport_type;  /**< transport type */

	uint32_t uar_id;                        /**< QP UAR ID. */
	uint32_t user_index;                    /**< User defined user_index for the created QP. */
	int qp_access_mask;                     /**< QP's access permission
	                                         *  (Expected values:
	                                         *   IBV_ACCESS_REMOTE_WRITE,
	                                         *   IBV_ACCESS_REMOTE_READ,
	                                         *   IBV_ACCESS_REMOTE_ATOMIC,
	                                         *   IBV_ACCESS_LOCAL_WRITE). */
	int ops_flag;                           /**< deprecated. Ignored.
	                                         *  For backward compatibility only */
	struct ibv_pd *pd;                      /**< IBV protection domain information for the
	                                         *  created QP. */
	int log_sq_depth;                       /**< Log number of entries of the QP's SQ. */
	int no_sq;                              /**< Indication to create the QP without an SQ. */
	uint32_t sq_cqn;                        /**< CQ number of the QP's SQ. */

	int log_rq_depth;                       /**< Log number of entries of the QP's RQ. */
	int rq_type;                            /**< QP's RQ type (regular, RMP, zero-RQ) */
	uint32_t rq_cqn;                        /**< CQ number of the QP's RQ. Not relevant
	                                         * for RMP */
	uint32_t rmpqn;                         /**< RMP queue number, relevant only if QP RQ is
	                                         *   RMP. */

	struct flexio_qmem qp_wq_buff_qmem;     /**< Ring memory info for the created QP's WQ. */
	struct flexio_qmem qp_wq_dbr_qmem;      /**< DBR memory info for the created QP's WQ. */

	/* RC QP modify attributes */
	enum flexio_qp_state next_state;        /**< QP state to move the QP to
	                                         *  (reset, init, RTS, RTR). */
	uint32_t remote_qp_num;                 /**< Remote QP number to set for the modified QP. */
	uint8_t gid_table_index;                /**< GID table index to set for the modified QP */
	uint8_t fl;                             /**< Indication to enable force loopback for the
	                                         *  modified QP. */
	uint8_t *dest_mac;                      /**< Destination MAC address to set for the
	                                         *  modified QP */
	union ibv_gid rgid_or_rip;              /**< Remote GID or remote IP to set for the
	                                         *  modified QP. */
	uint16_t rlid;                          /**< Remote LID to set for the modified QP. */
	uint32_t min_rnr_nak_timer;             /**< Minimal RNR NACK timer to set for the
	                                         *  modified QP. */
	enum flexio_qp_qpc_mtu path_mtu;        /**< Path MTU to set for the modified QP. */
	uint8_t retry_count;                    /**< Retry count to set for the modified QP. */
	uint8_t vhca_port_num;                  /**< VHCA port number to set for the modified QP. */
	uint32_t next_rcv_psn;                  /**< Next receive PSN to set for the modified QP. */
	uint32_t next_send_psn;                 /**< Next send PSN to set for the modified QP. */
	uint16_t udp_sport;                     /**< UDP port to set for the modified QP. */
	uint8_t grh;                            /**< GRH to set for the modified QP. */
	uint8_t log_rra_max;                    /**< Log of the number of allowed outstanding RDMA
	                                         *  read/atomic operations */
	uint8_t log_sra_max;                    /**< Log of the number of allowed outstanding RDMA
	                                         *  read/atomic operations as requester */
	uint8_t isolate_vl_tc;                  /**< When set, the QP will transmit on an isolated
	                                         *  VL/TC if available. */
	uint32_t q_key;                         /**< Q_Key to be validated against received
	                                         *  datagram. */
};

/**
 * Describes process attributes for creating a Flex IO MKey.
 */
struct flexio_mkey_attr {
	struct ibv_pd *pd;      /**< IBV protection domain information for the created MKey. */
	flexio_uintptr_t daddr; /**< DPA address the MKey is created for. */
	size_t len;             /**< Length of the address space the MKey is created for. */
	int access;             /**< access contains the access mask for the MKey
	                         *  (Expected values:
	                         *  IBV_ACCESS_REMOTE_WRITE, IBV_ACCESS_LOCAL_WRITE). */
};

/** Flex IO command queue states. */
enum flexio_cmdq_state {
	FLEXIO_CMDQ_STATE_PENDING = 0,
	FLEXIO_CMDQ_STATE_RUNNING = 1,
};

/**
 * Describes process attributes for creating a Flex IO command queue (async RPC).
 */
struct flexio_cmdq_attr {
	int workers;                    /**< Number of available workers, each worker can handle up
	                                 *  to batch_size number of tasks in a single invocation.
	                                 */
	int batch_size;                 /**< Number of tasks to be executed to completion by invoked
	                                 *  thread. */
	enum flexio_cmdq_state state;   /**< Command queue initial state. */
};

/**
 * Describes process attributes for creating a Flex IO application.
 */
struct flexio_app_attr {
	uint64_t *flexio_dev_versions;        /**< Array of flexio_dev_versions. */
	size_t flexio_dev_versions_len_size;  /**< Length of array of flexio_dev_versions. */
	uint64_t dpa_api_version;             /**< RTOS version. */
	const char *app_name;                 /**< DPA application name. */
	void *app_ptr;                        /**< Pointer to a buffer holds the DPA application. */
	size_t app_bsize;                     /**< DPA application size (bytes). */
	void *sig_ptr;                        /**< Pointer to a buffer holds the signature of the
	                                       * application. */
	size_t sig_bsize;                     /**< DPA application signature buffer size (bytes).
	                                       *  sig_bsize == 0 indicates no signature. */
};

/** Flex IO device messaging synchronization modes. */
enum flexio_msg_dev_sync_mode {
	FLEXIO_MSG_DEV_SYNC_MODE_SYNC   = 0,
	FLEXIO_MSG_DEV_SYNC_MODE_ASYNC  = 1,
	FLEXIO_MSG_DEV_SYNC_MODE_BATCH  = 2, /* Same as SYNC, saved for BWC */
	FLEXIO_MSG_DEV_SYNC_MODE_TRACER = 3,

	/* deprecated names for backward compatibility only,
	 * Never use them in modern code */
	FLEXIO_LOG_DEV_SYNC_MODE_SYNC   = 0,
	FLEXIO_LOG_DEV_SYNC_MODE_ASYNC  = 1,
	FLEXIO_LOG_DEV_SYNC_MODE_BATCH  = 2,
	FLEXIO_LOG_DEV_SYNC_MODE_TRACER = 3,
};

/** Flex IO device messaging transport modes. */
enum flexio_msg_transport {
	FLEXIO_MSG_TRANSPORT_QP_RC  = 0,
	FLEXIO_MSG_TRANSPORT_WINDOW = 1,
	FLEXIO_MSG_TRANSPORT_QP_UC  = 2,
	FLEXIO_MSG_TRANSPORT_QP_UD  = 3,

	/* deprecated names for backward compatibility only,
	 * Never use them in modern code */
	FLEXIO_TRACER_TRANSPORT_QP     = 0,
	FLEXIO_TRACER_TRANSPORT_WINDOW = 1,
};

/**
 * Describes DPA msg thread attributes for messaging from the Device to the Host side.
 */
typedef struct flexio_msg_stream_attr {
	struct flexio_uar *uar;                   /**< Deprecated field. Value will be ignored.
	                                           *  flexio_process UAR be used instead.
	                                           */
	size_t data_bsize;                        /**< Size of buffer, used for data transfer
	                                           *  from Flex IO to HOST MUST be power of
	                                           *  two and be at least 2Kb. */
	enum flexio_msg_dev_sync_mode sync_mode; /**< Select sync mode scheme. */

	const char *stream_name;                  /**< The name of the stream. */
	flexio_msg_dev_level level;               /**< Log level of the stream with the ranges
	                                           *  between FLEXIO_MSG_DEV_NO_PRINT and
	                                           *  FLEXIO_MSG_DEV_DEBUG.
	                                           *  FLEXIO_MSG_DEV_ALWAYS_PRINT cannot be used. */
	struct flexio_affinity mgmt_affinity;     /**< EU affinity for stream management operations
	                                           *  creation, modification and destruction
	                                           *  Passing a nullified struct will set affinity
	                                           *  type to 'NONE'. */
	union {
		enum flexio_msg_transport tracer_mode;    /**< deprecated name for backward
		                                           * compatibility only. Never use it */
		enum flexio_msg_transport transport_mode; /**< Transport mode. */
	};
	char *const *tracer_msg_formats;          /**< Tracer print format templates array, last
	                                           *  entry must be NULL. Device message format ID
	                                           *  is used as index to this array. */
} flexio_msg_stream_attr_t;

/**
 * Describes attributes for creating a Flex IO outbox.
 */
struct flexio_outbox_attr {
	struct flexio_uar *uar; /**< Deprecated field. Value will be ignored. flexio_process UAR
	                         *  will be used instead. */
	uint32_t en_pcc;        /**< Create outbox with support for CC operations. */
};

/** Flex IO HW model ID. */
enum flexio_hw_model_id {
	FLEXIO_HW_MODEL_DEF = 0,
	FLEXIO_HW_MODEL_CX7 = 1,
	FLEXIO_HW_MODEL_BF3 = 2,
	FLEXIO_HW_MODEL_CX8 = 3,
	FLEXIO_HW_MODEL_CX9 = 4,
	FLEXIO_HW_MODEL_NA  = 0xFFFF
};

/**
 * Describes attributes for selecting a Flex IO app.
 */
struct flexio_app_select_attr {
	const char *app_name;                /**< Application name to match. */
	enum flexio_hw_model_id hw_model_id; /**< HW model ID to match. */
	struct ibv_context *ibv_ctx;         /**< IBV device for selecting default HW model. */
};

/* Flex IO API Functions */

/**
 * @brief Allocates a buffer on Flex IO heap memory.
 *
 * This function allocates a buffer with the requested size on the Flex IO heap memory.
 * On success - sets dest_daddr_p to the start address of the allocated buffer.
 * On Failure - sets dest_daddr_p to 0x0.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[in]  buff_bsize - The size of the buffer to allocate.
 * @param[out] dest_daddr_p - A pointer to the Flex IO address, where the buffer was allocated.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_buf_dev_alloc(struct flexio_process *process,
						       size_t buff_bsize,
						       flexio_uintptr_t *dest_daddr_p);

/**
 * @brief Deallocates Flex IO heap memory buffer.
 *
 * This function frees Flex IO heap memory buffer by address.
 *
 * @param[in] process - A pointer to the Flex IO process context.
 * @param[in] daddr - A pointer to an address of allocated memory on the Flex IO heap.
 *                    Zero value is valid argument.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_buf_dev_free(struct flexio_process *process,
						      flexio_uintptr_t daddr);

/**
 * @brief Copy from host memory to a pre-allocted Flex IO heap memory buffer.
 *
 * This function copies data from a buffer on the host memory to a buffer on the Flex IO heap
 * memory.
 *
 * @param[in] process - A pointer to the Flex IO process context.
 * @param[in] src_haddr - An address of the buffer on the host memory.
 * @param[in] buff_bsize - The size of the buffer to copy.
 * @param[in] dest_daddr - Flex IO heap memory buffer address to copy to.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_host2dev_memcpy(struct flexio_process *process,
							 void *src_haddr,
							 size_t buff_bsize,
							 flexio_uintptr_t dest_daddr);

/**
 * @brief Memory copy.
 *
 * This function copies data from source buffer to destination buffer. Source and destination
 * addresses are allowed to be host or device buffers.
 *
 * @param[in] process - A pointer to the Flex IO process context.
 * @param[in] src - A pointer to source memory structure.
 * @param[in] dst - A pointer to destination memory structure.
 * @param[in] buff_bsize - The size of the buffer to copy.
 *
 * @return flexio status value.
 */
flexio_status flexio_memcpy(struct flexio_process *process, struct flexio_mem *src,
			    struct flexio_mem *dst, size_t buff_bsize);

/**
 * @brief Sets DPA heap memory buffer to a given value.
 *
 * @param[in] process - A pointer to the Flex IO process context.
 * @param[in] value - A value to set the DPA heap memory buffer to.
 * @param[in] buff_bsize - The size of the Flex IO heap memory buffer.
 * @param[in] dest_daddr - Flex IO heap memory buffer address to set.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_buf_dev_memset(struct flexio_process *process, int value,
							size_t buff_bsize,
							flexio_uintptr_t dest_daddr);

/**
 * @brief Copy from host memory to Flex IO heap memory buffer.
 *
 * This function copies data from a buffer on the host memory to the Flex IO memory.
 * The function allocates memory on the device heap which dest_address points to.
 * It is the caller responsibility to deallocate this memory when it is no longer used.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[in]  src_haddr - An address of the buffer on the host memory.
 * @param[in]  buff_bsize - The size of the buffer to copy.
 * @param[out] dest_daddr_p - A pointer to the Flex IO address, where the buffer was copied to.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_copy_from_host(struct flexio_process *process,
							void *src_haddr, size_t buff_bsize,
							flexio_uintptr_t *dest_daddr_p);

/**
 * @brief Allocates a recoverable buffer on Flex IO heap memory.
 *
 * This function allocates a recoverable buffer with the requested size on the Flex IO heap memory.
 * This buffer will force a memory object to be created and on object destroy will dump its content
 * to the user supplied buffer.
 *
 * Notes:
 * (1) The created memory object is in the resolution of DPA memory block size so if buff_bsize
 *     is not a multiple of DPA memory block size the actual allocation would round up.
 * (2) MKey size must match the buffer size (including rounding-up as mentioned above).
 *
 * On success - sets dest_daddr_p to the start address of the allocated buffer.
 * On Failure - sets dest_daddr_p to 0x0.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[in]  buff_bsize - The size of the buffer to allocate.
 * @param[in]  mkey - An MKey ID to the user buffer for memory dump on object destroy.
 * @param[out] dest_daddr_p - A pointer to the Flex IO address, where the buffer was allocated.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_recoverable_buf_dev_alloc(struct flexio_process *process,
								   size_t buff_bsize, uint32_t mkey,
								   flexio_uintptr_t *dest_daddr_p);

/**
 * @brief Get process memory info.
 *
 * This function returns the process heap memory base address and its available size.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[out] info - A pointer to flexio_heap_mem_info struct to fill info.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_process_mem_info_get(const struct flexio_process *process,
							      struct flexio_heap_mem_info *info);
/**
 * @brief Creates a Flex IO CQ.
 *
 * This function creates a Flex IO CQ.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (might be different than process').
 *                       If NULL - process' will be used.
 * @param[in]  fattr - A pointer to the CQ attributes struct.
 * @param[out] cq - A pointer to the created CQ context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cq_create(struct flexio_process *process,
						   struct ibv_context *ibv_ctx,
						   const struct flexio_cq_attr *fattr,
						   struct flexio_cq **cq);

/**
 * @brief Destroys a Flex IO CQ.
 *
 * This function destroys a Flex IO CQ.
 *
 * @param[in] cq - A pointer to a CQ context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cq_destroy(struct flexio_cq *cq);

/**
 * @brief Get the Flex IO CQ object.
 *
 * This function returns the Flex IO CQ object.
 *
 * @param[in] cq - A pointer to the CQ context.
 *
 * @return the CQ devx object.
 */
struct mlx5dv_devx_obj *flexio_cq_get_obj(struct flexio_cq *cq);

/**
 * @brief Queries a Flex IO CQ moderation configuration.
 *
 * @param[in]  cq - A pointer to a CQ context.
 * @param[out] max_count - A pointer to the CQ moderation max count value.
 * @param[out] period - A pointer to the CQ moderation period value.
 * @param[out] mode - A pointer to the CQ moderation mode value.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cq_query_moderation(struct flexio_cq *cq,
							     uint16_t *max_count,
							     uint16_t *period,
							     uint16_t *mode);

/**
 * @brief Modifies a Flex IO CQ moderation configuration.
 *
 * @param[in] cq - A pointer to a CQ context.
 * @param[in] max_count - CQ moderation max count value.
 * @param[in] period - CQ moderation period value.
 * @param[in] mode - CQ moderation mode value.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cq_modify_moderation(struct flexio_cq *cq,
							      uint16_t max_count,
							      uint16_t period,
							      uint16_t mode);

/**
 * @brief Creates an Mkey to the process device UMEM
 *
 * This function creates an MKey over the provided PD for the provided process device
 * UMEM.
 * The mkey_id will point to the field in the containing flexio_mkey object.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[in]  fattr - A pointer to a Flex IO MKey attribute struct.
 * @param[out] mkey - A pointer to a pointer to the created MKey struct.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_device_mkey_create(struct flexio_process *process,
							    struct flexio_mkey_attr *fattr,
							    struct flexio_mkey **mkey);

/**
 * @brief destroys an MKey object containing the given ID
 *
 * This function destroys an Mkey object containing the given ID.
 *
 * @param[in] mkey - A pointer to the Flex IO MKey to destroy. NULL is a valid value.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_device_mkey_destroy(struct flexio_mkey *mkey);

/**
 * @brief Creates a Flex IO UAR object
 *
 * This function creates a Flex IO UAR object.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 * @param[out] flexio_uar - A pointer to a pointer to the created Flex IO UAR struct.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_uar_create(struct flexio_process *process,
						    struct flexio_uar **flexio_uar);

/**
 * @brief destroys a Flex IO UAR object
 *
 * This function destroys a Flex IO UAR object.
 *
 * @param[in] uar - A pointer to the Flex IO UAR to destroy.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_uar_destroy(struct flexio_uar *uar);

/**
 * @brief Creates a Flex IO event handler.
 *
 * This function creates a Flex IO event handler for an existing Flex IO process.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  fattr - A pointer to the event handler attributes struct.
 * @param[out] event_handler_ptr - A pointer to the created event handler context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_event_handler_create(struct flexio_process *process,
					  struct flexio_event_handler_attr *fattr,
					  struct flexio_event_handler **event_handler_ptr);

/**
 * @brief Destroys a Flex IO event handler.
 *
 * This function destroys a Flex IO event handler.
 *
 * @param[in] event_handler - A pointer to an event handler context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_event_handler_destroy(struct flexio_event_handler *event_handler);

/**
 * @brief Run a Flex IO event handler.
 *
 * This function makes a Flex IO event handler start running.
 *
 * @param[in] event_handler - A pointer to an event handler context.
 * @param[in] user_arg - A 64 bit argument for the event handler's thread.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_event_handler_run(struct flexio_event_handler *event_handler,
				       uint64_t user_arg);

/**
 * @brief Query the Flex IO event handler activation id.
 *
 * This function returns the needed activation id in order to activate this event handler
 * by another thread of the same process.
 *
 * @param[in] event_handler - A pointer to an event handler context.
 *
 * @return the activation id or UINT32_MAX in case of error.
 */
FLEXIO_EXPERIMENTAL
uint32_t flexio_event_handler_get_activation_id(struct flexio_event_handler *event_handler);

/**
 * @brief Calls a Flex IO process.
 *
 * @param[in]  process - A pointer to the Flex IO process to run.
 * @param[in]  host_func - The host stub function that is used by the application to
 *                        reference the device function.
 * @param[out] func_ret - A pointer to the ELF function return value.
 * @param[in]  ... - Arguments (var argos).
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_process_call(struct flexio_process *process,
						      flexio_func_t *host_func,
						      uint64_t *func_ret, ...);

/**
 * @brief Create a new Flex IO process.
 *
 * This function creates a new Flex IO process with requested image.
 *
 * @param[in]  ibv_ctx - A pointer to a device context.
 * @param[in]  app - Device side application handle.
 * @param[in]  process_attr - Optional, process attributes for create. Can be NULL.
 * @param[out] process_ptr - A pointer to the created process pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_process_create(struct ibv_context *ibv_ctx, struct flexio_app *app,
				    const struct flexio_process_attr *process_attr,
				    struct flexio_process **process_ptr);

/**
 * @brief Set the Flexio process error handler.
 *
 * This function sets the Flex IO process error handler. The error handler must be set after
 * the process is created, and before the first thread is created.
 * The function registered for error handler should be annotated with __dpa_global__.
 *
 * @param[in] process - A pointer to a process
 * @param[in] error_handler - The host stub function that is used as a reference to the error
 *                            handler function.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_process_error_handler_set(struct flexio_process *process,
								   flexio_func_t *error_handler);

/**
 * @brief Create a DPA core dump of the process
 *
 * This function creates a core dump image of a process and all it's threads, and is intended
 * to be used after a fatal error or abnormal termination to allow the user to debug DPA
 * application code.
 *
 * There must be sufficient free memory to allocate 2-3 times the maximum core file size for
 * intermediate processing before the elf file is written.
 *
 * Memory windows that may be referenced by DPA code are *not* dumped by this code and
 * must be handled separately if the data is desired.
 *
 * @param[in] process - A pointer to a flexio_process
 * @param[in] outfile - pathname to write ELF formatted core dump data too.
 *                      If NULL - filename will be generated in form flexio_dev.NNN.core, where
 *                      NNN is the process id.
 *                      If outfile is not NULL - suffix .NNN.core will be added.
 *                      If outfile starts from slash (/pathname) - it will be passed with
 *                      suffix described above to fopen()
 *                      otherwise outfile will be created in the current directory or (if failed)
 *                      in /tmp directory
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_coredump_create(struct flexio_process *process,
							 const char *outfile);

/**
 * @brief Provide crash info in textual form
 *
 * This function displays useful crash info in textual form. Info will be printed on console and
 * duplicated to outfile
 *
 * @param[in] process - A pointer to a flexio_process
 * @param[in] outfile - pathname to write ELF formatted core dump data too.
 *                      If NULL - filename will be generated in form flexio_dev.NNN.crash, where
 *                      NNN is the process id.
 *                      If outfile is not NULL - suffix .NNN.crash will be added.
 *                      If outfile starts from slash (/pathname) - it will be passed with
 *                      suffix described above to fopen()
 *                      otherwise outfile will be created in the current directory or (if failed)
 *                      in /tmp directory
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_crash_data(struct flexio_process *process,
						    const char *outfile);

/**
 * @brief Destroys a Flex IO process.
 *
 * This function destroys a Flex IO process.
 *
 * @param[in] process - A pointer to a process. NULL is a valid value.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_process_destroy(struct flexio_process *process);

/**
 * @brief Creates a Flex IO RQ assuming cross device.
 *
 * This function creates a Flex IO RQ assuming cross VHCA_ID without checking.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (should be different than process').
 *                       If NULL or same as process - will result in an error.
 * @param[in]  cq_num - A CQ number.
 * @param[in]  fattr - A pointer to the RQ WQ attributes struct.
 * @param[out] flexio_rq_ptr - A pointer to the created RQ context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_rq_create_cross_dev(struct flexio_process *process,
					 struct ibv_context *ibv_ctx, uint32_t cq_num,
					 const struct flexio_wq_attr *fattr,
					 struct flexio_rq **flexio_rq_ptr);

/**
 * @brief Creates a Flex IO RQ.
 *
 * This function creates a Flex IO RQ.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (might be different than process').
 *                       If NULL - process' will be used.
 * @param[in]  cq_num - A CQ number.
 * @param[in]  fattr - A pointer to the RQ WQ attributes struct.
 * @param[out] flexio_rq_ptr - A pointer to the created RQ context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_rq_create(struct flexio_process *process,
						   struct ibv_context *ibv_ctx,
						   uint32_t cq_num,
						   const struct flexio_wq_attr *fattr,
						   struct flexio_rq **flexio_rq_ptr);

/**
 * @brief Sets a Flex IO RQ to error state.
 *
 * This function sets a Flex IO RQ to error state.
 *
 * @param[in] rq - A pointer to the RQ context to move to error state.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_rq_set_err_state(struct flexio_rq *rq);

/**
 * @brief Get the Flex IO RQ object.
 *
 * This function returns the Flex IO RQ object.
 *
 * @param[in] rq - A pointer to the RQ context.
 *
 * @return the RQ devx object.
 */
FLEXIO_EXPERIMENTAL struct mlx5dv_devx_obj *flexio_rq_get_object(struct flexio_rq *rq);

/**
 * @brief Destroys a Flex IO RQ.
 *
 * This function destroys a Flex IO RQ.
 *
 * @param[in] flexio_rq - A pointer to an RQ context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_rq_destroy(struct flexio_rq *flexio_rq);

/**
 * @brief Creates a Flex IO RMP.
 *
 * This function creates a Flex IO RMP.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (might be different than process').
 *                       If NULL - process' will be used.
 * @param[in]  fattr - A pointer to the WQ attributes struct.
 * @param[out] flexio_rmp_ptr - A pointer to the created RMP context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_rmp_create(struct flexio_process *process,
						    struct ibv_context *ibv_ctx,
						    const struct flexio_wq_attr *fattr,
						    struct flexio_rmp **flexio_rmp_ptr);

/**
 * @brief Destroys a Flex IO RMP.
 *
 * This function destroys a Flex IO RMP.
 *
 * @param[in] flexio_rmp - A pointer to an RMP context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_rmp_destroy(struct flexio_rmp *flexio_rmp);

/**
 * @brief Creates a Flex IO SQ assuming cross device.
 *
 * This function creates a Flex IO SQ assuming cross VHCA_ID without checking.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (should be different than process').
 *                       If NULL or same as process - will result in an error.
 * @param[in]  cq_num - A CQ number (can be Flex IO or host CQ).
 * @param[in]  fattr - A pointer to the SQ attributes struct.
 * @param[out] flexio_sq_ptr - A pointer to the created SQ context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_sq_create_cross_dev(struct flexio_process *process,
					 struct ibv_context *ibv_ctx, uint32_t cq_num,
					 const struct flexio_wq_attr *fattr,
					 struct flexio_sq **flexio_sq_ptr);

/**
 * @brief Creates a Flex IO SQ.
 *
 * This function creates a Flex IO SQ.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (might be different than process').
 *                       If NULL - process' will be used.
 * @param[in]  cq_num - A CQ number (can be Flex IO or host CQ).
 * @param[in]  fattr - A pointer to the SQ attributes struct.
 * @param[out] flexio_sq_ptr - A pointer to the created SQ context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_sq_create(struct flexio_process *process,
						   struct ibv_context *ibv_ctx,
						   uint32_t cq_num,
						   const struct flexio_wq_attr *fattr,
						   struct flexio_sq **flexio_sq_ptr);

/**
 * @brief Destroys a Flex IO SQ.
 *
 * This function destroys a Flex IO SQ.
 *
 * @param[in] flexio_sq - A pointer to an SQ context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_sq_destroy(struct flexio_sq *flexio_sq);

/**
 * @brief Creates a Flex IO SQ transport interface send (TIS) object.
 *
 * This function creates a Flex IO SQ TIS for allowing steering from an SQ.
 *
 * @param[in]  ibv_ctx - A pointer to an IBV device context. must be the same as the SQ's that will
 *                       use this TIS.
 * @param[in]  td - A pointer to a Flex IO transport domain struct. TD must be created for the same
 *                  IBV device context provided for TIS creation.
 * @param[out] tis - A pointer to the created SQ TIS context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_sq_tis_create(struct ibv_context *ibv_ctx,
						       struct flexio_transport_domain *td,
						       struct flexio_sq_tis **tis);

/**
 * @brief Destroys a Flex IO SQ TIS.
 *
 * This function destroys a Flex IO TIS.
 *
 * @param[in] tis - A pointer to an SQ TIS context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_sq_tis_destroy(struct flexio_sq_tis *tis);

/**
 * @brief Creates a Flex IO window.
 *
 * This function Creates a Flex IO window for the given process.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  pd - A pointer to a protection domain struct to the memory the window should access.
 * @param[out] window - A pointer to the created window context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_window_create(struct flexio_process *process,
						       struct ibv_pd *pd,
						       struct flexio_window **window);

/**
 * @brief Destroys a Flex IO window.
 *
 * This function destroys a Flex IO window.
 *
 * @param[in] window - A pointer to a window context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_window_destroy(struct flexio_window *window);

/**
 * @brief Creates a Flex IO outbox.
 *
 * This function Creates a Flex IO outbox for the given process.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  fattr - A pointer to the outbox attributes struct.
 * @param[out] outbox - A pointer to the created outbox context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_outbox_create(struct flexio_process *process,
						       struct flexio_outbox_attr *fattr,
						       struct flexio_outbox **outbox);

/**
 * @brief Destroys a Flex IO outbox.
 *
 * This function destroys a Flex IO outbox.
 *
 * @param[in] outbox - A pointer to a outbox context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_outbox_destroy(struct flexio_outbox *outbox);

/**
 * @brief Create a Flex IO msg stream that can contain output messages sent from the DPA.
 *
 * This function can create a flexio_msg_stream that could have device messages directed to it.
 * Directing messages from the device to the host, could be done to any and all open streams,
 * including the default stream.
 *
 * The function creates the same resources for any new stream.
 * It can also create the default stream. It creates it with the FLEXIO_MSG_DEV_INFO
 * stream level, and that could be modified using flexio_msg_stream_level_set.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  stream_fattr - A pointer to the flexio_msg_stream attributes struct.
 * @param[in]  out - file to save data from Flex IO. Use stdout if you want receive data
 *                   on HOST's console
 * @param[out] ppthread - A pointer to receive pthread ID of created thread. May be NULL if
 *                        user doesn't need it.
 * @param[out] stream - A pointer to the created stream context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_msg_stream_create(struct flexio_process *process,
							   flexio_msg_stream_attr_t *stream_fattr,
							   FILE *out,
							   pthread_t *ppthread,
							   struct flexio_msg_stream **stream);

/**
 * @brief Destroys a Flex IO msg stream.
 *
 * This function destroys any Flex IO msg stream.
 *
 * @param[in] stream - A pointer to the stream context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_msg_stream_destroy(struct flexio_msg_stream *stream);

/**
 * @brief Flush a msg stream's buffer in case of asynchronous messaging mode.
 *
 * All data from the msg stream buffer will be flushed to the file defined in
 * flexio_msg_stream_create().
 *
 * In case of synchronous device messaging this functions does nothing.
 * This function allocates resources to support messaging from Flex IO to HOST.
 *
 * @param[in] stream - A pointer to the Flex IO msg stream.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_msg_stream_flush(struct flexio_msg_stream *stream);

/**
 * @brief Gets the Flex IO device message stream's ID (aka file descriptor).
 *
 * Using this function on a destroyed stream will result in unpredictable behavior.
 * @param[in] stream - A pointer to a Flex IO message stream.
 *
 * @return the stream_id or -1 in case of error.
 */
FLEXIO_EXPERIMENTAL int flexio_msg_stream_get_id(struct flexio_msg_stream *stream);

/**
 * @brief Change the provided device message stream's level.
 *
 * The default stream's level cannot be altered.
 * Note that modifying the stream's level while messages are being sent may result in missing
 * or unwanted messages.
 *
 * @param[in] stream - A pointer to a Flex IO message stream.
 * @param[in] level - The new desired level, ranges between FLEXIO_MSG_DEV_NO_PRINT
 *                    FLEXIO_MSG_DEV_DEBUG. FLEXIO_MSG_DEV_ALWAYS_PRINT cannot be used here.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_msg_stream_level_set(struct flexio_msg_stream *stream,
							      flexio_msg_dev_level level);

/**
 * @brief Get file descriptor for error handler
 *
 * User should get fd in order to monitor for nonrecoverable errors
 *
 * User can poll all created processes, using select/poll/epoll
 * functions family.
 *
 * @param[in] process - A pointer to the Flex IO process.
 *
 * @return - file descriptor.
 */
FLEXIO_EXPERIMENTAL int flexio_err_handler_fd(struct flexio_process *process);

/**
 * @brief Check if unrecoverable error occurred
 *
 * It is suggested to check error status if file from flexio_err_handler_fd() reports about
 * existence data to read.
 *
 * Check error status before finishing process as well.
 *
 * @param[in] process - A pointer to the Flex IO process. NULL is a valid value.
 *
 * @return - nonzero value if error happen. See explanation of function flexio_dev_error()
 *           for agreement regarding error codes ranges (FW errors, FlexIO errors, User errors)
 */
FLEXIO_EXPERIMENTAL int flexio_err_status_get(struct flexio_process *process);

/**
 * @brief Creates a Flex IO QP.
 *
 * This function creates a Flex IO QP.
 *
 * @param[in]  process - A pointer to the Flex IO process.
 * @param[in]  ibv_ctx - A pointer to an IBV device context (might be different than process').
 *                       If NULL - process' will be used.
 * @param[in]  qp_fattr - A pointer to the QP attributes struct.
 * @param[out] qp_ptr - A pointer to the created QP context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_qp_create(struct flexio_process *process,
						   struct ibv_context *ibv_ctx,
						   struct flexio_qp_attr *qp_fattr,
						   struct flexio_qp **qp_ptr);

/**
 * @brief Modify Flex IO QP.
 *
 * This function modifies Flex IO QP and transition it between states.
 * At the end of the procedure Flex IO QP would have moved from it's current state to to next state,
 * given in the fattr, if the move is a legal transition in the QP's state machine.
 *
 * @param[in] qp - A pointer to the QP context.
 * @param[in] fattr - A pointer to the QP attributes struct that will also define the QP connection.
 * @param[in] mask - A pointer to the optional QP attributes mask.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_qp_modify(struct flexio_qp *qp,
						   struct flexio_qp_attr *fattr,
						   struct flexio_qp_attr_opt_param_mask *mask);

/**
 * @brief Destroys a Flex IO QP.
 *
 * This function destroys a Flex IO QP.
 *
 * @param[in] qp - A pointer to the QP context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_qp_destroy(struct flexio_qp *qp);

/**
 * @brief Get a list of FlexIO Apps that are available.
 *
 * This function returns a list of Flex IO apps that are loaded.
 *
 * @param[out]    app_list - A list of apps that are available.
 * @param[in,out] num_apps - number of apps to obtain / obtained.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_get_list(struct flexio_app ***app_list,
						      uint32_t *num_apps);

/**
 * @brief Free the list of flexio apps.
 *
 * This function frees the list of apps obtained from `flexio_app_get_list`.
 *
 * @param[in] apps_list - list obtained previously.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_list_free(struct flexio_app **apps_list);

/** Maximum length of application and device function names */
#define FLEXIO_MAX_NAME_LEN (256)
/**
 * @brief Create a container for a FlexIO App.
 *
 * This function creates a named app with a given ELF buffer.
 * It is called from within the constructor generated by the compiler.
 *
 * @param[in] fattr - A pointer to the application attributes struct.
 * @param[out] app - Created app.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_create(struct flexio_app_attr *fattr,
						    struct flexio_app **app);

/**
 * @brief Register a flexio app with a HW model ID.
 *
 * This function registers an existing Flex IO app with a Flex IO HW model ID.
 * It is called from within the constructor generated by the compiler.
 *
 * @param[in] app - App object that was created by flexio_app_create().
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_hw_model_register(struct flexio_app *app,
							       enum flexio_hw_model_id hw_model_id);

/**
 * @brief Destroy a flexio app.
 *
 * This function destroys the state associated with the app and all registered functions.
 * This function will free the internal elf buffer.
 * It is called from within the destructor generated by the compiler.
 *
 * @param[in] app - App that was created before.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_destroy(struct flexio_app *app);

/**
 * @brief Gets a Flex IO app from app list.
 *
 * This function gets a  Flex IO app by name and HW model from the app list.
 * The user may use this function in run time for dynamically select the application to run over the
 * current HW model.
 * Specifying FLEXIO_HW_MODEL_DEF as HW model ID will automatically select the best suited HW model.
 * In case user program doesn't reference an entry point from the device program, flexio_app_get()
 * will return NULL.
 *
 * @param[in]  fattr - A pointer to the application selection attributes struct.
 * @param[out] app - Selected app (NULL on failure).
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_get(struct flexio_app_select_attr *fattr,
						 struct flexio_app **app);

/**
 * @brief  Callback function to pack the arguments for a function.
 *
 * This function is called internally from the FlexIO runtime upon user
 * making a call (e.g., flexio_process_call).
 * It packs the arguments for a user function into the argument buffer provided in `argbuf`.
 * The argument list can be arbitrarily long and is represented by `ap`. The correct usage
 * of this function requires the caller to initialize the list using `va_start`.
 *
 * @param[in] argbuf - Argument buffer of appropriate size to pack into arguments.
 * @param[in] ap - Variable argument list.
 *
 * @return flexio status value.
 */
typedef void (flexio_func_arg_pack_fn_t) (void *argbuf, va_list ap);

/**
 * @brief Register a function name at application start.
 *
 * This function registers the function name, stub address with the runtime.
 * It is called from within the constructor generated by the compiler.
 * @param[in] app - App that created before.
 * @param[in] dev_func_name - The device function name (entry point).
 *                            Length of name should be up to FLEXIO_MAX_NAME_LEN bytes.
 * @param[in] dev_unpack_func_name - The device wrapper function that unpacks the argument buffer.
 *                                   Length of name should be up to FLEXIO_MAX_NAME_LEN bytes.
 * @param[in] host_stub_func_addr - The host stub function that is used by the application to
 *                                  reference the device function.
 * @param[in] argbuf_size - Size of the argument buffer required by this function.
 * @param[in] host_pack_func - Host callback function that packs the arguments.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_func_pup_register(struct flexio_app *app, const char *dev_func_name,
				       const char *dev_unpack_func_name,
				       flexio_func_t *host_stub_func_addr, size_t argbuf_size,
				       flexio_func_arg_pack_fn_t *host_pack_func);

/**
 * @brief Register a function to be used later.
 *
 * This function is intended to be called directly by user in the situation where they
 * don’t desire pack/unpack support that is typically done by the compiler interface.
 *
 * It is the user’s responsibility to ensure that a function was annotated for event handler
 * with __dpa_global__. The runtime will not provide any type checking.
 * A mismatched call will result in undefined behavior.
 *
 * @param[in]  app - previously created flexio app.
 * @param[in]  dev_func_name - name of flexio function on device that will be called.
 *                             Length of name should be up to FLEXIO_MAX_NAME_LEN bytes.
 * @param[out] out_func - opaque handle to use with flexio_process_call(),
 *                        flexio_event_handler_create(), …
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_func_register(struct flexio_app *app,
						       const char *dev_func_name,
						       flexio_func_t **out_func);

/**
 * @brief Obtain info for previously registered function.
 *
 * This function is used to obtain info about a previously registered function.
 * It is used to compose higher-level libraries on top of DPACC / FlexIO interface.
 * It is not intended to be used directly by the user.
 *
 * The caller must ensure that the string pointers have been allocated
 * and are at least `FLEXIO_MAX_NAME_LEN + 1` long to ensure that the call
 * doesn’t fail to copy full function name.
 *
 * @param[in]  app - FlexIO app.
 * @param[in]  host_stub_func_addr - Known host stub func addr.
 * @param[out] pup - Whether function has been registered with pack/unpack support (0: No, 1:Yes).
 * @param[out] dev_func_name - Name of device function.
 * @param[out] dev_unpack_func_name - Name of unpack routine on device, NA if pup == 0.
 * @param[in]  func_name_size - Size of function name len allocated.
 * @param[out] argbuf_size - Size of argument buffer, NA if pup == 0.
 * @param[out] host_pack_func - Function pointer to host packing routine, NA if pup == 0.
 * @param[out] dev_func_addr - address of device function.
 * @param[out] dev_unpack_func_addr - address of device unpack function.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_func_get_register_info(struct flexio_app *app,
					    flexio_func_t *host_stub_func_addr,
					    uint32_t *pup, char *dev_func_name,
					    char *dev_unpack_func_name, size_t func_name_size,
					    size_t *argbuf_size,
					    flexio_func_arg_pack_fn_t **host_pack_func,
					    flexio_uintptr_t *dev_func_addr,
					    flexio_uintptr_t *dev_unpack_func_addr);

/**
 * @brief Retrieve ELF binary associated with application.
 *
 * This function registers the function name, stub address with the runtime.
 * Compiler calls this from within the constructor.
 * @param[in] app - App that created before.
 * @param[in] bin_buff - Pointer to buffer to copy ELF binary.
 * @param[in] bin_size - Size of buffer pointed by bin_buff.
 *                       If parameter is smaller than ELF binary size function will fail.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_app_get_elf(struct flexio_app *app, uint64_t *bin_buff,
						     size_t bin_size);

/**
 * @brief Create asynchronous rpc command queue.
 *
 * This function creates the asynchronous rpc command queue infrastructure
 * allowing background tasks execution.
 *
 * @param[in]  process - A pointer to the process context.
 * @param[in]  fattr - A pointer to the command queue attributes struct.
 * @param[out] cmdq - A pointer to the created command queue context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cmdq_create(struct flexio_process *process,
						     struct flexio_cmdq_attr *fattr,
						     struct flexio_cmdq **cmdq);

/**
 * @brief Add a task to the asynchronous rpc command queue.
 *
 * This function adds a task to the asynchronous rpc command queue to be executed
 * by DPA in background.
 * allowing background jobs execution.
 *
 * @param[in] cmdq - A pointer to the command queue context.
 * @param[in] host_func - host stub function for DPA function to execute.
 * @param[in] arg - user argument to function.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cmdq_task_add(struct flexio_cmdq *cmdq,
						       flexio_func_t *host_func,
						       uint64_t arg);

/**
 * @brief Move command queue to running state.
 *
 * This function moves the command queue to running state in the case the queue was
 * create in pending state. Otherwise has no affect.
 *
 * @param[in] cmdq - A pointer to the command queue context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cmdq_state_running(struct flexio_cmdq *cmdq);

/**
 * @brief Check if command queue is empty.
 *
 * This function checks if the command queue is empty and all jobs up to this point
 * where performed.
 *
 * @param[in] cmdq - A pointer to the command queue context.
 *
 * @return boolean.
 */
FLEXIO_EXPERIMENTAL int flexio_cmdq_is_empty(struct flexio_cmdq *cmdq);

/**
 * @brief Destroy the command queue infrastructure.
 *
 * This function destroy the command queue infrastructure and release all its
 * resources.
 *
 * @param[in] cmdq - A pointer to the command queue context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_cmdq_destroy(struct flexio_cmdq *cmdq);

/**
 * @brief Create a Flex IO transport domain (TD).
 *
 * This function creates a TD for the given IBV device context.
 *
 * @param[in]  ibv_ctx - A pointer to an IBV device context.
 * @param[out] td - A pointer to the created TD context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_transport_domain_create(struct ibv_context *ibv_ctx,
					     struct flexio_transport_domain **td);

/**
 * @brief Destroy a Flex IO transport domain (TD).
 *
 * This function destroys a Flex IO TD.
 *
 * @param[in] td - A pointer to a TD context.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_transport_domain_destroy(struct flexio_transport_domain *td);

/* Helpers and Getters */

/**
 * @brief Gets a Flex IO thread object from a Flex IO event handler.
 *
 * @param[in] event_handler - A pointer to a Flex IO event handler.
 *
 * @return the event handler's thread or NULL on error.
 */
FLEXIO_EXPERIMENTAL
struct flexio_thread *flexio_event_handler_get_thread(struct flexio_event_handler *event_handler);

/**
 * @brief Gets the ID from a Flex IO event handler's thread metadata.
 *
 * @param[in] event_handler - A pointer to a Flex IO event handler.
 *
 * @return the event handler's thread ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL
uint32_t flexio_event_handler_get_id(struct flexio_event_handler *event_handler);

/**
 * @brief Gets the object ID of a Flex IO event handler.
 *
 * @param[in] event_handler - A pointer to a Flex IO event handler.
 *
 * @return the event handler's thread object ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL
uint32_t flexio_event_handler_get_obj_id(struct flexio_event_handler *event_handler);

/**
 * @brief Gets the thread object of a Flex IO event handler.
 *
 * @param[in] event_handler - A pointer to a Flex IO event handler.
 *
 * @return the event handler's thread object or null on error.
 */
FLEXIO_EXPERIMENTAL
struct mlx5dv_devx_obj *flexio_event_handler_get_thread_obj(
	struct flexio_event_handler *event_handler);

/**
 * @brief Gets the Flex IO CQ number.
 *
 * @param[in] cq - A pointer to a Flex IO CQ.
 *
 * @return the CQ number or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_cq_get_cq_num(struct flexio_cq *cq);

/**
 * @brief Gets the Flex IO RQ number.
 *
 * @param[in] rq - A pointer to a Flex IO RQ.
 *
 * @return the RQ number or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_rq_get_wq_num(struct flexio_rq *rq);

/**
 * @brief Gets the Flex IO RMP number.
 *
 * @param[in] rmp - A pointer to a Flex IO RMP.
 *
 * @return the RQ number or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_rmp_get_wq_num(struct flexio_rmp *rmp);

/**
 * @brief Gets the Flex IO RQ TIR object.
 *
 * @param[in] rq - A pointer to a Flex IO RQ.
 *
 * @return the RQ TIR object or NULL on error.
 */
FLEXIO_EXPERIMENTAL struct mlx5dv_devx_obj *flexio_rq_get_tir(struct flexio_rq *rq);

/**
 * @brief Gets the Flex IO SQ number.
 *
 * @param[in] sq - A pointer to a Flex IO SQ.
 *
 * @return the SQ number or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_sq_get_wq_num(struct flexio_sq *sq);

/**
 * @brief Gets the Flex IO QP number.
 *
 * @param[in] qp - A pointer to a Flex IO QP.
 *
 * @return the QP number or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_qp_get_qp_num(struct flexio_qp *qp);

/**
 * @brief Gets a Flex IO UAR object from a Flex IO outbox.
 *
 * @param[in] outbox - A pointer to a Flex IO outbox.
 *
 * @return the Flex IO outbox UAR object or NULL on error.
 */
FLEXIO_EXPERIMENTAL struct flexio_uar *flexio_outbox_get_uar(struct flexio_outbox *outbox);

/**
 * @brief Gets a Flex IO UAR object from a Flex IO process.
 *
 * @param[in] process - A pointer to a Flex IO process.
 *
 * @return the Flex IO process UAR object or NULL on error.
 */
FLEXIO_EXPERIMENTAL struct flexio_uar *flexio_process_get_uar(struct flexio_process *process);

/**
 * @brief Extend UAR to an ibv context.
 *
 * This function extend the UAR to an ibv context to allow handling its queues.
 *
 * @param[in]  in_uar - A pointer to the Flex IO uar.
 * @param[in]  to_extend - A pointer to an IBV device context to be extended to.
 * @param[out] extended - A pointer to the UAR context pointer.
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_uar_extend(struct flexio_uar *in_uar,
						    struct ibv_context *to_extend,
						    struct flexio_uar **extended);

/**
 * @brief Gets the Flex IO extended UAR ID.
 *
 * @param[in] uar - A pointer to a Flex IO extended UAR.
 *
 * @return the Flex IO UAR extended ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL flexio_uar_device_id flexio_uar_get_extended_id(struct flexio_uar *uar);

/**
 * @brief Gets the Flex IO UAR ID.
 *
 * @param[in] uar - A pointer to a Flex IO UAR.
 *
 * @return the Flex IO UAR ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_uar_get_id(struct flexio_uar *uar);

/**
 * @brief Gets the Flex IO outbox ID.
 *
 * @param[in] outbox - A pointer to a Flex IO outbox.
 *
 * @return the Flex IO outbox ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_outbox_get_id(struct flexio_outbox *outbox);

/**
 * @brief Gets the Flex IO window ID.
 *
 * @param[in] window - A pointer to a Flex IO window.
 *
 * @return the Flex IO window ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_window_get_id(struct flexio_window *window);

/**
 * @brief Gets the Flex IO MKey ID.
 *
 * @param[in] mkey - A pointer to a Flex IO MKey.
 *
 * @return the Flex IO mkey ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_mkey_get_id(struct flexio_mkey *mkey);

/**
 * @brief Gets a Flex IO application name.
 *
 * @param[in] app - A pointer to a Flex IO application.
 *
 * @return the application's name or NULL on error.
 */
FLEXIO_EXPERIMENTAL const char *flexio_app_get_name(struct flexio_app *app);

/**
 * @brief Gets a Flex IO application size.
 *
 * @param[in] app - A pointer to a Flex IO application.
 *
 * @return the application's size (bytes) or NULL on error.
 */
FLEXIO_EXPERIMENTAL size_t flexio_app_get_elf_size(struct flexio_app *app);

/**
 * @brief Gets a Flex IO IBV PD object from a Flex IO process.
 *
 * @param[in] process - A pointer to a Flex IO process.
 *
 * @return the process's PD object or NULL on error.
 */
FLEXIO_EXPERIMENTAL struct ibv_pd *flexio_process_get_pd(struct flexio_process *process);

/**
 * @brief Gets the Flex IO process DUMEM ID.
 *
 * @param[in] process - A pointer to a Flex IO process.
 *
 * @return the Flex IO process DUMEM ID or UINT32_MAX on error.
 */
FLEXIO_EXPERIMENTAL uint32_t flexio_process_get_dumem_id(struct flexio_process *process);

/* Host Logger API */
/** Flex IO SDK host logging levels */
typedef enum flexio_log_lvl {
	FLEXIO_LOG_LVL_ERR  = 0,
	FLEXIO_LOG_LVL_WARN = 1,
	FLEXIO_LOG_LVL_INFO = 2,
	FLEXIO_LOG_LVL_DBG  = 3,
} flexio_log_lvl_t;

/**
 * @brief Sets host SDK logging level.
 *
 * This function sets the host logging level. Changing the logging level may change the
 * visibility of some logging entries in the SDK code.
 *
 * @param[in] lvl - logging level to set. All entries with this or higher priority level will be
 *                  printed.
 *
 * @return the previous host logging level.
 */
FLEXIO_EXPERIMENTAL enum flexio_log_lvl flexio_log_lvl_set(enum flexio_log_lvl lvl);

/**
 * @brief retrieve the device QP state.
 *
 * This function return the device QP state it is currently in.
 *
 * @param[in] qp - A pointer to a Flex IO QP.
 *
 * @return enum flexio_qp_state.
 */
FLEXIO_EXPERIMENTAL enum flexio_qp_state flexio_qp_state_get(struct flexio_qp *qp);

/**
 * @brief Get token for Flex IO process debug access.
 *
 * This function returns the token, needed for user debug syscalls access.
 *
 * @param[in]  process - A pointer to the Flex IO process context.
 *
 * @return the requested token. Zero value means - User Debug access for the process is not allowed.
 */
FLEXIO_EXPERIMENTAL uint64_t flexio_process_udbg_token_get(struct flexio_process *process);

/**
 * @brief Set version for flexio.
 *
 * This function allows the library to determine which version it should work with.
 * The function is called without error only once; if called again with a different
 * version, it returns an error. The function also returns an error if it is called
 * with a version greater than the FLEXIO_CURRENT_VERSION.
 *
 * @param[in]  version - version to set in format FLEXIO_VER(major, minor, 0).
 *
 * @return flexio status value.
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_version_set(uint64_t version);

/**
 * @brief Get Flex IO HW model ID.
 *
 * This function gets the Flex IO HW model ID for the given IBV device.
 *
 * @param[in] ctx - IBV device to query.
 *
 * @return flexio HW model ID value.
 */
FLEXIO_EXPERIMENTAL enum flexio_hw_model_id flexio_hw_model_id_get(struct ibv_context *ibv_ctx);

/*********************************************************************************************
 * Necropolis of deprecated code starts here
 * Stuff here need for backward compatibility only
 * Never use it in modern code
 *********************************************************************************************/

/* Deprecated structures and enumerations names */
#define flexio_log_dev_attr flexio_msg_stream_attr
#define flexio_log_dev_sync_mode flexio_msg_dev_sync_mode
#define flexio_tracer_transport flexio_msg_transport

/* Deprecated type definitions */
typedef enum flexio_msg_dev_sync_mode flexio_msg_dev_sync_mode;

/* Deprecated functions prototypes */
/**
 * @brief Deprecated function. Use flexio_msg_stream_create() instead
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_log_dev_init(struct flexio_process *process,
				  flexio_msg_stream_attr_t *stream_fattr, FILE *out,
				  pthread_t *ppthread) __attribute__ ((deprecated));

/**
 * @brief Deprecated function. Use flexio_msg_stream_flush() instead
 */
FLEXIO_EXPERIMENTAL
flexio_status flexio_log_dev_flush(struct flexio_process *process) __attribute__ ((deprecated));

/**
 * @brief Deprecated function. Use flexio_msg_stream_destroy() instead
 */
FLEXIO_EXPERIMENTAL flexio_status flexio_log_dev_destroy(struct flexio_process *process)
__attribute__ ((deprecated));

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* _FLEXIO_SDK_H_ */
