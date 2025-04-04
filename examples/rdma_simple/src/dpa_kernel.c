#include "dpa_kernel.h"

/*
* This is the main function of the L2 reflector device, called on each packet from dpa_event_handler()
* Packet are received from the RQ, processed by changing MAC addresses and transmitted to the SQ.
*
* @dtctx [in]: This thread context
*/
void
process_packet(struct flexio_dev_thread_ctx *dtctx)
{
  // uint32_t rq_wqe_idx;
  // struct flexio_dev_wqe_rcv_data_seg *rwqe;
  // uint32_t data_sz;
  // char *rq_data;
  // char *sq_data;
  // union flexio_dev_sqe_seg *swqe;
  // const uint16_t mss = 0, checksum = 0;
  // char tmp;
  // /* MAC address has 6 bytes: ff:ff:ff:ff:ff:ff */
  // const int nb_mac_address_bytes = 6;

  // // flexio_dev_print("Processing packet!\n");
  // /* Extract relevant data from CQE */
  // rq_wqe_idx = flexio_dev_cqe_get_wqe_counter(dev_ctx.rqcq_ctx.cqe);
  // data_sz = flexio_dev_cqe_get_byte_cnt(dev_ctx.rqcq_ctx.cqe);

  // /* Get RQ WQE pointed by CQE */
  // rwqe = &dev_ctx.rq_ctx.rq_ring[rq_wqe_idx & DPA_RQ_IDX_MASK];

  // /* Extract data (whole packet) pointed by RQ WQE */
  // rq_data = flexio_dev_rwqe_get_addr(rwqe);

  // /* Take next entry from data ring */
  // sq_data = get_next_dte(&dev_ctx.dt_ctx, DPA_DATA_IDX_MASK, DPA_LOG_WQ_DATA_ENTRY_BSIZE);

  // /* Copy received packet to sq_data as is */
  // memcpy(sq_data, rq_data, data_sz);

  // /* swap mac addresses */
  // for (int byte = 0; byte < nb_mac_address_bytes; byte++) {
  //   tmp = sq_data[byte];
  //   sq_data[byte] = sq_data[byte + nb_mac_address_bytes];
  //   /* dst and src MACs are aligned one after the other in the ether header */
  //   sq_data[byte + nb_mac_address_bytes] = tmp;
  // }

  // /* Take first segment for SQ WQE (3 segments will be used) */
  // swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);

  // /* Fill out 1-st segment (Control) */
  // flexio_dev_swqe_seg_ctrl_set(swqe, dev_ctx.sq_ctx.sq_pi, dev_ctx.sq_ctx.sq_number,
  //           MLX5_CTRL_SEG_CE_CQE_ON_CQE_ERROR, FLEXIO_CTRL_SEG_SEND_EN);

  // /* Fill out 2-nd segment (Ethernet) */
  // swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);
  // flexio_dev_swqe_seg_eth_set(swqe, mss, checksum, 0, NULL);

  // /* Fill out 3-rd segment (Data) */
  // swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);
  // flexio_dev_swqe_seg_mem_ptr_data_set(swqe, data_sz, dev_ctx.lkey, (uint64_t)sq_data);

  // /* Send WQE is 4 WQEBBs need to skip the 4-th segment */
  // swqe = get_next_sqe(&dev_ctx.sq_ctx, DPA_SQ_IDX_MASK);

  /* Ring DB */
  // __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
  dev_ctx.sq_ctx.sq_pi++;
  flexio_dev_qp_sq_ring_db(dtctx, dev_ctx.sq_ctx.sq_pi, dev_ctx.sq_ctx.sq_number);
  // __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
  // flexio_dev_dbr_rq_inc_pi(dev_ctx.rq_ctx.rq_dbr);
}
