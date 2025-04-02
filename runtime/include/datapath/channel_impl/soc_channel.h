#pragma once

#include <infiniband/verbs.h>
#include <unordered_map>

#include "common.h"
#include "log.h"
#include "datapath/channel.h"
#include "common/soc_queue.h"
#include "common/math_utils.h"
#include "utils/huge_alloc.h"
#include "common/buffer.h"

namespace nicc {

class Channel_SoC : public Channel {
/**
 * ----------------------Parameters of SoC Channel----------------------
 */ 
 public:

    static constexpr size_t kMaxRoutingInfoSize = 48;  ///< Space for routing info
    static constexpr size_t kMaxInline = 60;   ///< Maximum send wr inline data
    /// Ideally, the connection handshake should establish a secure queue key.
    /// For now, anything outside 0xffff0000..0xffffffff (reserved by CX3) works.
    static constexpr uint32_t kQKey = 0x0205; 

    static constexpr size_t kRQDepth = RDMA_SoC_QP::kNumRxRingEntries;   ///< RECV queue depth
    static constexpr size_t kSQDepth = RDMA_SoC_QP::kNumTxRingEntries;   ///< Send queue depth

    static constexpr size_t kPostlist = 32;    ///< Maximum SEND postlist

    // static constexpr size_t kSgeSize = kMTU;  /// seg size cannot exceed MTU
    static constexpr size_t kSgeSize = round_up<4096>(RDMA_SoC_QP::kMTU);    /// manully update "2048" to higher if kMTU is > 2048
    static_assert(is_power_of_two(kSgeSize), "kSgeSize must be a power of 2");
    static_assert(kSgeSize >= RDMA_SoC_QP::kMTU, "kSgeSize must be >= kMTU");

    static constexpr size_t kRecvMbufSize = kSgeSize;    ///< RECV size 
    static constexpr size_t kSendMbufSize = kSgeSize;    ///< SEND size

    static constexpr size_t kMaxUDSize = kSendMbufSize * kSQDepth;  ///< Maximum UD message size
    static constexpr size_t kMemRegionSize = 2 * (kRecvMbufSize * kRQDepth + kSendMbufSize * kSQDepth);  ///< Memory region size, for both TX and RX

    static constexpr size_t kInvalidQpId = SIZE_MAX;

    
/**
 * ----------------------Public methods----------------------
 */ 
 public:
    Channel_SoC(channel_typeid_t channel_type_of_prior, channel_mode_t channel_mode_of_prior, channel_typeid_t channel_type_of_next, channel_mode_t channel_mode_of_next)
        : Channel() {
        this->_typeid_of_prior = channel_type_of_prior;
        this->_mode_of_prior = channel_mode_of_prior;
        this->_typeid_of_next = channel_type_of_next;
        this->_mode_of_next = channel_mode_of_next;
        this->qp_for_prior_info = (QPInfo*)malloc(sizeof(QPInfo));
        this->qp_for_next_info = (QPInfo*)malloc(sizeof(QPInfo));
    }
    ~Channel_SoC() {
        NICC_DEBUG_C("destory channel for prior QP %lu, next QP %lu", this->qp_for_prior->_qp_id, this->qp_for_next->_qp_id);
        // deregister memory region
        int ret = ibv_dereg_mr(this->_mr);
        if (ret != 0) {
            NICC_WARN_C("Memory degistration failed. size %zu B, lkey %u\n", this->_mr->length / MB(1), this->_mr->lkey);
        }
        NICC_DEBUG_C("Deregistered %zu MB (lkey = %u)\n", this->_mr->length / MB(1), this->_mr->lkey);
        // delete Buffer in _rx_ring
        for (size_t i = 0; i < kRQDepth; i++) {
            delete this->qp_for_prior->_rx_ring[i];
            delete this->qp_for_next->_rx_ring[i];
        }
        // delete SHM
        delete this->_huge_alloc;

        // Destroy QPs and CQs. QPs must be destroyed before CQs.
        exit_assert(ibv_destroy_qp(this->qp_for_prior->_qp) == 0, "Failed to destroy send QP");
        exit_assert(ibv_destroy_cq(this->qp_for_prior->_send_cq) == 0, "Failed to destroy send CQ");
        exit_assert(ibv_destroy_cq(this->qp_for_prior->_recv_cq) == 0, "Failed to destroy recv CQ");
        exit_assert(ibv_destroy_qp(this->qp_for_next->_qp) == 0, "Failed to destroy send QP");
        exit_assert(ibv_destroy_cq(this->qp_for_next->_send_cq) == 0, "Failed to destroy send CQ");
        exit_assert(ibv_destroy_cq(this->qp_for_next->_recv_cq) == 0, "Failed to destroy recv CQ");
        exit_assert(ibv_destroy_ah(this->_local_ah) == 0, "Failed to destroy local AH");
        exit_assert(ibv_destroy_ah(this->qp_for_prior->_remote_ah) == 0, "Failed to destroy remote AH");
        exit_assert(ibv_destroy_ah(this->qp_for_next->_remote_ah) == 0, "Failed to destroy remote AH");
        exit_assert(ibv_dealloc_pd(this->_pd) == 0, "Failed to destroy PD. Leaked MRs?");
        exit_assert(ibv_close_device(this->_resolve.ib_ctx) == 0, "Failed to close device");
    }

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
     * @brief Connect QP to a component block or remote/local host
     * @param is_prior [in] whether the connection is for qp_for_prior or qp_for_next
     * @param neighbour_component_block [in] the neighbour component block
     * @param qp_info [in] QP info of the remote/local host
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t connect_qp(bool is_prior, 
                             const ComponentBlock *neighbour_component_block, 
                             const QPInfo *qp_info);

/**
 * ----------------------Public parameters----------------------
 */
 public:
    /// Parameters for qp init
    class RDMA_SoC_QP *qp_for_prior;        /// QP for prior component block
    class RDMA_SoC_QP *qp_for_next;         /// QP for next component block
    QPInfo *qp_for_prior_info;
    QPInfo *qp_for_next_info;
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
     * @brief Create RDMA RC QP
     * @param qp RDMA_SoC_QP
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __create_qp(RDMA_SoC_QP *qp);

