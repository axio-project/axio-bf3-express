#pragma once
#include "common.h"
#include "log.h"
#include "common/soc_queue.h"
#include "common/ws_hdr.h"
#include "common/timer.h"

#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

namespace nicc {

/**
 * ======================Quick test for the application======================
 */
enum msg_handler_type_t : uint8_t {
  kRxMsgHandler_Empty = 0,
  kRxMsgHandler_T_APP,
  kRxMsgHandler_L_APP,
  kRxMsgHandler_M_APP,
  kRxMsgHandler_FS_WRITE,
  kRxMsgHandler_FS_READ,
  kRxMsgHandler_KV
};
enum pkt_handler_type_t : uint8_t {
  kRxPktHandler_Empty = 0,
  kRxPktHandler_Echo
};
/* -----Message-level specification----- */
#define kRxMsgHandler kRxMsgHandler_T_APP
#define ApplyNewMbuf false
static constexpr size_t kAppTicksPerMsg = 0;    // extra execution ticks for each message, used for more accurate emulation
/// Payload size for CLIENT behavior
// Corresponding MAC frame len: 22 -> 64; 86 -> 128; 214 -> 256; 470 -> 512; 982 -> 1024; 1458 -> 1500
constexpr size_t kAppReqPayloadSize = 
    (kRxMsgHandler == kRxMsgHandler_Empty) ? 0 :
    (kRxMsgHandler == kRxMsgHandler_T_APP) ? 982 :
    (kRxMsgHandler == kRxMsgHandler_L_APP) ? 86 :
    (kRxMsgHandler == kRxMsgHandler_M_APP) ? 86 : 
    (kRxMsgHandler == kRxMsgHandler_FS_WRITE) ? KB(16) : 
    (kRxMsgHandler == kRxMsgHandler_FS_READ) ? 22 : 
    (kRxMsgHandler == kRxMsgHandler_KV) ?  81 : //type + key size + value size
    0;
static_assert(kAppReqPayloadSize > 0, "Invalid application payload size");
/// Payload size for SERVER behavior
constexpr size_t kAppRespPayloadSize = 
    (kRxMsgHandler == kRxMsgHandler_Empty) ? 0 :
    (kRxMsgHandler == kRxMsgHandler_T_APP) ? 22 :
    (kRxMsgHandler == kRxMsgHandler_L_APP) ? 86 :
    (kRxMsgHandler == kRxMsgHandler_M_APP) ? 86 : 
    (kRxMsgHandler == kRxMsgHandler_FS_WRITE) ? 22 : 
    (kRxMsgHandler == kRxMsgHandler_FS_READ) ? KB(100) : 
    (kRxMsgHandler == kRxMsgHandler_KV) ? 81 : // type + key size + value size
    0;
static_assert(kAppRespPayloadSize > 0, "Invalid application response payload size");
// M_APP specific
static constexpr size_t kMemoryAccessRangePerPkt    = KB(1);
static constexpr size_t kStatefulMemorySizePerCore  = KB(256);

/* -----Packet-level specification----- */
#define kRxPktHandler  kRxPktHandler_Empty

// client specific
#define EnableInflyMessageLimit true    // whether to enable infly message limit, if false, the client will send messages as fast as possible
static constexpr uint64_t kInflyMessageBudget = 1024;


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

    /// TX specific
    static constexpr size_t kAppRequestPktsNum = ceil((double)kAppReqPayloadSize / (double)RDMA_SoC_QP::kMaxPayloadSize);  // number of packets in a request message
    static constexpr size_t kAppFullPaddingSize = RDMA_SoC_QP::kMaxPayloadSize - sizeof(ws_hdr);
    static constexpr size_t kAppLastPaddingSize = kAppReqPayloadSize - (kAppRequestPktsNum - 1) * RDMA_SoC_QP::kMaxPayloadSize - sizeof(ws_hdr);
    // RX specific
    static constexpr size_t kAppReponsePktsNum = ceil((double)kAppRespPayloadSize / (double)RDMA_SoC_QP::kMaxPayloadSize); // number of packets in a response message
    static constexpr size_t kAppRespFullPaddingSize = RDMA_SoC_QP::kMaxPayloadSize - sizeof(ws_hdr);

    /// Minimal number of buffered packets for collect_tx_pkts
    static constexpr size_t kTxBatchSize = 32;
    /// Maximum number of transmitted packets for tx_burst
    static constexpr size_t kTxPostSize = 32;
    /// Minimal number of buffered packets before dispatching
    static constexpr size_t kRxBatchSize = 32;
    /// Maximum number of packets received in rx_burst
    static constexpr size_t kRxPostSize = 128;
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
        RDMA_SoC_QP *prior_qp;    /// QP for communicating with the prior component block
        RDMA_SoC_QP *next_qp;     /// QP for communicating with the next component block
        /// e.g. pkt handler ptr
        /// e.g. match-action table ptr
        /* ========== metadata for worker ========== */
        /// e.g. function state ptr
        /// e.g. event handler ptr
        /// lock-free queue
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
    ~SoCWrapper(){}

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
     * \brief Flush the dispatcher tx queue to the NIC. Dispatcher will be blocked
     * until all packets are sent
     * \param RDMA_SoC_QP *qp, the QP for sending packets
     * \return the number of packets sent
     */
    size_t __tx_flush(RDMA_SoC_QP *qp);

    /**
     * \brief set the payload of a buffer
     * \param Buffer *m, [in] the buffer to be set
     * \param size_t payload_size, [in] the size of the payload
     */
    static void __set_echo(Buffer *m, size_t payload_size) {
        m->length_ = /* eth + ip + udp */42 + sizeof(ws_hdr) + payload_size;
        /// switch src udp port and dst udp port
        udphdr *uh = (udphdr *)m->get_uh();
        uint16_t src_port = ntohs(uh->source);
        uint16_t dst_port = ntohs(uh->dest);
        uh->source = htons(dst_port);
        uh->dest = htons(src_port);
        /// set the payload
        if (unlikely(payload_size == 0)) {
            return;
        }
        char *payload_ptr = (char *)m->get_ws_payload();
        memset(payload_ptr, 'a', payload_size - 1);
        payload_ptr[payload_size - 1] = '\0'; 
        return;
    }


/**
 * ----------------------Internel parameters----------------------
 */
 private:
    soc_wrapper_type_t _type = kSoC_Invalid;
    SoCWrapperContext *_context = nullptr;
    /// QPs
    RDMA_SoC_QP *_prior_qp = nullptr;
    RDMA_SoC_QP *_next_qp = nullptr;
};


} // namespace nicc
