#pragma once

#include <infiniband/verbs.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unordered_map>

#include "common.h"
#include "log.h"
#include "datapath/channel.h"
#include "libutils/soc_queue.h"
#include "utils/verbs_common.h"
#include "libutils/math_utils.h"
#include "utils/huge_alloc.h"
#include "utils/qpinfo.hh"
#include "utils/mgnt_connection.h"

namespace nicc {

class Channel_SoC : public Channel {
/**
 * ----------------------Parameters of SoC Channel----------------------
 */ 
 public:
    static constexpr size_t kNumRxRingEntries = 2048;
    static_assert(is_power_of_two<size_t>(kNumRxRingEntries), "The num of RX ring entries is not power of two.");
    static constexpr size_t kNumTxRingEntries = 2048;
    static_assert(is_power_of_two<size_t>(kNumTxRingEntries), "The num of TX ring entries is not power of two.");
    static constexpr size_t kMTU = 1024;
    static_assert(is_power_of_two<size_t>(kMTU), "The size of MTU is not power of two.");
    // static constexpr size_t kMaxPayloadSize = kMTU - sizeof(iphdr) - sizeof(udphdr);
    /// Minimal number of buffered packets for collect_tx_pkts
    static constexpr size_t kTxBatchSize = 32;
    /// Maximum number of transmitted packets for tx_burst
    static constexpr size_t kTxPostSize = 32;
    /// Minimal number of buffered packets before dispatching
    static constexpr size_t kRxBatchSize = 32;
    /// Maximum number of packets received in rx_burst
    static constexpr size_t kRxPostSize = 128;

    static constexpr size_t kMaxRoutingInfoSize = 48;  ///< Space for routing info
    static constexpr size_t kMaxInline = 60;   ///< Maximum send wr inline data
    /// Ideally, the connection handshake should establish a secure queue key.
    /// For now, anything outside 0xffff0000..0xffffffff (reserved by CX3) works.
    static constexpr uint32_t kQKey = 0x0205; 

    static constexpr size_t kRQDepth = kNumRxRingEntries;   ///< RECV queue depth
    static constexpr size_t kSQDepth = kNumTxRingEntries;   ///< Send queue depth

    static constexpr size_t kPostlist = 32;    ///< Maximum SEND postlist

    static constexpr size_t kGRHBytes = 40;
    static constexpr size_t kSgeSize = kMTU;  /// seg size cannot exceed MTU

    static constexpr size_t kRecvMbufSize = kSgeSize + 64;    ///< RECV size (with GRH in first 64B)
    static constexpr size_t kSendMbufSize = kSgeSize;    ///< SEND size

    static constexpr size_t kMaxUDSize = kSendMbufSize * kSQDepth;  ///< Maximum UD message size
    static constexpr size_t kMemRegionSize = kRecvMbufSize * kRQDepth + kSendMbufSize * kSQDepth;  ///< Memory region size, for both TX and RX

    static constexpr size_t kInvalidQpId = SIZE_MAX;

    /// Address of the remote endpoint
    static constexpr uint16_t kDefaultUdpPort = 10010;
    static constexpr uint16_t kDefaultMngtPort = 20086;
    const char* kRemoteIpStr = "10.10.10.10";  // \todo: set it via config file
    
/**
 * ----------------------Public methods----------------------
 */ 
 public:
    Channel_SoC(channel_typeid_t channel_type, channel_mode_t channel_mode)
        : Channel() {
        this->_typeid = channel_type;
        this->_mode = channel_mode;
    }
    ~Channel_SoC() {}

    /**
     * \brief Allocate channel resources including QP, CQ and application queues
     * \param pd Protection domain
     * \param ibv_ctx IBV context
     * \param queue_depth Depth of application queues
     * \param max_sge Maximum number of scatter/gather elements per WR
     * \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t allocate_channel(const char *dev_name, uint8_t phy_port);

    /**
     * \brief Deallocate channel resources
     * \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t deallocate_channel();

/**
 * ----------------------Internel methods----------------------
 */ 
 private:
    /**
     * @brief Resolve InfiniBand-specific fields in \p resolve
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __roce_resolve_phy_port();

    /**
     * @brief Initialize structures: device context, protection domain, and queue pair.
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_verbs_structs();

    /**
     * @brief Set local QP info
     * @param qp_info QP info
     */
    void __set_local_qp_info(QPInfo *qp_info);

    /**
     * @brief Create address handles for local and remote endpoints
     * @param local_qp_info Local QP info
     * @param remote_qp_info Remote QP info
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __create_ah(QPInfo *local_qp_info, QPInfo *remote_qp_info);
    
    /**
     * @brief Initialize and allocate memory for TX/RX rings
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_ring();

/**
 * ----------------------Internel parameters----------------------
 */
 private:
    /// The hugepage allocator for this channel
    HugeAlloc *_huge_alloc = nullptr;
    /// Info resolved from \p phy_port, must be filled by constructor.
    class IBResolve : public VerbsResolve {
    public:
      ipaddr_t ipv4_addr_;   // The port's IPv4 address in host-byte order
      uint16_t port_lid = 0;  ///< Port LID. 0 is invalid.
      union ibv_gid gid;      ///< GID, used only for RoCE
    } _resolve;

    /// Protection domain
    struct ibv_pd *_pd = nullptr;

    /// Parameters for qp init
    struct ibv_cq *_send_cq = nullptr;
    struct ibv_cq *_recv_cq = nullptr;
    struct ibv_qp *_qp = nullptr;
    size_t _qp_id = kInvalidQpId;

    /// An address handle for this endpoint's port. 
    struct ibv_ah *_local_ah = nullptr;
    size_t _remote_qp_id = kInvalidQpId;  ///< The remote QP ID
    struct ibv_ah *_remote_ah = nullptr;  ///< An address handle for the remote endpoint's port.
    /// Address handles that we must free in the destructor
    std::vector<ibv_ah *> _ah_to_free_vec;
    ipaddr_t *_daddr = nullptr;  ///< Destination IP address

    /// Parameters for tx/rx ring

};

}  // namespace nicc