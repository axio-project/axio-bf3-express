#include "soc_wrapper.h"

namespace nicc {

SoCWrapper::SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context) {
    if (type == kSoC_Invalid) {
        NICC_ERROR_C("Invalid SoC wrapper type");
        return;
    }
    this->_type = type;
    NICC_CHECK_POINTER(this->_context = context);
    NICC_CHECK_POINTER(this->_prior_qp = context->prior_qp);
    NICC_CHECK_POINTER(this->_next_qp = context->next_qp);
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
}

nicc_retval_t SoCWrapper::__init_dispatcher() {
    /// Allocate the SHM queue for transferring buffers between dispatcher and worker
    this->_prior_qp->_worker_queue = new soc_shm_lock_free_queue();
    this->_next_qp->_worker_queue = new soc_shm_lock_free_queue();
    return NICC_SUCCESS;
}

nicc_retval_t SoCWrapper::__init_worker() {
    return NICC_SUCCESS;
}

nicc_retval_t SoCWrapper::__run() {
    return NICC_SUCCESS;
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
    assert(ret >= 0);
    qp->_wait_for_disp += ret;

    return static_cast<size_t>(ret);
}

size_t SoCWrapper::__dispatch_rx_pkts(RDMA_SoC_QP *qp) {
    /// \todo dispatch rx_burst packets to worker rx queue; flush the rx queue
    struct soc_shm_lock_free_queue *worker_queue = qp->_worker_queue;
    size_t dispatch_total = 0;
    Buffer *ring_entry = qp->_rx_ring[qp->_ring_head];
    for (size_t i = 0; i < qp->_wait_for_disp; i++) {
        /// \todo resolve pkt header to get workload_type
        /// \todo get corresponding workspace id
        /// \todo get workspace rx queue
        if (unlikely(!worker_queue->enqueue((uint8_t*)ring_entry))) {
            /// \todo drop the packet if the ws queue is full
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
    size_t remain_ring_size = RDMA_SoC_QP::kMTU - qp->_tx_queue_idx;
    uint8_t nb_collect_queue = 0;
    size_t nb_collect_num = 0;
    /// \todo collect tx_burst packets from worker tx queues
    struct soc_shm_lock_free_queue *worker_queue = qp->_worker_queue;
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

size_t SoCWrapper::__tx_flush(RDMA_SoC_QP *qp) {
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
    while (qp->_free_send_wr_num > 0 && nb_tx_res < RDMA_SoC_QP::kNumTxRingEntries) {
        tail_wr = &qp->_send_wr[qp->_send_tail];
        struct ibv_sge* sgl = &qp->_send_sgl[qp->_send_tail];
        Buffer *m = qp->_tx_queue[qp->_tx_queue_idx];
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

} // namespace nicc