#include "dpa_kernel.h"

#define SWAP(a, b) \
	do { \
		__typeof__(a) tmp;  \
		tmp = a;            \
		a = b;              \
		b = tmp;            \
	} while (0)

/* Swap source and destination MAC addresses in the packet.
 *  packet - pointer to the packet.
 */
static void swap_macs(char *packet)
{
	char *dmac, *smac;
	int i;

	dmac = packet;
	smac = packet + 6;
	for (i = 0; i < 6; i++, dmac++, smac++)
		SWAP(*smac, *dmac);
}

/*
* This is the main function of the L2 reflector device, called on each packet from dpa_event_handler()
* Packet are received from the RQ, processed by changing MAC addresses and transmitted to the SQ.
*/
void process_packet(void);
void process_packet(void)
{
	/* RX packet handling variables */
	struct flexio_dev_wqe_rcv_data_seg *rwqe;
	/* RQ WQE index */
	uint32_t rq_wqe_idx;
	/* Pointer to RQ data */
	char *rq_data;

	/* TX packet handling variables */
	union flexio_dev_sqe_seg *swqe;
	/* Pointer to SQ data */
	char *sq_data;

	/* Size of the data */
	uint32_t data_sz;

	/* Extract relevant data from the CQE */
	rq_wqe_idx = flexio_dev_cqe_get_wqe_counter(dev_ctx.rqcq_ctx.cqe);
	data_sz = flexio_dev_cqe_get_byte_cnt(dev_ctx.rqcq_ctx.cqe);

	/* Get the RQ WQE pointed to by the CQE */
	rwqe = &dev_ctx.rq_ctx.rq_ring[rq_wqe_idx & DPA_RQ_IDX_MASK];

	/* Extract data (whole packet) pointed to by the RQ WQE */
	rq_data = flexio_dev_rwqe_get_addr(rwqe);

	/* Take the next entry from the data ring */
	sq_data = get_next_dte(&dev_ctx.dt_ctx, DPA_DATA_IDX_MASK, DPA_LOG_WQ_DATA_ENTRY_BSIZE);

	/* Copy received packet to sq_data as is */
	memcpy(sq_data, rq_data, data_sz);

	/* swap mac address */
	swap_macs(sq_data);

	/* Primitive validation, that packet is our hardcoded */
	if (data_sz == 65) {
		/* modify UDP payload */
		memcpy(sq_data + 0x2a, "  Event demo***************", 65 - 0x2a);

		/* Set hexadecimal value by the index */
		sq_data[0x2a] = "0123456789abcdef"[dev_ctx.dt_ctx.tx_buff_idx & 0xf];
	}

	/* Take first segment for SQ WQE (3 segments will be used) */
	swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);

	/* Fill out 1-st segment (Control) */
	flexio_dev_swqe_seg_ctrl_set(swqe, dev_ctx.sq_ctx.sq_pi, dev_ctx.sq_ctx.sq_number,
				     MLX5_CTRL_SEG_CE_CQE_ON_CQE_ERROR, FLEXIO_CTRL_SEG_SEND_EN);

	/* Fill out 2-nd segment (Ethernet) */
	swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);
	flexio_dev_swqe_seg_eth_set(swqe, 0, 0, 0, NULL);

	/* Fill out 3-rd segment (Data) */
	swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);
	flexio_dev_swqe_seg_mem_ptr_data_set(swqe, data_sz, dev_ctx.lkey, (uint64_t)sq_data);

	/* Send WQE is 4 WQEBBs need to skip the 4-th segment */
	swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);

	/* Ring DB */
	__dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
	flexio_dev_qp_sq_ring_db(++dev_ctx.sq_ctx.sq_pi, dev_ctx.sq_ctx.sq_number);
	__dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
	flexio_dev_dbr_rq_inc_pi(dev_ctx.rq_ctx.rq_dbr);
}
