#include "soc_wrapper.h"

namespace nicc {

SoCWrapper::SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context) {
    if (type == kSoC_Invalid) {
        NICC_ERROR_C("Invalid SoC wrapper type");
        return;
    }
    this->_type = type;
    NICC_CHECK_POINTER(this->_context = context);
    NICC_CHECK_POINTER(this->_qp_for_prior = context->qp_for_prior);
    NICC_CHECK_POINTER(this->_qp_for_next = context->qp_for_next);
    NICC_CHECK_POINTER(this->_tmp_worker_rx_queue = new soc_shm_lock_free_queue());
    NICC_CHECK_POINTER(this->_tmp_worker_tx_queue = new soc_shm_lock_free_queue());
    if (type & kSoC_Dispatcher) {
        // init the dispatcher
        if (this->__init_dispatcher() != NICC_SUCCESS) {
            NICC_ERROR_C("Failed to initialize dispatcher");
            return;
        }
    } else if (type & kSoC_Worker) {
        // init the worker
        if (this->__init_worker() != NICC_SUCCESS) {
            NICC_ERROR_C("Failed to initialize worker");
            return;
        }
    }
    /// run the SoCWrapper
    this->__run(10.0);
    return;
}

nicc_retval_t SoCWrapper::__init_dispatcher() {
    /// Allocate the SHM queue for transferring buffers between dispatcher and worker
    this->_qp_for_prior->_disp_worker_queue = this->_tmp_worker_rx_queue;
    this->_qp_for_next->_collect_worker_queue = this->_tmp_worker_tx_queue;
    return NICC_SUCCESS;
}

nicc_retval_t SoCWrapper::__init_worker() {
    return NICC_SUCCESS;
}

void SoCWrapper::__run(double seconds) {
    double freq_ghz = measure_rdtsc_freq();
    size_t timeout_tsc = ms_to_cycles(1000*seconds, freq_ghz);
    size_t interval_tsc = us_to_cycles(1.0, freq_ghz);  // launch an event loop once per one us

    /* Start loop */
    size_t start_tsc = rdtsc();
    size_t loop_tsc = start_tsc;
    while (true) {
        if (rdtsc() - loop_tsc > interval_tsc) {
            loop_tsc = rdtsc();
            this->__launch();
        }
        if (unlikely(rdtsc() - start_tsc > timeout_tsc)) {
            /// Only the first workspace records the stats
            // update_stats(seconds);
            break;
        }
    }
    return;
}

/// \todo divide into worker and dispatcher, now we write in one function
void SoCWrapper::__launch() {
    /* 1. RX Direction, from piror component block to next component block */
    size_t nb_rx = this->__rx_burst(this->_qp_for_prior);
    size_t nb_disp = this->__dispatch_rx_pkts(this->_qp_for_prior);

    /// Worker Logic
    size_t worker_queue_size = this->_tmp_worker_rx_queue->get_size();
    /// \todo get packets per message within header; now assume 1 packet per message
    size_t msg_num = worker_queue_size / 1;
    if (msg_num > kAppRxMsgBatchSize) {
        /// handle received messages, \todo add user's msg handler

        /// dequeue from worker queue, and enqueue into next worker for pipelined processing
        for (size_t i = 0; i < msg_num * 1; i++) {
            Buffer *m = (Buffer*)this->_tmp_worker_rx_queue->dequeue();
            this->_tmp_worker_tx_queue->enqueue((uint8_t*)m);
        }
    }

    size_t nb_collect = this->__collect_tx_pkts(this->_qp_for_next);

    if (this->_qp_for_next->get_tx_queue_size() >= kTxBatchSize) {
        /// \todo use MAT to decide dst qp_id
        this->__tx_flush(this->_qp_for_next);
    }

    /* 2. TX Direction, from next component block to prior component block */
    nb_rx = this->__rx_burst(this->_qp_for_next);
    /// immediately send the packets
    size_t nb_direct_tx = this->__direct_tx_burst(this->_qp_for_next, this->_qp_for_prior);
    size_t nb_tx = this->__tx_flush(this->_qp_for_prior);
}

size_t SoCWrapper::__rx_burst(RDMA_SoC_QP *qp) {
    /// post recvs first
    Buffer *ring_entry = qp->_rx_ring[qp->_recv_head];
    size_t num_recvs = 0;
    while (ring_entry->state_ == Buffer::kFREE_BUF) {
        num_recvs++;
        ring_entry->state_ = Buffer::kPOSTED;
        ring_entry = ring_entry->next_;
    }
    if (num_recvs) {
        this->__post_recvs(qp, num_recvs);
    }

    /// poll cq
    int ret = ibv_poll_cq(qp->_recv_cq, kRxBatchSize, qp->_recv_wc);
    /// set buffer's length
    for (int i = 0; i < ret; i++) {
        qp->_rx_ring[(qp->_ring_head + qp->_wait_for_disp + i) % RDMA_SoC_QP::kNumRxRingEntries]->length_ = qp->_recv_wc[i].byte_len;
    }
    qp->_wait_for_disp += ret;

    return static_cast<size_t>(ret);
}

