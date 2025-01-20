#pragma once
#include "common.h"
#include "request_def.h"
#include "math_utils.h"
#include "buffer.h"
#include <infiniband/verbs.h>

namespace nicc {
#define kWsQueueSize 1024

/**
 * \brief A lock-free queue for transferring buffer ownership within 
 * a component block, e.g., SoC cores in a same component block.
 * Typically, we adopt pipeline-parallelism within a component block, 
 * where each core can be an application or a dispatcher.
 * For TX direction, application is producer, and dispatcher is consumer. Application 
 * can only operate on the tail of the queue, and dispatcher can only operate
 * on the head of the queue.
 * For RX, dispatcher is producer, and application is consumer. Similarly, 
 * dispatcher can only operate on the tail of the queue, and application can
 * only operate on the head of the queue.q
 */
struct soc_shm_lock_free_queue {
    uint8_t* queue_[kWsQueueSize];
    volatile size_t head_ = 0;
    volatile size_t tail_ = 0;
    const size_t mask_ = kWsQueueSize - 1;  // Assuming kWsQueueSize is a power of 2
    public:
    soc_shm_lock_free_queue() {
        rt_assert(is_power_of_two<size_t>(kWsQueueSize), "The size of Ws Queue is not power of two.");
        memset(queue_, 0, sizeof(queue_));
    }
    inline bool enqueue(uint8_t *pkt) {
        size_t next_tail = (tail_ + 1) & mask_;
        if (next_tail == head_) return false;
        queue_[tail_] = pkt;
        tail_ = next_tail;
        return true;
    }
    inline uint8_t* dequeue() {
        if (head_ == tail_) return nullptr;
        uint8_t* ret = queue_[head_];
        head_ = (head_ + 1) & mask_;
        return ret;
    }
    inline void reset_head() {
        head_ = 0;
    }
    inline void reset_tail() {
        tail_ = 0;
    }
    inline size_t get_size() {
        return (tail_ - head_) & mask_;
    }
    inline bool is_empty() {
        return head_ == tail_;
    }
    inline bool is_full() {
        return (((tail_ + 1) & mask_) == head_);
    }
};

/**
 * \brief A RDMA-based SoC queue pair for transferring buffers between different component blocks.
 */
class RDMA_SoC_QP {
 public:
    static constexpr size_t kNumRxRingEntries = 2048;
    static_assert(is_power_of_two<size_t>(kNumRxRingEntries), "The num of RX ring entries is not power of two.");
    static constexpr size_t kNumTxRingEntries = 2048;
    static_assert(is_power_of_two<size_t>(kNumTxRingEntries), "The num of TX ring entries is not power of two.");
    static constexpr size_t kMTU = 1024;
    static_assert(is_power_of_two<size_t>(kMTU), "The size of MTU is not power of two.");

    struct ibv_cq *_send_cq = nullptr;
    struct ibv_cq *_recv_cq = nullptr;
    struct ibv_qp *_qp = nullptr;
    size_t _qp_id = SIZE_MAX;
    size_t _remote_qp_id = SIZE_MAX;
    /// An address handle for this endpoint's port. 
    struct ibv_ah *_remote_ah = nullptr;  ///< An address handle for the remote endpoint's port.

    /* SEND */
    struct ibv_send_wr _send_wr[kNumTxRingEntries];
    struct ibv_sge _send_sgl[kNumTxRingEntries];
    struct ibv_wc _send_wc[kNumTxRingEntries];
    size_t _send_head = 0;
    size_t _send_tail = 0;

    Buffer *_sw_ring[kNumTxRingEntries];
    Buffer *_tx_queue[kNumTxRingEntries];
    size_t _tx_queue_idx = 0;
    /* RECV */
    struct ibv_recv_wr _recv_wr[kNumRxRingEntries];
    struct ibv_sge _recv_sgl[kNumRxRingEntries];
    struct ibv_wc _recv_wc[kNumRxRingEntries];
    size_t _recv_head = 0;
    Buffer *_rx_ring[kNumRxRingEntries];
    size_t _ring_head = 0;

    // idx for ownership transfer between dispatcher and worker
    struct soc_shm_lock_free_queue *_worker_queue = nullptr;
    size_t _free_send_wr_num = kNumTxRingEntries;
    size_t _wait_for_disp = 0;
};
} // namespace nicc