    /**
     * @brief Set local QP info
     * @param qp_info [out] QP info recording the gid, lid, qp_num, mtu, nic_name
     * @param qp [in] RDMA_SoC_QP
     */
    void __set_local_qp_info(QPInfo *qp_info, RDMA_SoC_QP *qp);

    /**
     * @brief Create address handles for local and remote endpoints
     * @param local_qp_info [in] Local QP info
     * @param remote_qp_info [in] Remote QP info
     * @param qp [in] RDMA_SoC_QP
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __create_ah(const QPInfo *local_qp_info, const QPInfo *remote_qp_info, RDMA_SoC_QP *qp);
    
    /**
     * @brief Initialize and allocate memory for TX/RX rings
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_rings();

    /**
     * @brief Initialize the RECV queue
     * @param qp [in] RDMA_SoC_QP for prior or next component block
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_recvs(RDMA_SoC_QP *qp);

    /**
     * @brief Initialize the SEND queue
     * @param qp [in] RDMA_SoC_QP for prior or next component block
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_sends(RDMA_SoC_QP *qp);

    /**
     * @brief connect a qp to a component block
     * @param qp [in] RDMA_SoC_QP
     * @param neighbour_component_block [in] the neighbour component block
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __connect_qp_to_component_block(RDMA_SoC_QP *qp, const ComponentBlock *neighbour_component_block, const QPInfo *local_qp_info);

    /**
     * @brief connect a qp to a remote/local host
     * @param qp [in] RDMA_SoC_QP
     * @param remote_qp_info [in] QP info of the target component block
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __connect_qp_to_host(RDMA_SoC_QP *qp, const QPInfo *remote_qp_info, const QPInfo *local_qp_info);
    /**
     * @brief Fill the RECV queue
     * @param qp [in] RDMA_SoC_QP for prior or next component block
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __fill_recv_queue(RDMA_SoC_QP *qp);

/**
 * ----------------------Internel parameters----------------------
 */
 private:
    /// The hugepage allocator for this channel
    HugeAlloc *_huge_alloc = nullptr;
    /// Info resolved from \p phy_port, must be filled by constructor.
    class IBResolve : public VerbsResolve {
    public:
      ipaddr_t ipv4_addr;          ///< The port's IPv4 address in host-byte order
      uint16_t port_lid = 0;        ///< Port LID. 0 is invalid.
      union ibv_gid gid;            ///< GID, used only for RoCE
      uint8_t gid_index = 0;        ///< GID index, used only for RoCE
      uint8_t mac_addr[6] = {0};    ///< MAC address of the device port
    } _resolve;

    /// Protection domain
    struct ibv_pd *_pd = nullptr;

    /// An address handle for this endpoint's port. 
    struct ibv_ah *_local_ah = nullptr;

    /// Parameters for tx/rx ring
    struct ibv_mr *_mr = nullptr;
};

}  // namespace nicc