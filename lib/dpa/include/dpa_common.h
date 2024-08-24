#ifndef DPA_COMMON_H_
#define DPA_COMMON_H_

/* Logarithm ring size */
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


// metadata of a dpa cq
struct dpa_cq {
    uint32_t cq_num;
    uint32_t log_cq_depth;
    flexio_uintptr_t cq_ring_daddr;
    flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));


// metadata of a dpa wq
struct dpa_wq {
    uint32_t wq_num;
    uint32_t wqd_mkey_id;
    flexio_uintptr_t wq_ring_daddr;
    flexio_uintptr_t wq_dbr_daddr;
    flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));


/* Transport data from HOST application to DEV application */
struct dpa_data_queues {
    struct dpa_cq rq_cq_data;   // device RQ's CQ
    struct dpa_wq rq_data;	    // device RQ
    struct dpa_cq sq_cq_data;   // device SQ's CQ
    struct dpa_wq sq_data;	    // device SQ
} __attribute__((__packed__, aligned(8)));

#endif