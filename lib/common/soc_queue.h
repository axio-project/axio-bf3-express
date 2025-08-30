#pragma once
#include "common.h"
#include "log.h"
#include <infiniband/verbs.h>
#include <vector>
#include <unistd.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_thash.h>
#include <rte_flow.h>
#include <rte_ethdev.h>
#include <rte_hash.h>

#include "common/math_utils.h"
#include "common/buffer.h"
#include "common/iphdr.h"
// #include "common/ethhdr.h"

namespace nicc {

// Constants for SoC queue configuration
constexpr size_t kWsQueueSize = 1024;
constexpr size_t kNumRxRingEntries = 2048;
constexpr size_t kNumTxRingEntries = 2048;
constexpr size_t kMTU = 4096;

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
  /**
   * ----------------------Util methods----------------------
   */ 
 public:
    size_t get_tx_queue_size() {
      return this->_tx_queue_idx;
    }

    size_t get_rx_queue_size() {
      return this->_wait_for_disp;
    }

    size_t get_rx_worker_queue_size() {
      return this->_disp_worker_queue->get_size();
    }
    size_t get_tx_worker_queue_size() {
      return this->_collect_worker_queue->get_size();
    }

 public:

    static constexpr size_t kMaxPayloadSize = kMTU - sizeof(iphdr) - sizeof(udphdr);

    struct ibv_cq *_send_cq = nullptr;
    struct ibv_cq *_recv_cq = nullptr;
    struct ibv_qp *_qp = nullptr;
    size_t _qp_id = SIZE_MAX;
    size_t _remote_qp_id = SIZE_MAX;
    /// An address handle for this endpoint's port. 
    struct ibv_ah *_remote_ah = nullptr;  ///< An address handle for the remote endpoint's port.

    /* SEND */
    struct ibv_send_wr _send_wr[nicc::kNumTxRingEntries];
    struct ibv_sge _send_sgl[nicc::kNumTxRingEntries];
    struct ibv_wc _send_wc[nicc::kNumTxRingEntries];
    size_t _send_head = 0;
    size_t _send_tail = 0;

    Buffer *_sw_ring[nicc::kNumTxRingEntries];
    Buffer *_tx_queue[nicc::kNumTxRingEntries];
    size_t _tx_queue_idx = 0;
    /* RECV */
    struct ibv_recv_wr _recv_wr[kNumRxRingEntries];
    struct ibv_sge _recv_sgl[kNumRxRingEntries];
    struct ibv_wc _recv_wc[kNumRxRingEntries];
    size_t _recv_head = 0;
    Buffer *_rx_ring[kNumRxRingEntries];
    size_t _ring_head = 0;

    // idx for ownership transfer between dispatcher and worker
    soc_shm_lock_free_queue* _collect_worker_queue = nullptr;
    soc_shm_lock_free_queue* _disp_worker_queue = nullptr;
    size_t _free_send_wr_num = nicc::kNumTxRingEntries;
    size_t _wait_for_disp = 0;
};


/**
 * \brief A DPDK-based SoC queue pair for transferring buffers between different component blocks.
 */
class DPDK_SoC_QP {

  static constexpr size_t kMaxPayloadSize = kMTU - sizeof(iphdr) - sizeof(udphdr);
  static constexpr size_t kMaxPhyPorts = 2;
  static constexpr size_t kMaxQueuesPerPort = 32;
  static constexpr size_t kInvalidQpId = SIZE_MAX;

 public:
  struct ownership_memzone_t {
    private:
      std::mutex mutex_;  /// Guard for reading/writing to the memzone
      size_t epoch_;      /// Incremented after each QP ownership change attempt
      size_t num_qps_available_;

      struct {
        /// pid_ is the PID of the process that owns QP #i. Zero means
        /// the corresponding QP is free.
        int pid_;

        /// proc_random_id_ is a random number installed by the process that owns
        /// QP #i. This is used to defend against PID reuse.
        size_t proc_random_id_;
      } owner_[kMaxPhyPorts][kMaxQueuesPerPort];

    public:
      struct rte_eth_link link_[kMaxPhyPorts];  /// Resolved link status

      void init() {
        new (&mutex_) std::mutex();  // Fancy in-place construction
        num_qps_available_ = kMaxQueuesPerPort;
        epoch_ = 0;
        memset(owner_, 0, sizeof(owner_));
      }

      size_t get_epoch() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return epoch_;
      }

