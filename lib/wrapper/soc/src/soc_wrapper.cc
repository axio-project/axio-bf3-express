#include "soc_wrapper.h"

namespace nicc {

SoCWrapper::SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context) {
    if (type == kSoC_Invalid) {
        NICC_ERROR_C("Invalid SoC wrapper type");
        return;
    }
    this->_type = type;
    NICC_CHECK_POINTER(this->_context = context);
    #if SoC_QP_PRIOR_TYPE == ROCE_MODE
    NICC_CHECK_POINTER(this->_qp_for_prior = static_cast<RDMA_SoC_QP*>(context->qp_for_prior));
    #elif SoC_QP_PRIOR_TYPE == DPDK_MODE
    NICC_CHECK_POINTER(this->_qp_for_prior = static_cast<DPDK_SoC_QP*>(context->qp_for_prior));
    #endif
    #if SoC_QP_NEXT_TYPE == ROCE_MODE
    NICC_CHECK_POINTER(this->_qp_for_next = static_cast<RDMA_SoC_QP*>(context->qp_for_next));
    #elif SoC_QP_NEXT_TYPE == DPDK_MODE
    NICC_CHECK_POINTER(this->_qp_for_next = static_cast<DPDK_SoC_QP*>(context->qp_for_next));
    #endif
    // NICC_CHECK_POINTER(this->_tmp_worker_rx_queue = new soc_shm_lock_free_queue());
    // NICC_CHECK_POINTER(this->_tmp_worker_tx_queue = new soc_shm_lock_free_queue());
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
    
    // call user defined init handler if available
    if (this->_context->init_handler) {
        // user init_handler allocates and returns user_state with size info
        user_state_info state_info = this->_context->init_handler();
        this->_context->user_state = state_info.state;
        this->_context->user_state_size = state_info.size;
        
        if (this->_context->user_state) {
            NICC_LOG("User init handler called successfully, user_state allocated: size=%zu", 
                     this->_context->user_state_size);
        } else {
            NICC_WARN_C("User init handler returned null user_state");
            this->_context->user_state_size = 0;
        }
    } else {
        NICC_LOG("No user init handler registered");
        this->_context->user_state = nullptr;
        this->_context->user_state_size = 0;
    }
    
    /// run the SoCWrapper
    this->__run(30.0);
    return;
}

SoCWrapper::~SoCWrapper() {
    // call user defined cleanup handler if available
    if (this->_context->cleanup_handler && this->_context->user_state) {
        this->_context->cleanup_handler(this->_context->user_state);
        NICC_LOG("User cleanup handler called, user_state freed");
        this->_context->user_state = nullptr;
    } else if (this->_context->user_state) {
        NICC_WARN_C("user_state exists but no cleanup handler provided - potential memory leak");
    }
}

