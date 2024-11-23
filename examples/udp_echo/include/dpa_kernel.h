#ifndef DPA_USER_H_
#define DPA_USER_H_

#include <stddef.h>

#include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <dpaintrin.h>

/* Logarithm ring size */
#define L2_LOG_SQ_RING_DEPTH 7 /* 2^7 entries */
#define L2_LOG_RQ_RING_DEPTH 7 /* 2^7 entries */
#define L2_LOG_CQ_RING_DEPTH 7 /* 2^7 entries */

#define L2_LOG_WQ_DATA_ENTRY_BSIZE 11 /* WQ buffer logarithmic size */

/* Queues index mask, represents the index of the last CQE/WQE in the queue */
#define L2_CQ_IDX_MASK ((1 << L2_LOG_CQ_RING_DEPTH) - 1)
#define L2_RQ_IDX_MASK ((1 << L2_LOG_RQ_RING_DEPTH) - 1)
#define L2_SQ_IDX_MASK ((1 << (L2_LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
#define L2_DATA_IDX_MASK ((1 << (L2_LOG_SQ_RING_DEPTH)) - 1)

/* ----------------------------Fake def. TODO: NICC LIB PROVIDE---------------------------- */
/* CQ Context */
struct cq_ctx_t {
	uint32_t cq_number;			/* CQ number */
	struct flexio_dev_cqe64 *cq_ring;	/* CQEs buffer */
	struct flexio_dev_cqe64 *cqe;		/* Current CQE */
	uint32_t cq_idx;			/* Current CQE IDX */
	uint8_t cq_hw_owner_bit;		/* HW/SW ownership */
	uint32_t *cq_dbr;			/* CQ doorbell record */
};

/* RQ Context */
struct rq_ctx_t {
	uint32_t rq_number;				/* RQ number */
	struct flexio_dev_wqe_rcv_data_seg *rq_ring;	/* WQEs buffer */
	uint32_t *rq_dbr;				/* RQ doorbell record */
};

/* SQ Context */
struct sq_ctx_t {
	uint32_t sq_number;			/* SQ number */
	uint32_t sq_wqe_seg_idx;		/* WQE segment index */
	union flexio_dev_sqe_seg *sq_ring;	/* SQEs buffer */
	uint32_t *sq_dbr;			/* SQ doorbell record */
	uint32_t sq_pi;				/* SQ producer index */
};

/* SQ data buffer */
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

/*
 * Get next data buffer entry
 *
 * @dt_ctx [in]: Data transfer context
 * @dt_idx_mask [in]: Data transfer segment index mask
 * @log_dt_entry_sz [in]: Log of data transfer entry size
 * @return: Data buffer entry
 */
static void *
get_next_dte(struct dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz)
{
	uint32_t mask = ((dt_ctx->tx_buff_idx++ & dt_idx_mask) << log_dt_entry_sz);
	char *buff_p =  (char *) dt_ctx->sq_tx_buff;

	return buff_p + mask;
}

/*
 * Get next SQE from the SQ ring
 *
 * @sq_ctx [in]: SQ context
 * @sq_idx_mask [in]: SQ index mask
 * @return: pointer to next SQE
 */
static void *
get_next_sqe(struct sq_ctx_t *sq_ctx, uint32_t sq_idx_mask)
{
	return &sq_ctx->sq_ring[sq_ctx->sq_wqe_seg_idx++ & sq_idx_mask];
}

#endif
