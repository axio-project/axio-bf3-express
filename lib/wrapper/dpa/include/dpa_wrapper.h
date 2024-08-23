#ifndef DPA_WRAPPER_H_
#define DPA_WRAPPER_H_

#include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <dpaintrin.h>

// #include "dpa_common.h" 

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

struct dpa_cq {
  uint32_t cq_num;
  uint32_t log_cq_depth;
  flexio_uintptr_t cq_ring_daddr;
  flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));

struct dpa_wq {
  uint32_t wq_num;
  uint32_t wqd_mkey_id;
  flexio_uintptr_t wq_ring_daddr;
  flexio_uintptr_t wq_dbr_daddr;
  flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));

/* Transport data from HOST application to DEV application */
struct dpa_data_queues {
  struct dpa_cq rq_cq_data; /* device RQ's CQ */
  struct dpa_wq rq_data;	/* device RQ */
  struct dpa_cq sq_cq_data; /* device SQ's CQ */
  struct dpa_wq sq_data;	/* device SQ */
} __attribute__((__packed__, aligned(8)));

/* ----------------------------User defined functions---------------------------- */
extern void process_packet(struct flexio_dev_thread_ctx *ctx);

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

/*
 * Initialize the CQ context
 *
 * @app_cq [in]: CQ HW context
 * @ctx [out]: CQ context
 */
static void
init_cq(const struct dpa_cq app_cq, struct cq_ctx_t *ctx)
{
	ctx->cq_number = app_cq.cq_num;
	ctx->cq_ring = (struct flexio_dev_cqe64 *)app_cq.cq_ring_daddr;
	ctx->cq_dbr = (uint32_t *)app_cq.cq_dbr_daddr;

	ctx->cqe = ctx->cq_ring; /* Points to the first CQE */
	ctx->cq_idx = 0;
	ctx->cq_hw_owner_bit = 0x1;
}

/*
 * Initialize the RQ context
 *
 * @app_rq [in]: RQ HW context
 * @ctx [out]: RQ context
 */
static void
init_rq(const struct dpa_wq app_rq, struct rq_ctx_t *ctx)
{
	ctx->rq_number = app_rq.wq_num;
	ctx->rq_ring = (struct flexio_dev_wqe_rcv_data_seg *)app_rq.wq_ring_daddr;
	ctx->rq_dbr = (uint32_t *)app_rq.wq_dbr_daddr;
}

/*
 * Initialize the SQ context
 *
 * @app_sq [in]: SQ HW context
 * @ctx [out]: SQ context
 */
static void
init_sq(const struct dpa_wq app_sq, struct sq_ctx_t *ctx)
{
	ctx->sq_number = app_sq.wq_num;
	ctx->sq_ring = (union flexio_dev_sqe_seg *)app_sq.wq_ring_daddr;
	ctx->sq_dbr = (uint32_t *)app_sq.wq_dbr_daddr;

	ctx->sq_wqe_seg_idx = 0;
	ctx->sq_dbr++;
}

/*
 * Increase consumer index of the CQ,
 * Once a CQE is polled, the consumer index is increased.
 * Upon completing a CQ epoch, the HW owner bit is flipped.
 *
 * @cq_ctx [in]: CQ context
 * @cq_idx_mask [in]: CQ index mask which indicates when the CQ is full
 */
static void
step_cq(struct cq_ctx_t *cq_ctx, uint32_t cq_idx_mask)
{
	cq_ctx->cq_idx++;
	cq_ctx->cqe = &cq_ctx->cq_ring[cq_ctx->cq_idx & cq_idx_mask];
	/* check for wrap around */
	if (!(cq_ctx->cq_idx & cq_idx_mask))
		cq_ctx->cq_hw_owner_bit = !cq_ctx->cq_hw_owner_bit;

	__dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
	flexio_dev_dbr_cq_set_ci(cq_ctx->cq_dbr, cq_ctx->cq_idx);
}

#endif