size_t SoCWrapper::__dispatch_rx_pkts(RDMA_SoC_QP *qp) {
    size_t dispatch_total = 0;
    Buffer *ring_entry = qp->_rx_ring[qp->_ring_head];
    struct soc_shm_lock_free_queue *worker_queue = qp->_disp_worker_queue;
    for (size_t i = 0; i < qp->_wait_for_disp; i++) {
        if (unlikely(!worker_queue->enqueue((uint8_t*)ring_entry))) {
            ring_entry->state_ = Buffer::kFREE_BUF;
            ring_entry = ring_entry->next_;
            continue;
        }
        ring_entry->state_ = Buffer::kAPP_OWNED_BUF;
        ring_entry = ring_entry->next_;
        dispatch_total++;
    }
    qp->_ring_head = (qp->_ring_head + qp->_wait_for_disp) % RDMA_SoC_QP::kNumRxRingEntries;
    qp->_wait_for_disp = 0;
    return dispatch_total;
}

size_t SoCWrapper::__collect_tx_pkts(RDMA_SoC_QP *qp) {
    size_t remain_ring_size = RDMA_SoC_QP::kNumTxRingEntries - qp->_tx_queue_idx;
    uint8_t nb_collect_queue = 0;
    size_t nb_collect_num = 0;
    struct soc_shm_lock_free_queue *worker_queue = qp->_collect_worker_queue;
    size_t tx_size = (worker_queue->get_size() > remain_ring_size) 
                          ? remain_ring_size : worker_queue->get_size();
    for (size_t i = 0; i < tx_size; i++) {
        qp->_tx_queue[qp->_tx_queue_idx] = (Buffer*)worker_queue->dequeue();
        qp->_tx_queue_idx++;
    }
    nb_collect_queue++;
    remain_ring_size -= tx_size;
    nb_collect_num += tx_size;
    return tx_size;
}

size_t SoCWrapper::__tx_burst(RDMA_SoC_QP *qp, Buffer **tx, size_t tx_size) {
    // Mount buffers to send wr, generate corresponding sge
    size_t nb_tx_res = 0;   // total number of mounted wr for this burst tx
    /// post send cq first
    int ret = ibv_poll_cq(qp->_send_cq, RDMA_SoC_QP::kNumTxRingEntries, qp->_send_wc);
    assert(ret >= 0);
    qp->_free_send_wr_num += ret;
    for (int i = 0; i < ret; i++) {
        qp->_sw_ring[qp->_send_head]->state_ = Buffer::kFREE_BUF;
        qp->_send_head = (qp->_send_head + 1) % RDMA_SoC_QP::kNumTxRingEntries;
    }
    /// post send wr
    struct ibv_send_wr* first_wr = &qp->_send_wr[qp->_send_tail];
    struct ibv_send_wr* tail_wr = nullptr;
    while (qp->_free_send_wr_num > 0 && nb_tx_res < tx_size) {
        tail_wr = &qp->_send_wr[qp->_send_tail];
        struct ibv_sge* sgl = &qp->_send_sgl[qp->_send_tail];
        Buffer *m = tx[nb_tx_res];
        m->state_ = Buffer::kPOSTED;
        sgl->addr = reinterpret_cast<uint64_t>(m->get_buf());
        sgl->length = m->length_;
        sgl->lkey = m->lkey_;
        /// \todo UD mode
        /// mount buffer to sw_ring
        qp->_sw_ring[qp->_send_tail] = m;
        qp->_send_tail = (qp->_send_tail + 1) % RDMA_SoC_QP::kNumTxRingEntries;
        qp->_free_send_wr_num--;
        nb_tx_res++;
    }
    if (nb_tx_res > 0) {
        struct ibv_send_wr* bad_send_wr;
        struct ibv_send_wr* temp_wr = tail_wr->next;
        tail_wr->next = nullptr; // Breaker of chains
        ret = ibv_post_send(qp->_qp, first_wr, &bad_send_wr);
        if (unlikely(ret != 0)) {
            NICC_ERROR_C("Post SEND (normal) error %d\n", ret);
        }
        tail_wr->next = temp_wr;  // Restore circularity
    }
    return nb_tx_res;
}

size_t SoCWrapper::__tx_flush(RDMA_SoC_QP *qp) {
    size_t nb_tx = 0, tx_total = 0;
    Buffer **tx = &qp->_tx_queue[0];
    while(tx_total < qp->_tx_queue_idx) {
        nb_tx = this->__tx_burst(qp, tx, qp->_tx_queue_idx - tx_total);
        tx += nb_tx;
        tx_total += nb_tx;
    }
    qp->_tx_queue_idx = 0;
    return tx_total;
}

size_t SoCWrapper::__direct_tx_burst(RDMA_SoC_QP *rx_qp, RDMA_SoC_QP *tx_qp) {
    size_t remain_tx_queue_size = (RDMA_SoC_QP::kNumTxRingEntries - tx_qp->_tx_queue_idx > rx_qp->_wait_for_disp) 
                                    ? rx_qp->_wait_for_disp : RDMA_SoC_QP::kNumTxRingEntries - tx_qp->_tx_queue_idx;
    
    for (size_t i = 0; i < remain_tx_queue_size; i++) {
        Buffer *m = rx_qp->_rx_ring[(rx_qp->_ring_head + i) % RDMA_SoC_QP::kNumRxRingEntries];
        tx_qp->_tx_queue[tx_qp->_tx_queue_idx] = m;
        tx_qp->_tx_queue_idx++;
    }
    rx_qp->_ring_head = (rx_qp->_ring_head + remain_tx_queue_size) % RDMA_SoC_QP::kNumRxRingEntries;
    rx_qp->_wait_for_disp -= remain_tx_queue_size;

    return remain_tx_queue_size;
}

} // namespace nicc