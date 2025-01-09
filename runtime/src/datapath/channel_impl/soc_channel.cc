#include "datapath/channel_impl/soc_channel.h"

namespace nicc {

// GIDs are currently used only for RoCE. This default value works for most
// clusters, but we need a more robust GID selection method. Some observations:
//  * On physical clusters, gid_index = 0 always works (in my experience)
//  * On VM clusters (AWS/KVM), gid_index = 0 does not work, gid_index = 1 works
//  * Mellanox's `show_gids` script lists all GIDs on all NICs
static constexpr size_t kDefaultGIDIndex = 0;   // Currently, the GRH (ipv4 + udp port) is set by CPU

nicc_retval_t Channel_SoC::allocate_channel(const char *dev_name, uint8_t phy_port) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    this->_huge_alloc = new HugeAlloc(kMemRegionSize, /* numa_node */0);    // SoC only has one NUMA node
    common_resolve_phy_port(dev_name, phy_port, kMTU, _resolve);

    if(unlikely(NICC_SUCCESS != (retval = __roce_resolve_phy_port()))){
        NICC_WARN_C("failed to resolve phy port: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }

    if(unlikely(NICC_SUCCESS != (retval = __init_verbs_structs()))){
        NICC_WARN_C("failed to init verbs structs: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }



exit:
    // TODO: destory if failed
    return retval;
}

nicc_retval_t Channel_SoC::__roce_resolve_phy_port() {
    nicc_retval_t retval = NICC_SUCCESS;
    struct ibv_port_attr port_attr;

    NICC_CHECK_POINTER(this->_resolve.ib_ctx);

    // query port
    if (ibv_query_port(this->_resolve.ib_ctx, this->_resolve.dev_port_id, &port_attr)) {
        NICC_WARN_C("failed to query port: dev_port_id(%u), retval(%u)", this->_resolve.dev_port_id, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    this->_resolve.port_lid = port_attr.lid;

    if (ibv_query_gid(_resolve.ib_ctx, _resolve.dev_port_id, kDefaultGIDIndex, &_resolve.gid)) {
        NICC_WARN_C("failed to query gid: dev_port_id(%u), gid_index(%u), retval(%u)", _resolve.dev_port_id, kDefaultGIDIndex, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // validate gid
    uint64_t *gid_data = (uint64_t *)&this->_resolve.gid;
    if (gid_data[0] == 0 && gid_data[1] == 0) {
        NICC_WARN_C("invalid gid (all zeros), dev_port_id(%u), gid_index(%u)", this->_resolve.dev_port_id, kDefaultGIDIndex);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // set ipv4 address
    memset(&this->_resolve.ipv4_addr_, 0, sizeof(ipaddr_t));
    this->_resolve.ipv4_addr_.ip = this->_resolve.gid.global.interface_id & 0xffffffff;

    return retval;
}

nicc_retval_t Channel_SoC::__init_verbs_structs() {
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(this->_resolve.ib_ctx);

    // Create protection domain, send CQ, and recv CQ
    this->_pd = ibv_alloc_pd(this->_resolve.ib_ctx);
    NICC_CHECK_POINTER(this->_pd);

    // Create send CQ
    this->_send_cq = ibv_create_cq(this->_resolve.ib_ctx, kSQDepth, nullptr, nullptr, 0);
    NICC_CHECK_POINTER(this->_send_cq);

    // Create recv CQ
    this->_recv_cq = ibv_create_cq(this->_resolve.ib_ctx, kRQDepth, nullptr, nullptr, 0);
    NICC_CHECK_POINTER(this->_recv_cq);

    // Initialize QP creation attributes
    struct ibv_qp_init_attr create_attr;
    memset(static_cast<void *>(&create_attr), 0, sizeof(struct ibv_qp_init_attr));
    create_attr.send_cq = this->_send_cq;
    create_attr.recv_cq = this->_recv_cq;
    create_attr.qp_type = IBV_QPT_RC;

    create_attr.cap.max_send_wr = kSQDepth;
    create_attr.cap.max_recv_wr = kRQDepth;
    create_attr.cap.max_send_sge = 1;
    create_attr.cap.max_recv_sge = 1;
    create_attr.cap.max_inline_data = kMaxInline;

    this->_qp = ibv_create_qp(this->_pd, &create_attr);
    NICC_CHECK_POINTER(this->_qp);
    this->_qp_id = this->_qp->qp_num;

    /// exchange qp info via TCP mgmt connection
    // \todo: send queue for host, recv queue for prior component block
    struct QPInfo qp_info;
    struct QPInfo remote_qp_info;
    this->__set_local_qp_info(&qp_info);
    TCPClient mgnt_client;
    mgnt_client.connectToServer(kRemoteIpStr, kDefaultMngtPort);
    mgnt_client.sendMsg(qp_info.serialize());
    remote_qp_info.deserialize(mgnt_client.receiveMsg());

    /// Transition QP to INIT state
    struct ibv_qp_attr init_attr;
    memset(static_cast<void *>(&init_attr), 0, sizeof(struct ibv_qp_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.pkey_index = 0;
    init_attr.port_num = static_cast<uint8_t>(this->_resolve.dev_port_id);
    init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
    int attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(this->_qp, &init_attr, attr_mask) != 0) {
        NICC_WARN_C("failed to modify QP to INIT: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    /// RTR state
    struct ibv_qp_attr rtr_attr;
    memset(static_cast<void *>(&rtr_attr), 0, sizeof(struct ibv_qp_attr));
    rtr_attr.qp_state = IBV_QPS_RTR;
    switch(kMTU){
        case 1024:
            rtr_attr.path_mtu = IBV_MTU_1024;
            break;
        case 2048:
            rtr_attr.path_mtu = IBV_MTU_2048;
            break;
        case 4096:
            rtr_attr.path_mtu = IBV_MTU_4096;
            break;
        default:
            NICC_WARN_C("unsupported MTU: %lu, only 1024, 2048, 4096 are supported", kMTU);
            return NICC_ERROR_HARDWARE_FAILURE;
    }
    rtr_attr.dest_qp_num = remote_qp_info.qp_num;
    rtr_attr.rq_psn = 0;
    rtr_attr.max_dest_rd_atomic = 1;
    rtr_attr.min_rnr_timer = 12;

    rtr_attr.ah_attr.sl = 0;
    rtr_attr.ah_attr.src_path_bits = 0;
    rtr_attr.ah_attr.port_num = 1;
    rtr_attr.ah_attr.dlid = remote_qp_info.lid;
    memcpy(&rtr_attr.ah_attr.grh.dgid, remote_qp_info.gid, 16);
    rtr_attr.ah_attr.is_global = 1;
    rtr_attr.ah_attr.grh.sgid_index = kDefaultGIDIndex;
    rtr_attr.ah_attr.grh.hop_limit = 2;
    rtr_attr.ah_attr.grh.traffic_class = 0;
    if (ibv_modify_qp(this->_qp, &rtr_attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                      IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                      IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        NICC_WARN_C("failed to modify QP to RTR: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    /// create address handles
    if(unlikely(NICC_SUCCESS != (retval = this->__create_ah(&qp_info, &remote_qp_info)))){
        NICC_WARN_C("failed to create address handles: retval(%u)", retval);
        return retval;
    }

    /// RTS state
    rtr_attr.qp_state = IBV_QPS_RTS;
    rtr_attr.sq_psn = 0;
    rtr_attr.timeout = 14;
    rtr_attr.retry_cnt = 7;
    rtr_attr.rnr_retry = 7;
    rtr_attr.max_rd_atomic = 1;
    if (ibv_modify_qp(this->_qp, &rtr_attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                      IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
        NICC_WARN_C("failed to modify QP to RTS: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }


    return retval;
}

void Channel_SoC::__set_local_qp_info(QPInfo *qp_info) {
    NICC_CHECK_POINTER(qp_info);
    qp_info->qp_num = this->_qp_id;
    qp_info->lid = this->_resolve.port_lid;
    for (size_t i = 0; i < 16; i++) {
        qp_info->gid[i] = this->_resolve.gid.raw[i];
    }
    qp_info->mtu = kMTU;
    memcpy(qp_info->nic_name, this->_resolve.ib_ctx->device->name, MAX_NIC_NAME_LEN);
}

nicc_retval_t Channel_SoC::__create_ah(QPInfo *local_qp_info, QPInfo *remote_qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(local_qp_info);
    NICC_CHECK_POINTER(remote_qp_info);
    struct ibv_ah_attr ah_attr;
    /// Create local AH
    union ibv_gid gid; 
    memset(&gid, 0, sizeof(union ibv_gid));
    memcpy(&gid, local_qp_info->gid, 16);
    memset(&ah_attr, 0, sizeof(struct ibv_ah_attr));
            ah_attr.is_global = 1;
            ah_attr.dlid = 0;
            ah_attr.sl = 0;
            ah_attr.src_path_bits = 0;
            ah_attr.port_num = local_qp_info->lid; // Local port
            ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
            ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
            ah_attr.grh.sgid_index = kDefaultGIDIndex;
            ah_attr.grh.hop_limit = 2;
    this->_local_ah = ibv_create_ah(this->_pd, &ah_attr);
    if (unlikely(this->_local_ah == nullptr)) {
        NICC_WARN_C("failed to create local AH");
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    /// Create remote AH
    this->_remote_qp_id = remote_qp_info->qp_num;
    memset(&ah_attr, 0, sizeof(struct ibv_ah_attr));
            ah_attr.sl = 0;
            ah_attr.src_path_bits = 0;
            ah_attr.port_num = 1;
            ah_attr.dlid = remote_qp_info->lid;
            memcpy(&ah_attr.grh.dgid, remote_qp_info->gid, 16);
            ah_attr.is_global = 1;
            ah_attr.grh.sgid_index = kDefaultGIDIndex;
            ah_attr.grh.hop_limit = 2;
            ah_attr.grh.traffic_class = 0;
    this->_remote_ah = ibv_create_ah(this->_pd, &ah_attr);
    if (unlikely(this->_remote_ah == nullptr)) {
        NICC_WARN_C("failed to create remote AH");
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    return retval;
}

nicc_retval_t Channel_SoC::__init_ring() {
    nicc_retval_t retval = NICC_SUCCESS;
    // Initialize the ring buffer
    /// Step 1: Allocate memory for the ring buffer
    Buffer raw_mr = this->_huge_alloc->alloc_raw(kMemRegionSize, DoRegister::kTrue);
    if (raw_mr.buf_ == nullptr) {
        NICC_WARN_C("failed to allocate memory for the ring buffer");
        return NICC_ERROR_MEMORY_FAILURE;
    }
    NICC_CHECK_POINTER(this->_mr = ibv_reg_mr(this->_pd, raw_mr.buf_, kMemRegionSize, IBV_ACCESS_LOCAL_WRITE));
    raw_mr.set_lkey(this->_mr->lkey);
    this->_huge_alloc->add_raw_buffer(raw_mr, kMemRegionSize);

    /// Step 2: Initialize the ring buffer
    if (unlikely(NICC_SUCCESS != (retval = this->__init_recvs()))){
        NICC_WARN_C("failed to initialize the recv ring buffer");
        return retval;
    }
    if (unlikely(NICC_SUCCESS != (retval = this->__init_sends()))){
        NICC_WARN_C("failed to initialize the send ring buffer");
        return retval;
    }

    return retval;
}

nicc_retval_t Channel_SoC::__init_recvs() {
    nicc_retval_t retval = NICC_SUCCESS;
    // Initialize the memory region for RECVs
    const size_t ring_extent_size = kRQDepth * kRecvMbufSize;
    NICC_ASSERT(ring_extent_size <= HugeAlloc::k_max_class_size); // Currently the max memory size for rx ring is k_max_class_size

    Buffer * ring_extent = this->_huge_alloc->alloc(ring_extent_size);
    if (ring_extent->buf_ == nullptr) {
        NICC_WARN_C("failed to allocate memory for the recv ring buffer");
        return NICC_ERROR_MEMORY_FAILURE;
    }

    // Initialize constant fields of RECV descriptors
    for (size_t i = 0; i < kRQDepth; i++) {
        uint8_t *buf = ring_extent->buf_;
        const size_t offset = (i * kRecvMbufSize);
        NICC_ASSERT(offset + kRecvMbufSize <= ring_extent_size);
        this->_recv_sgl[i].length = kRecvMbufSize;
        this->_recv_sgl[i].lkey = ring_extent->lkey_;
        this->_recv_sgl[i].addr = reinterpret_cast<uint64_t>(&buf[offset]);
        this->_recv_wr[i].wr_id = i;
        this->_recv_wr[i].sg_list = &this->_recv_sgl[i];
        this->_recv_wr[i].num_sge = 1;      /// Only one SGE per recv wr
        this->_rx_ring[i] = new Buffer(&buf[offset], kRecvMbufSize, ring_extent->lkey_);  // RX ring entry
        this->_rx_ring[i]->state_ = Buffer::kPOSTED;
        this->_recv_wr[i].next = (i < kRQDepth - 1) ? &this->_recv_wr[i + 1] : &this->_recv_wr[0];
    }

    // Circular link rx ring
    for (size_t i = 0; i < kRQDepth; i++) {
        this->_rx_ring[i]->next_ = (i < kRQDepth - 1) ? this->_rx_ring[i + 1] : this->_rx_ring[0];
    }

    // Fill the RECV queue. post_recvs() can use fast RECV and therefore not
    // actually fill the RQ, so post_recvs() isn't usable here.
    struct ibv_recv_wr *bad_wr;
    this->_recv_wr[kRQDepth - 1].next = nullptr;  // Breaker of chains, mother of dragons

    int ret = ibv_post_recv(this->_qp, &this->_recv_wr[0], &bad_wr);
    if (unlikely(ret != 0)) {
        NICC_WARN_C("failed to fill RECV queue");
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    this->_recv_wr[kRQDepth - 1].next = &this->_recv_wr[0];  // Restore circularity

    return retval;
}

nicc_retval_t Channel_SoC::__init_sends() {
    nicc_retval_t retval = NICC_SUCCESS;
    for (size_t i = 0; i < kSQDepth; i++) {
        this->_send_wr[i].opcode = IBV_WR_SEND;
        this->_send_wr[i].send_flags = IBV_SEND_SIGNALED;
        this->_send_wr[i].sg_list = &this->_send_sgl[i];
        this->_send_wr[i].num_sge = 1;
        // Circular link send wr
        this->_send_wr[i].next = (i < kSQDepth - 1) ? &this->_send_wr[i + 1] : &this->_send_wr[0];
    }
    return retval;
}

nicc_retval_t Channel_SoC::deallocate_channel() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

} // namespace nicc