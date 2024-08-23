#include "dpa_wrapper.h"

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
 * Called by host to initialize the device context
 *
 * @data [in]: pointer to the device context from the host
 * @return: This function always returns 0
 */
__dpa_rpc__ uint64_t
dpa_device_init(uint64_t data)
{
	struct dpa_data_queues *shared_data = (struct dpa_data_queues *)data;

	dev_ctx.lkey = shared_data->sq_data.wqd_mkey_id;
	init_cq(shared_data->rq_cq_data, &dev_ctx.rqcq_ctx);
	init_rq(shared_data->rq_data, &dev_ctx.rq_ctx);
	init_cq(shared_data->sq_cq_data, &dev_ctx.sqcq_ctx);
	init_sq(shared_data->sq_data, &dev_ctx.sq_ctx);

	dev_ctx.dt_ctx.sq_tx_buff = (void *)shared_data->sq_data.wqd_daddr;
	dev_ctx.dt_ctx.tx_buff_idx = 0;

	dev_ctx.is_initalized = 1;
	return 0;
}

/*
 * This function is called when a new packet is received to RQ's CQ.
 * Upon receiving a packet, the function will iterate over all received packets and process them.
 * Once all packets in the CQ are processed, the CQ will be rearmed to receive new packets events.
 */
void
__dpa_global__ dpa_event_handler(uint64_t __unused arg0)
{
	struct flexio_dev_thread_ctx *dtctx;

	flexio_dev_get_thread_ctx(&dtctx);

	if (dev_ctx.is_initalized == 0)
		flexio_dev_thread_reschedule();

	while (flexio_dev_cqe_get_owner(dev_ctx.rqcq_ctx.cqe) != dev_ctx.rqcq_ctx.cq_hw_owner_bit) {
		__dpa_thread_fence(__DPA_MEMORY, __DPA_R, __DPA_R);
		process_packet(dtctx);
		step_cq(&dev_ctx.rqcq_ctx, L2_CQ_IDX_MASK);
	}
	__dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
	flexio_dev_cq_arm(dtctx, dev_ctx.rqcq_ctx.cq_idx, dev_ctx.rqcq_ctx.cq_number);
	flexio_dev_thread_reschedule();
}
