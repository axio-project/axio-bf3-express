#ifndef DPA_QUEUE_H_
#define DPA_QUEUE_H_

//* Logarithm ring size */
#define DPA_LOG_SQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_RQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_CQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_WQ_DATA_ENTRY_BSIZE 11      // WQ buffer logarithmic size
#define DPA_LOG_WQE_BSIZE           6       // size of wqe inside SQ/RQ of DPA (sizeof(struct mlx5_wqe_data_seg))

/* Queues index mask, represents the index of the last CQE/WQE in the queue */
#define DPA_CQ_IDX_MASK ((1 << DPA_LOG_CQ_RING_DEPTH) - 1)
#define DPA_RQ_IDX_MASK ((1 << DPA_LOG_RQ_RING_DEPTH) - 1)
#define DPA_SQ_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
#define DPA_DATA_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH)) - 1)

/* ----------------------------Structure defination used on host---------------------------- */
// metadata of a dpa cq
struct dpa_cq {
    uint32_t cq_num;                /* CQ number */
    uint32_t log_cq_depth;          /* Depth of CQ in the logarithm */
    flexio_uintptr_t cq_ring_daddr; /* CQ ring address in DPA */
    flexio_uintptr_t cq_dbr_daddr;  /* CQ Doorbell register address in DPA */
} __attribute__((__packed__, aligned(8)));

// metadata of a dpa wq, used for Ethernet mode
struct dpa_eth_wq {
    uint32_t wq_num;                 /* WQ number */
    uint32_t wqd_mkey_id;            /* WQ MKEY ID */
    flexio_uintptr_t wq_ring_daddr;  /* WQ ring address in DPA */
    flexio_uintptr_t wq_dbr_daddr;   /* WQ Doorbell register address in DPA */
    flexio_uintptr_t wqd_daddr;      /* WQ data start address in DPA */
} __attribute__((__packed__, aligned(8)));

// metadata of a dpa qp, used for RDMA mode
struct dpa_qp {
    uint32_t qp_num;
    // uint32_t sqd_mkey_id;       
    // uint32_t rqd_mkey_id;       
    uint32_t qpd_mkey_id;               // as we use a unified memory for both sq and rq, we only need one mkey id
    uint32_t reserved;
    uint32_t log_qp_sq_depth;
    uint32_t log_qp_rq_depth;
    uint32_t sq_ci_idx;
    uint32_t rq_ci_idx;
    uint32_t sq_pi_idx;
    uint32_t rq_pi_idx;

    // uint64_t sqd_lkey;       
    // uint64_t rqd_lkey;
    uint64_t qpd_lkey;                  // as we use a unified memory for both sq and rq, we only need one lkey

    flexio_uintptr_t qp_dbr_daddr;      // doorbell record address
    flexio_uintptr_t qp_sq_daddr;       // SQ ring address
    flexio_uintptr_t qp_rq_daddr;       // RQ ring address
    // flexio_uintptr_t sqd_daddr;         // data buffer address of first SQ entry
    // flexio_uintptr_t rqd_daddr;         // data buffer address of first RQ entry
    flexio_uintptr_t qpd_daddr;          // first mbuf address of the unified memory (i.e., memory pool)
} __attribute__((__packed__, aligned(8)));

/* Transport data from HOST application to DEV application */
struct dpa_data_queues {
    uint8_t type;               // channel_typeid_t, defined in channel.h
    uint8_t reserved1[7];       // padding to ensure 8-byte alignment
    struct dpa_cq rq_cq_data;   // device RQ's CQ
    struct dpa_cq sq_cq_data;   // device SQ's CQ
    struct dpa_eth_wq rq_data;	    // device RQ, used for Ethernet mode
    struct dpa_eth_wq sq_data;	    // device SQ, used for Ethernet mode
	struct dpa_qp qp_data;	    // device QP, used for RDMA mode
} __attribute__((__packed__, aligned(8)));

/* ----------------------------Structure defination used on device---------------------------- */
/* Sample DPA CQ metadata structure.
 * Contains all data needed to DPA to work with CQ.
 */
struct cq_ctx_t {
	uint32_t cq_number;			/* CQ number */
	struct flexio_dev_cqe64 *cq_ring;	/* CQEs buffer */
	struct flexio_dev_cqe64 *cqe;		/* Current CQE */
	uint32_t cq_idx;			/* Current CQE IDX */
	uint8_t cq_hw_owner_bit;		/* HW/SW ownership */
	uint32_t *cq_dbr;			/* CQ doorbell record */
};

/* Sample DPA RQ metadata structure.
 * Contains all data needed to DPA to work with RQ.
 */
struct rq_ctx_t {
	uint32_t rq_number;				/* RQ number */
	struct flexio_dev_wqe_rcv_data_seg *rq_ring;	/* WQEs buffer */
	uint32_t *rq_dbr;				/* RQ doorbell record */
};

/* Sample DPA SQ metadata structure.
 * Contains all data needed to DPA to work with SQ.
 */
struct sq_ctx_t {
	uint32_t sq_number;			/* SQ number */
	uint32_t sq_wqe_seg_idx;		/* WQE segment index */
	union flexio_dev_sqe_seg *sq_ring;	/* SQEs buffer */
	uint32_t *sq_dbr;			/* SQ doorbell record */
	uint32_t sq_pi;				/* SQ producer index */
};

/* Sample DPA send metadata structure.
 * Contains SQ ring and data buffer for sending.
 */
struct dt_ctx_t {
	void *sq_tx_buff;	/* SQ TX buffer */
	uint32_t tx_buff_idx;	/* TX buffer index */
};

/* Device context */
static struct {
	uint32_t lkey;			/* Local memory key */
	uint32_t is_initalized;		/* Initialization flag */
	struct cq_ctx_t rqcq_ctx;	/* RQ CQ context */
	struct cq_ctx_t sqcq_ctx;	/* SQ CQ context */
	struct rq_ctx_t rq_ctx;		/* RQ context */
	struct sq_ctx_t sq_ctx;		/* SQ context */
	struct dt_ctx_t dt_ctx;		/* DT context */
	uint32_t packets_count;		/* Number of processed packets */
} dev_ctx = {0};

#endif
