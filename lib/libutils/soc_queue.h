#pragma once
#include "common.h"
#include "request_def.h"
#include "math_utils.h"

namespace nicc {
/**
 * @brief A lock-free queue for storing Application-generated packets. 
 * For TX, application is producer, and dispatcher is consumer. Application 
 * can only operate on the tail of the queue, and dispatcher can only operate
 * on the head of the queue. 
 * For RX, dispatcher is producer, and application is consumer. Similarly, 
 * dispatcher can only operate on the tail of the queue, and application can
 * only operate on the head of the queue.
*/

#define kWsQueueSize 1024

struct lock_free_queue {
    request_t* queue_[kWsQueueSize];
    volatile size_t head_ = 0;
    volatile size_t tail_ = 0;
    const size_t mask_ = kWsQueueSize - 1;  // Assuming kWsQueueSize is a power of 2
    public:
    lock_free_queue() {
        rt_assert(is_power_of_two<size_t>(kWsQueueSize), "The size of Ws Queue is not power of two.");
        memset(queue_, 0, sizeof(queue_));
    }
    inline bool enqueue(request_t *pkt) {
        size_t next_tail = (tail_ + 1) & mask_;
        if (next_tail == head_) return false;
        queue_[tail_] = pkt;
        tail_ = next_tail;
        return true;
    }
    inline request_t* dequeue() {
        if (head_ == tail_) return nullptr;
        request_t* ret = queue_[head_];
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
} // namespace nicc
