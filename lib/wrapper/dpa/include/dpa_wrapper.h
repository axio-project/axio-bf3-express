#ifndef DPA_WRAPPER_H_
#define DPA_WRAPPER_H_

#include <stddef.h>

// #include <libflexio-libc/stdio.h>
#include <libflexio-libc/string.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_err.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <dpaintrin.h>

#include "dpa_queue.h" 


/* ----------------------------User defined functions---------------------------- */
extern void process_packet(void);

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

// /*
//  * Initialize the RQ context
//  *
//  * @app_rq [in]: RQ HW context
//  * @ctx [out]: RQ context
//  */
// static void
// init_rq(const struct dpa_eth_wq app_rq, struct rq_ctx_t *ctx)
// {
// 	ctx->rq_number = app_rq.wq_num;
// 	ctx->rq_ring = (struct flexio_dev_wqe_rcv_data_seg *)app_rq.wq_ring_daddr;
// 	ctx->rq_dbr = (uint32_t *)app_rq.wq_dbr_daddr;
// }

// /*
//  * Initialize the SQ context
//  *
//  * @app_sq [in]: SQ HW context
//  * @ctx [out]: SQ context
//  */
// static void
// init_sq(const struct dpa_eth_wq app_sq, struct sq_ctx_t *ctx)
// {
// 	ctx->sq_number = app_sq.wq_num;
// 	ctx->sq_ring = (union flexio_dev_sqe_seg *)app_sq.wq_ring_daddr;
// 	ctx->sq_dbr = (uint32_t *)app_sq.wq_dbr_daddr;

// 	ctx->sq_wqe_seg_idx = 0;
// 	ctx->sq_dbr++;
// }

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
