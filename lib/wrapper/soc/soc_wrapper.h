#pragma once
#include "common.h"
#include "log.h"
#include "common/soc_queue.h"
#include "common/timer.h"
#include "ctrlpath/route_impl/soc_routing.h"

namespace nicc {

// SoC user function type definitions
typedef user_state_info (*soc_init_handler_t)();       // init handler allocates and returns user_state with size
typedef nicc_retval_t (*soc_pkt_handler_t)(Buffer* pkt, void* user_state);  
typedef nicc_retval_t (*soc_msg_handler_t)(Buffer* msg, void* user_state);
typedef void (*soc_cleanup_handler_t)(void* user_state);   // cleanup handler frees user_state

/**
 * \brief Wrapper for executing SoC functions, created and initialized by ComponentBlock_SoC
 */
class SoCWrapper {
/**
 * ----------------------Parameters used in SoCWrapper----------------------
 */ 
 public:
    static constexpr uint16_t kAppTxMsgBatchSize = 32;
    static constexpr uint16_t kAppRxMsgBatchSize = 32;

    /// Minimal number of buffered packets for collect_tx_pkts
    static constexpr size_t kTxBatchSize = 32;
    /// Maximum number of transmitted packets for tx_burst
    static constexpr size_t kTxPostSize = 32;
    /// Minimal number of buffered packets before dispatching
    static constexpr size_t kRxBatchSize = 128;
    /// Maximum number of packets received in rx_burst
    static constexpr size_t kRxPostSize = 32;
/**
 * ----------------------Public Structures----------------------
 */ 
 public:
    enum soc_wrapper_type_t {
        kSoC_Invalid = 0x00,
        kSoC_Dispatcher = 0x01,     /// The thread will communicate with other component blocks
        kSoC_Worker = 0x02          /// The thread will execute the app function
    };
    /**
     * \brief   local SoCWrapper context for executing SoC functions, including
     *                - function state ptr
     *                - event handler ptr
     *                - queue pair ptrs for component block communication
     */
    struct SoCWrapperContext{
        /* ========== metadata for dispatcher ========== */
        RDMA_SoC_QP *qp_for_prior;    /// QP for communicating with the prior component block
        RDMA_SoC_QP *qp_for_next;     /// QP for communicating with the next component block
        /// e.g. pkt handler ptr
        /// e.g. match-action table ptr
        /* ========== metadata for worker ========== */
        /// e.g. function state ptr
        /// e.g. event handler ptr
        /// lock-free queue
        
        /* ========== user defined handlers and state ========== */
        soc_init_handler_t init_handler;    /// user defined init handler
        soc_pkt_handler_t pkt_handler;      /// user defined packet handler  
        soc_msg_handler_t msg_handler;      /// user defined message handler
        soc_cleanup_handler_t cleanup_handler; /// user defined cleanup handler
        void* user_state;                   /// user defined state object (allocated by user in init_handler)
        size_t user_state_size;             /// size of user_state (for future reschedule support)
        
        /* ========== routing for packet forwarding ========== */
        ComponentRouting_SoC* routing;      /// routing component for packet forwarding decisions
    };

/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    /**
     * \brief Constructor, ComponentBlock_SoC calls this when allocating a thread for SoCWrapper.
     *        For dispatcher, 
     * \param type  type of the SoCWrapper
     * \param context context of the SoCWrapper, registered by the ComponentBlock_SoC
     */
    SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context);
    ~SoCWrapper();  

/**
 * ----------------------Internel Methonds----------------------
 */ 
 private:
    /**
     * \brief Initialize the dispatcher, including comm_lib
     */
    nicc_retval_t __init_dispatcher();

    /**
     * \brief Initialize the worker, registering 
     */
    nicc_retval_t __init_worker();

    /**
     * \brief Run the SoCWrapper datapath in the sequence of worker tx, dispatcher tx, worker rx, and dispatcher rx.
     * \param double seconds, the time to run the datapath
     */
    void __run(double seconds);

    /**
     * \brief Launch the SoC kernel loop
     */
    void __launch();

    /* ========================SoC Datapath ========================*/