      size_t get_num_qps_available() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return num_qps_available_;
      }

      std::string get_summary(size_t phy_port) {
        const std::lock_guard<std::mutex> guard(mutex_);
        std::ostringstream ret;
        ret << "[" << num_qps_available_ << " QPs of " << kMaxQueuesPerPort
            << " available] ";

        if (num_qps_available_ < kMaxQueuesPerPort) {
          ret << "[Ownership: ";
          for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
            auto &owner = owner_[phy_port][i];
            if (owner.pid_ != 0) {
              ret << "[QP #" << i << ", "
                  << "PID " << owner.pid_ << "] ";
            }
          }
          ret << "]";
        }

        return ret.str();
      }

      /**
      * @brief Try to get a free QP
      *
      * @param phy_port The DPDK port ID to try getting a free QP from
      * @param proc_random_id A unique random process ID of the calling process
      *
      * @return If successful, the machine-wide global index of the free QP
      * reserved on phy_port. Else return kInvalidQpId.
      */
      size_t get_qp(size_t phy_port, size_t proc_random_id) {
        const std::lock_guard<std::mutex> guard(mutex_);
        epoch_++;
        const int my_pid = getpid();

        // Check for sanity
        for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
          auto &owner = owner_[phy_port][i];
          if (owner.pid_ == my_pid && owner.proc_random_id_ != proc_random_id) {
            NICC_ERROR(
                "Found another process with same PID (%d) as "
                "mine. Process random IDs: mine %zu, other: %zu\n",
                my_pid, proc_random_id, owner.proc_random_id_);
            return kInvalidQpId;
          }
        }

        for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
          auto &owner = owner_[phy_port][i];
          if (owner.pid_ == 0) {
            owner.pid_ = my_pid;
            owner.proc_random_id_ = proc_random_id;
            num_qps_available_--;
            return i;
          }
        }
        return kInvalidQpId;
      }

      /**
      * @brief Try to return a QP that was previously reserved from this
      * ownership manager
      *
      * @param phy_port The DPDK port ID to try returning the QP to
      * @param qp_id The QP ID returned by this manager during reservation
      *
      * @return 0 if success, else errno
      */
      int free_qp(size_t phy_port, size_t qp_id) {
        const std::lock_guard<std::mutex> guard(mutex_);
        const int my_pid = getpid();
        epoch_++;
        auto &owner = owner_[phy_port][qp_id];
        if (owner.pid_ == 0) {
          NICC_ERROR("PID %d tried to already-free QP %zu.\n",my_pid, qp_id);
          return EALREADY;
        }

        if (owner.pid_ != my_pid) {
          NICC_ERROR("PID %d tried to free QP %zu owned by PID %d. Disallowed.\n", my_pid, qp_id, owner.pid_);
          return EPERM;
        }

        num_qps_available_++;
        owner_[phy_port][qp_id].pid_ = 0;
        return 0;
      }

      /// Free-up QPs reserved by processes that exited before freeing a QP.
      /// This is safe, but it can leak QPs because of PID reuse.
      void daemon_reclaim_qps_from_crashed(size_t phy_port) {
        const std::lock_guard<std::mutex> guard(mutex_);

        for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
          auto &owner = owner_[phy_port][i];
          if (kill(owner.pid_, 0) != 0) {
            // This means that owner.pid_ is dead
            NICC_WARN("Reclaiming QP %zu from crashed PID %d\n",
                      i, owner.pid_);
            num_qps_available_++;
            owner_[phy_port][i].pid_ = 0;
          }
        }
      }
  };
  /**
   * ----------------------Util methods----------------------
   */ 

 public:
    size_t _qp_id = SIZE_MAX;
    rte_mempool *_mempool = nullptr;
    /// Info resolved from \p phy_port, must be filled by constructor.
    struct {
      ipaddr_t _ipv4_addr;   // The port's IPv4 address in host-byte order
      eth_addr _mac_addr;    // The port's MAC address
      size_t _bandwidth;     // Link bandwidth in bytes per second
      size_t _reta_size;     // Number of entries in NIC RX indirection table
    } resolve_;

    /// tx / rx queue
    struct rte_mbuf *_tx_queue[nicc::kNumTxRingEntries];
    struct rte_mbuf *_rx_queue[nicc::kNumRxRingEntries];
    size_t _tx_queue_idx = 0, _rx_queue_idx = 0;
};

} // namespace nicc