nicc_retval_t SoCWrapper::__init_dispatcher() {
    /// Allocate the SHM queue for transferring buffers between dispatcher and worker
    // this->_qp_for_prior->_disp_worker_queue = this->_tmp_worker_rx_queue;
    // this->_qp_for_next->_collect_worker_queue = this->_tmp_worker_tx_queue;
    if (this->_qp_for_prior->get_qp_type() == SoC_QP::QP_Type::DPDK || 
        this->_qp_for_next->get_qp_type() == SoC_QP::QP_Type::DPDK) {
        rte_thread_register();
    }
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

void SoCWrapper::__launch() {
    /* 1. RX Direction: prior component → worker → next component */
    size_t nb_rx = this->__rx_burst(this->_qp_for_prior);
    size_t nb_disp = this->__dispatch_rx_pkts(this->_qp_for_prior);

    // Worker processing (only for RDMA QP currently)
    this->__worker_process_batches_rdma(this->_qp_for_prior, this->_qp_for_next);

    // Collect processed packets and send
    size_t nb_collect = this->__collect_tx_pkts(this->_qp_for_prior, this->_qp_for_next);
    if (this->_qp_for_next->get_tx_queue_size() >= kTxBatchSize) {
        this->__tx_flush(this->_qp_for_next);
    }

    /* 2. TX Direction: next component → prior component (reverse path) */
    nb_rx = this->__rx_burst(this->_qp_for_next);
    size_t nb_direct_tx = this->__direct_tx_burst(this->_qp_for_next, this->_qp_for_prior);
    size_t nb_tx = this->__tx_flush(this->_qp_for_prior);
}

void SoCWrapper::__worker_process_batches_rdma(RDMA_SoC_QP *rx_qp, RDMA_SoC_QP *tx_qp) {
    size_t available = (rx_qp->_rx_sync_dispatch_head - rx_qp->_worker_rx_read_idx) & (nicc::kNumRxRingEntries - 1);

    if (available < kAppRxMsgBatchSize) {
        return;  // Not enough messages for a batch
    }
    
    // Process messages in batches
    size_t num_batches = available / kAppRxMsgBatchSize;
    
    for (size_t batch_idx = 0; batch_idx < num_batches; batch_idx++) {
        // Collect batch from rx_ring using worker_rx_read_idx
        Buffer* msg_batch[kAppRxMsgBatchSize];
        size_t start_idx = rx_qp->_worker_rx_read_idx;
        
        for (size_t i = 0; i < kAppRxMsgBatchSize; i++) {
            size_t idx = (start_idx + i) % nicc::kNumRxRingEntries;
            msg_batch[i] = rx_qp->_rx_sync_ring[idx];
        }
        
        // Call user defined message handler if available
        if (likely(this->_context->msg_handler)) {
            nicc_retval_t ret = this->_context->msg_handler(msg_batch, kAppRxMsgBatchSize, this->_context->user_state);
            if (unlikely(ret != NICC_SUCCESS)) {
                NICC_WARN_C("User batch msg handler failed: ret=%d, still forwarding all messages", ret);
            }
        }

        rx_qp->_worker_rx_read_idx = (start_idx + kAppRxMsgBatchSize) % nicc::kNumRxRingEntries;

        // Forward processed messages to TX queue (regardless of handler result)
        // for (size_t i = 0; i < kAppRxMsgBatchSize; i++) {
        //     msg_batch[i]->state_ = Buffer::kREADY_FOR_TX;
        // }
    }
}

size_t SoCWrapper::__rx_burst(RDMA_SoC_QP *qp) {
    /// post recvs first
    Buffer *ring_entry = qp->_rx_sync_ring[qp->_recv_head];
    size_t num_recvs = 0;
    while (ring_entry->state_ == Buffer::kFREE_BUF) {
        num_recvs++;
        ring_entry->state_ = Buffer::kPOSTED_PENDING;
        ring_entry = ring_entry->next_;
    }
    if (num_recvs) {
        this->__post_recvs(qp, num_recvs);
    }

    /// poll cq
    int ret = ibv_poll_cq(qp->_recv_cq, kRxBatchSize, qp->_recv_wc);
    /// set buffer's length
    for (int i = 0; i < ret; i++) {
        qp->_rx_sync_ring[(qp->_rx_sync_dispatch_head + qp->_wait_for_disp + i) % nicc::kNumRxRingEntries]->length_ = qp->_recv_wc[i].byte_len;
    }
    qp->_wait_for_disp += ret;

    return static_cast<size_t>(ret);
}

size_t SoCWrapper::__rx_burst(DPDK_SoC_QP *qp) {
    size_t nb_rx = 0;
    rte_mbuf **rx = &qp->_rx_queue[qp->_rx_queue_idx];
    nb_rx = rte_eth_rx_burst(qp->_phy_port, qp->_qp_id, rx, nicc::kNumRxRingEntries - qp->_rx_queue_idx);
    qp->_rx_queue_idx += nb_rx;
    return nb_rx;
}

size_t SoCWrapper::__dispatch_rx_pkts(RDMA_SoC_QP *qp) {
    size_t dispatch_total = qp->_wait_for_disp;
    if (unlikely(dispatch_total == 0)) {
        return 0;
    }
    
    // Mark buffers as APP_OWNED (worker will process them)
    // for (size_t i = 0; i < dispatch_total; i++) {
    //     size_t idx = (qp->_rx_sync_dispatch_head + i) % nicc::kNumRxRingEntries;
    //     qp->_rx_sync_ring[idx]->state_ = Buffer::kAPP_OPERATING;
    // }
    
    // Update ring head
    qp->_rx_sync_dispatch_head = (qp->_rx_sync_dispatch_head + dispatch_total) % nicc::kNumRxRingEntries;

    qp->_wait_for_disp = 0;
    
    return dispatch_total;
}

size_t SoCWrapper::__dispatch_rx_pkts(DPDK_SoC_QP *qp) {
    size_t dispatch_total = 0;
    rte_mbuf **rx = &qp->_rx_queue[0];
    struct soc_shm_lock_free_queue *worker_queue = qp->_disp_worker_queue;
    for (size_t i = 0; i < qp->_rx_queue_idx; i++) {
        if (unlikely(!worker_queue->enqueue((uint8_t*)rx[i]))) {
            /// drop the packet if the ws queue is full
            break;
        }
        dispatch_total++;
    }
    for (size_t i = dispatch_total; i < qp->_rx_queue_idx; i++) {
        rte_pktmbuf_free(rx[i]);
    }
    qp->_rx_queue_idx = 0;
    return dispatch_total;
}

size_t SoCWrapper::__collect_tx_pkts(RDMA_SoC_QP *prior_qp, RDMA_SoC_QP *next_qp) {
    // Collect processed packets (ready) to tx_queue
    size_t available = (prior_qp->_worker_rx_read_idx - prior_qp->_rx_sync_collect_head) & (nicc::kNumRxRingEntries - 1);
    if (unlikely(available == 0)) {
        return 0;
    }

    size_t start_idx = prior_qp->_rx_sync_collect_head;
    for (size_t i = 0; i < available; i++) {
        size_t idx = (start_idx + i) % nicc::kNumRxRingEntries;
        next_qp->_tx_queue[i + next_qp->_tx_queue_idx] = prior_qp->_rx_sync_ring[idx];
    }
    prior_qp->_rx_sync_collect_head = (start_idx + available) % nicc::kNumRxRingEntries;
    next_qp->_tx_queue_idx += available;

    return available;
}

size_t SoCWrapper::__collect_tx_pkts(DPDK_SoC_QP *qp) {
    size_t remain_ring_size = nicc::kNumTxRingEntries - qp->_tx_queue_idx;
    struct soc_shm_lock_free_queue *worker_queue = qp->_collect_worker_queue;
    size_t tx_size = (worker_queue->get_size() > remain_ring_size) 
                          ? remain_ring_size : worker_queue->get_size();
    for (size_t i = 0; i < tx_size; i++) {
        qp->_tx_queue[qp->_tx_queue_idx] = (rte_mbuf*)worker_queue->dequeue();
        qp->_tx_queue_idx++;
    }
    return tx_size;
}

size_t SoCWrapper::__tx_burst(RDMA_SoC_QP *qp, Buffer **tx, size_t tx_size) {
    // Mount buffers to send wr, generate corresponding sge
    size_t nb_tx_res = 0;   // total number of mounted wr for this burst tx`
    /// post send cq first
    int ret = ibv_poll_cq(qp->_send_cq, nicc::kNumTxRingEntries, qp->_send_wc);
    assert(ret >= 0);
    qp->_free_send_wr_num += ret;
    for (int i = 0; i < ret; i++) {
        qp->_sw_ring[qp->_send_head]->state_ = Buffer::kFREE_BUF;
        qp->_send_head = (qp->_send_head + 1) % nicc::kNumTxRingEntries;
    }
    /// post send wr
    struct ibv_send_wr* first_wr = &qp->_send_wr[qp->_send_tail];
    struct ibv_send_wr* tail_wr = nullptr;
    while (qp->_free_send_wr_num > 0 && nb_tx_res < tx_size) {
        tail_wr = &qp->_send_wr[qp->_send_tail];
        struct ibv_sge* sgl = &qp->_send_sgl[qp->_send_tail];
        Buffer *m = tx[nb_tx_res];
        sgl->addr = reinterpret_cast<uint64_t>(m->get_buf());
        sgl->length = m->length_;
        sgl->lkey = m->lkey_;
        /// \todo UD mode
        /// mount buffer to sw_ring
        qp->_sw_ring[qp->_send_tail] = m;
        qp->_send_tail = (qp->_send_tail + 1) % nicc::kNumTxRingEntries;
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

size_t SoCWrapper::__tx_flush(DPDK_SoC_QP *qp) {
    size_t nb_tx = 0, tx_total = 0;
    rte_mbuf **tx = &qp->_tx_queue[0];
    while(tx_total < qp->_tx_queue_idx) {
        nb_tx = rte_eth_tx_burst(qp->_phy_port, qp->_qp_id, tx, qp->_tx_queue_idx - tx_total);
        tx += nb_tx;
        tx_total += nb_tx;
    }
    qp->_tx_queue_idx = 0;
    return tx_total;
}

size_t SoCWrapper::__direct_tx_burst(RDMA_SoC_QP *rx_qp, RDMA_SoC_QP *tx_qp) {
    size_t remain_tx_queue_size = (nicc::kNumTxRingEntries - tx_qp->_tx_queue_idx > rx_qp->_wait_for_disp) 
                                    ? rx_qp->_wait_for_disp : nicc::kNumTxRingEntries - tx_qp->_tx_queue_idx;
    
    for (size_t i = 0; i < remain_tx_queue_size; i++) {
        Buffer *m = rx_qp->_rx_sync_ring[(rx_qp->_rx_sync_dispatch_head + i) % nicc::kNumRxRingEntries];
        tx_qp->_tx_queue[tx_qp->_tx_queue_idx] = m;
        tx_qp->_tx_queue_idx++;
    }
    rx_qp->_rx_sync_dispatch_head = (rx_qp->_rx_sync_dispatch_head + remain_tx_queue_size) % nicc::kNumRxRingEntries;
    rx_qp->_wait_for_disp -= remain_tx_queue_size;

    return remain_tx_queue_size;
}

size_t SoCWrapper::__direct_tx_burst(DPDK_SoC_QP *rx_qp, DPDK_SoC_QP *tx_qp) {
    size_t remain_tx_queue_size = (nicc::kNumTxRingEntries - tx_qp->_tx_queue_idx > rx_qp->_rx_queue_idx) 
                                    ? rx_qp->_rx_queue_idx : nicc::kNumTxRingEntries - tx_qp->_tx_queue_idx;
    for (size_t i = 0; i < remain_tx_queue_size; i++) {
        tx_qp->_tx_queue[tx_qp->_tx_queue_idx] = rx_qp->_rx_queue[rx_qp->_rx_queue_idx];
        tx_qp->_tx_queue_idx++;
    }
    rx_qp->_rx_queue_idx -= remain_tx_queue_size;
    return remain_tx_queue_size;
}

nicc_retval_t SoCWrapper::__forward_packet_with_routing(Buffer* packet, nicc_core_retval_t kernel_retval) {
    // TODO: Implement routing-based packet forwarding
    // For now, return success to maintain compatibility
    return NICC_SUCCESS;
    
    // if (!packet) {
    //     NICC_ERROR_C("Invalid packet buffer in __forward_packet_with_routing.");
    //     return NICC_ERROR;
    // }
    
    // if (!this->_context->routing) {
    //     NICC_ERROR_C("Routing not configured in SoCWrapper context.");
    //     return NICC_ERROR;
    // }
    
    // // Use ComponentRouting_SoC's forward_packet_after_kernel method
    // return this->_context->routing->forward_packet_after_kernel(
    //     packet, 
    //     kernel_retval, 
    //     reinterpret_cast<nicc::Channel*>(this->_qp_for_next)  // Use next QP as default channel
    // );
}

} // namespace nicc