    /**
     * \brief Post receive wrs to the NIC, and update the recv head
     * \param RDMA_SoC_QP *qp, the QP for receiving packets
     * \param size_t num_recvs, the number of receive wrs to be posted
     */
    static void __post_recvs(RDMA_SoC_QP *qp, size_t num_recvs) {
        // The recvs posted are @first_wr through @last_wr, inclusive
        struct ibv_recv_wr *first_wr, *last_wr, *temp_wr, *bad_wr;

        int ret;
        size_t first_wr_i = qp->_recv_head;
        size_t last_wr_i = first_wr_i + (num_recvs - 1);
        if (last_wr_i >= RDMA_SoC_QP::kNumRxRingEntries) last_wr_i -= RDMA_SoC_QP::kNumRxRingEntries;

        first_wr = &qp->_recv_wr[first_wr_i];
        last_wr = &qp->_recv_wr[last_wr_i];
        temp_wr = last_wr->next;

        last_wr->next = nullptr;  // Breaker of chains, queen of the First Men

        ret = ibv_post_recv(qp->_qp, first_wr, &bad_wr);
        if (unlikely(ret != 0)) {
            NICC_ERROR("SoCWrapper: Post RECV (normal) error %d\n", ret);
        }

        last_wr->next = temp_wr;  // Restore circularity

        // Update RECV head: go to the last wr posted and take 1 more step
        qp->_recv_head = last_wr_i;
        qp->_recv_head = (qp->_recv_head + 1) % RDMA_SoC_QP::kNumRxRingEntries;
    }

    /**
     * \brief Receive packets from the NIC and put them into the dispatcher rx queue.
     * \param RDMA_SoC_QP *qp, the QP for receiving packets
     * \return the number of packets received
     */
    size_t __rx_burst(RDMA_SoC_QP *qp);

    /**
     * \brief Dispatch packets from the dispatcher rx queue to the worker rx queue 
     * based on packet UDP field. Workspace will be blocked until all packets are
     * dispatched.
     * \param RDMA_SoC_QP *qp, the QP for receiving packets
     * \return the number of packets dispatched
     */
    size_t __dispatch_rx_pkts(RDMA_SoC_QP *qp);

    /**
     * \brief Iterate all worker queues assigned to this dispatcher, and collect packets from them.
     * \param RDMA_SoC_QP *qp, the QP for sending packets
     * \return the number of packets collected
     */
    size_t __collect_tx_pkts(RDMA_SoC_QP *qp);

    /**
     * \brief Post send wrs to the NIC, and update the send head
     * \param RDMA_SoC_QP *qp, the QP for sending packets
     * \param Buffer **tx, the array of buffers to be sent
     * \param size_t tx_size, the number of buffers to be sent
     * \return the number of packets sent
     */
    size_t __tx_burst(RDMA_SoC_QP *qp, Buffer **tx, size_t tx_size);

    /**
     * \brief Flush the dispatcher tx queue to the NIC. Dispatcher will be blocked
     * until all packets are sent
     * \param RDMA_SoC_QP *qp, the QP for sending packets
     * \return the number of packets sent
     */
    size_t __tx_flush(RDMA_SoC_QP *qp);

    /**
     * \brief Directly send packets from the one qp's rx queue to the another qp's tx queue.
     * \param RDMA_SoC_QP *rx_qp, the QP for receiving packets
     * \param RDMA_SoC_QP *tx_qp, the QP for sending packets
     * \return the number of packets sent
     */
    size_t __direct_tx_burst(RDMA_SoC_QP *rx_qp, RDMA_SoC_QP *tx_qp);

    /**
     * \brief Forward packet using routing decision based on kernel return value
     * \param packet            packet buffer to forward
     * \param kernel_retval     return value from user kernel
     * \return NICC_SUCCESS for successful forwarding
     */
    nicc_retval_t __forward_packet_with_routing(Buffer* packet, nicc_core_retval_t kernel_retval);

/**
 * ----------------------Internel parameters----------------------
 */
 private:
    soc_wrapper_type_t _type = kSoC_Invalid;
    SoCWrapperContext *_context = nullptr;
    /// QPs
    RDMA_SoC_QP *_qp_for_prior = nullptr;
    RDMA_SoC_QP *_qp_for_next = nullptr;

    /// tmp shm queue for testing
    soc_shm_lock_free_queue* _tmp_worker_rx_queue = nullptr;
    soc_shm_lock_free_queue* _tmp_worker_tx_queue = nullptr;
};


} // namespace nicc
