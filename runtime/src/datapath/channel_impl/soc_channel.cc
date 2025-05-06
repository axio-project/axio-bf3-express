#include "datapath/channel_impl/soc_channel.h"

namespace nicc {

// GIDs are currently used only for RoCE. This default value works for most
// clusters, but we need a more robust GID selection method. Some observations:
//  * On physical clusters, gid depends on the Ethernet is three layer or two layer; i.e., if two layer, choose the one gid without ip address.
//  * On VM clusters (AWS/KVM), gid_index = 0 does not work, gid_index = 1 works
//  * Mellanox's `show_gids` script lists all GIDs on all NICs
static constexpr size_t kDefaultGIDIndex = 1;   

nicc_retval_t Channel_SoC::allocate_channel(const char *dev_name, uint8_t phy_port) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    this->_huge_alloc = new HugeAlloc(kMemRegionSize, /* numa_node */0);    // SoC only has one NUMA node
    common_resolve_phy_port(dev_name, phy_port, RDMA_SoC_QP::kMTU, this->_resolve);

    if(unlikely(NICC_SUCCESS != (retval = __roce_resolve_phy_port()))){
        NICC_WARN_C("failed to resolve phy port: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }

    if(unlikely(NICC_SUCCESS != (retval = __init_verbs_structs()))){
        NICC_WARN_C("failed to init verbs structs: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }

    if(unlikely(NICC_SUCCESS != (retval = __init_rings()))){
        NICC_WARN_C("failed to init rings: dev_name(%s), phy_port(%u), retval(%u)", dev_name, phy_port, retval);
        goto exit;
    }

exit:
    // TODO: destory if failed
    return retval;
}

nicc_retval_t Channel_SoC::connect_qp(bool is_prior, const ComponentBlock *neighbour_component_block, const QPInfo *qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    RDMA_SoC_QP *qp = is_prior ? this->qp_for_prior : this->qp_for_next;
    QPInfo *local_qp_info = is_prior ? this->qp_for_prior_info : this->qp_for_next_info;
    NICC_CHECK_POINTER(qp);
    NICC_CHECK_POINTER(this->_mr);
    NICC_CHECK_POINTER(local_qp_info);
    if (is_prior && (this->_state & kChannel_State_Prior_Connected)) {
        NICC_WARN_C("prior QP is already connected");
        return NICC_ERROR_DUPLICATED;
    } else if (!is_prior && (this->_state & kChannel_State_Next_Connected)) {
        NICC_WARN_C("next QP is already connected");
        return NICC_ERROR_DUPLICATED;
    }

    if (neighbour_component_block != nullptr) {
        NICC_CHECK_POINTER(neighbour_component_block);
        if(unlikely(NICC_SUCCESS != (retval = this->__connect_qp_to_component_block(qp, neighbour_component_block, local_qp_info)))){
            NICC_WARN_C("failed to connect QP to component block: retval(%u)", retval);
            return retval;
        }
    } else {
        NICC_CHECK_POINTER(qp_info);
        if(unlikely(NICC_SUCCESS != (retval = this->__connect_qp_to_host(qp, qp_info, local_qp_info)))){
            NICC_WARN_C("failed to connect QP to host: retval(%u)", retval);
            return retval;
        }
        /// fill the RECV queue
        if(unlikely(NICC_SUCCESS != (retval = this->__fill_recv_queue(qp)))){
            NICC_WARN_C("failed to fill RECV queue for QP: retval(%u)", retval);
            return retval;
        }
    }
    this->_state |= (is_prior ? kChannel_State_Prior_Connected : kChannel_State_Next_Connected);
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

    // Query GID information using ibv_query_gid_ex
    struct ibv_gid_entry gid_entry;
    if (ibv_query_gid_ex(this->_resolve.ib_ctx, this->_resolve.dev_port_id, kDefaultGIDIndex, &gid_entry, 0)) {
        NICC_WARN_C("failed to query gid: dev_port_id(%u), gid_index(%lu), retval(%u)", 
                   this->_resolve.dev_port_id, kDefaultGIDIndex, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // Validate GID
    if (gid_entry.gid_type != IBV_GID_TYPE_ROCE_V2) {
        NICC_WARN_C("invalid gid type: expected RoCE v2, got %d", gid_entry.gid_type);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // Copy GID information
    memcpy(&this->_resolve.gid, &gid_entry.gid, sizeof(union ibv_gid));
    this->_resolve.gid_index = gid_entry.gid_index;

    // Get interface name from index
    char ifname[IF_NAMESIZE];
    if (if_indextoname(gid_entry.ndev_ifindex, ifname) == nullptr) {
        NICC_WARN_C("failed to get interface name for index %u", gid_entry.ndev_ifindex);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // Read MAC address from sysfs
    char sys_path[256];
    snprintf(sys_path, sizeof(sys_path), "/sys/class/net/%s/address", ifname);
    FILE *maddr_file = fopen(sys_path, "r");
    if (maddr_file == nullptr) {
        NICC_WARN_C("failed to open MAC address file: %s", sys_path);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    auto ret = fscanf(maddr_file, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%*c",
                     &this->_resolve.mac_addr[0],
                     &this->_resolve.mac_addr[1],
                     &this->_resolve.mac_addr[2],
                     &this->_resolve.mac_addr[3],
                     &this->_resolve.mac_addr[4],
                     &this->_resolve.mac_addr[5]);
    fclose(maddr_file);

    if (ret != 6) {
        NICC_WARN_C("failed to read MAC address from file: %s", sys_path);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // Get IPv4 address using ioctl
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        NICC_WARN_C("failed to create socket for ioctl");
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0) {
        NICC_WARN_C("failed to get IPv4 address for interface %s", ifname);
        close(sockfd);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    this->_resolve.ipv4_addr.ip = ntohl(addr->sin_addr.s_addr);

    close(sockfd);

    // Log the resolved information
    // NICC_DEBUG("MAC address: %02x:%02x:%02x:%02x:%02x:%02x", 
    //           this->_resolve.mac_addr[0], this->_resolve.mac_addr[1], this->_resolve.mac_addr[2], 
    //           this->_resolve.mac_addr[3], this->_resolve.mac_addr[4], this->_resolve.mac_addr[5]);
    // NICC_DEBUG("GID index: %u", this->_resolve.gid_index);
    // NICC_DEBUG("GID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", 
    //           this->_resolve.gid.raw[0], this->_resolve.gid.raw[1], this->_resolve.gid.raw[2], this->_resolve.gid.raw[3], 
    //           this->_resolve.gid.raw[4], this->_resolve.gid.raw[5], this->_resolve.gid.raw[6], this->_resolve.gid.raw[7], 
    //           this->_resolve.gid.raw[8], this->_resolve.gid.raw[9], this->_resolve.gid.raw[10], this->_resolve.gid.raw[11], 
    //           this->_resolve.gid.raw[12], this->_resolve.gid.raw[13], this->_resolve.gid.raw[14], this->_resolve.gid.raw[15]);
    // NICC_DEBUG("IPv4 address: %u.%u.%u.%u", 
    //           (this->_resolve.ipv4_addr.ip >> 24) & 0xFF,
    //           (this->_resolve.ipv4_addr.ip >> 16) & 0xFF,
    //           (this->_resolve.ipv4_addr.ip >> 8) & 0xFF,
    //           this->_resolve.ipv4_addr.ip & 0xFF);

    return retval;
}

nicc_retval_t Channel_SoC::__init_verbs_structs() {
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(this->_resolve.ib_ctx);
    NICC_CHECK_POINTER(this->qp_for_prior=new RDMA_SoC_QP());
    NICC_CHECK_POINTER(this->qp_for_next=new RDMA_SoC_QP());

    // Create protection domain, send CQ, and recv CQ
    this->_pd = ibv_alloc_pd(this->_resolve.ib_ctx);
    NICC_CHECK_POINTER(this->_pd);

    // Create prior QP and next QP
    if(unlikely(NICC_SUCCESS != (retval = this->__create_qp(this->qp_for_prior)))){
        NICC_WARN_C("failed to create prior QP: retval(%u)", retval);
        return retval;
    }
    if(unlikely(NICC_SUCCESS != (retval = this->__create_qp(this->qp_for_next)))){
        NICC_WARN_C("failed to create next QP: retval(%u)", retval);
        return retval;
    }
    this->__set_local_qp_info(this->qp_for_prior_info, this->qp_for_prior);
    this->__set_local_qp_info(this->qp_for_next_info, this->qp_for_next);

    return retval;
}

nicc_retval_t Channel_SoC::__create_qp(RDMA_SoC_QP *qp) {
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(qp);
    NICC_CHECK_POINTER(this->_pd);

    /// Create send CQ
    qp->_send_cq = ibv_create_cq(this->_resolve.ib_ctx, kSQDepth, nullptr, nullptr, 0);
    NICC_CHECK_POINTER(qp->_send_cq);

    /// Create recv CQ
    qp->_recv_cq = ibv_create_cq(this->_resolve.ib_ctx, kRQDepth, nullptr, nullptr, 0);
    NICC_CHECK_POINTER(qp->_recv_cq);

    // Initialize QP creation attributes
    struct ibv_qp_init_attr create_attr;
    memset(static_cast<void *>(&create_attr), 0, sizeof(struct ibv_qp_init_attr));
    create_attr.send_cq = qp->_send_cq;
    create_attr.recv_cq = qp->_recv_cq;
    create_attr.qp_type = IBV_QPT_RC;

    create_attr.cap.max_send_wr = kSQDepth;
    create_attr.cap.max_recv_wr = kRQDepth;
    create_attr.cap.max_send_sge = 1;
    create_attr.cap.max_recv_sge = 1;
    create_attr.cap.max_inline_data = kMaxInline;

    qp->_qp = ibv_create_qp(this->_pd, &create_attr);
    NICC_CHECK_POINTER(qp->_qp);
    qp->_qp_id = qp->_qp->qp_num;



    return retval;
}

void Channel_SoC::__set_local_qp_info(QPInfo *qp_info, RDMA_SoC_QP *qp) {
    NICC_ASSERT(qp_info->is_initialized == false);
    NICC_CHECK_POINTER(qp_info);
    NICC_CHECK_POINTER(qp);
    qp_info->qp_num = qp->_qp_id;
    qp_info->lid = this->_resolve.port_lid;
    for (size_t i = 0; i < 16; i++) {
        qp_info->gid[i] = this->_resolve.gid.raw[i];
    }
    qp_info->gid_table_index = this->_resolve.gid_index;
    qp_info->mtu = RDMA_SoC_QP::kMTU;
    memcpy(qp_info->nic_name, this->_resolve.ib_ctx->device->name, MAX_NIC_NAME_LEN);
    memcpy(qp_info->mac_addr, this->_resolve.mac_addr, 6);
    qp_info->is_initialized = true;
}

nicc_retval_t Channel_SoC::__create_ah(const QPInfo *local_qp_info, const QPInfo *remote_qp_info, RDMA_SoC_QP *qp) {
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(local_qp_info);
    NICC_CHECK_POINTER(remote_qp_info);
    NICC_CHECK_POINTER(qp);
    struct ibv_ah_attr ah_attr;
    if (this->_local_ah == nullptr) {
        /// Create local AH, only once
        union ibv_gid gid; 
        memset(&gid, 0, sizeof(union ibv_gid));
        memcpy(&gid, local_qp_info->gid, sizeof(union ibv_gid));
        memset(&ah_attr, 0, sizeof(struct ibv_ah_attr));
                ah_attr.is_global = 1;
                ah_attr.dlid = 0;  // RoCE v2 doesn't use LID
                ah_attr.sl = 0;    // Service level
                ah_attr.src_path_bits = 0;
                ah_attr.port_num = _resolve.dev_port_id;  // Use the actual port number
                
                // Set GID fields
                ah_attr.grh.dgid = gid;  // Use the entire GID structure
                ah_attr.grh.sgid_index = kDefaultGIDIndex;
                ah_attr.grh.hop_limit = 1;  // Typical value for local network
                ah_attr.grh.traffic_class = 0;
                ah_attr.grh.flow_label = 0;
        
        this->_local_ah = ibv_create_ah(this->_pd, &ah_attr);
        if (unlikely(this->_local_ah == nullptr)) {
            NICC_WARN_C("failed to create local AH");
            return NICC_ERROR_HARDWARE_FAILURE;
        }
    }

    /// Create remote AH
    qp->_remote_qp_id = remote_qp_info->qp_num;
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
    qp->_remote_ah = ibv_create_ah(this->_pd, &ah_attr);
    if (unlikely(qp->_remote_ah == nullptr)) {
        NICC_WARN_C("failed to create remote AH");
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    return retval;
}

nicc_retval_t Channel_SoC::__init_rings() {
    nicc_retval_t retval = NICC_SUCCESS;
    // Initialize the ring buffer
    /// Step 1: Allocate memory for the ring buffer
    Buffer raw_mr = this->_huge_alloc->alloc_raw(kMemRegionSize, DoRegister::kTrue);
    if (raw_mr.buf_ == nullptr) {
        NICC_WARN_C("failed to allocate memory for the ring buffer");
        return NICC_ERROR_MEMORY_FAILURE;
    }
    NICC_CHECK_POINTER(this->_mr = ibv_reg_mr(this->_pd, 
                                                raw_mr.buf_, 
                                                kMemRegionSize, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC));
    raw_mr.set_lkey(this->_mr->lkey);
    this->_huge_alloc->add_raw_buffer(raw_mr, kMemRegionSize);

    /// Step 2: Initialize the ring buffer
    if (unlikely(NICC_SUCCESS != (retval = this->__init_recvs(this->qp_for_prior)))){
        NICC_WARN_C("failed to initialize the recv ring buffer for prior component block");
        return retval;
    }
    if (unlikely(NICC_SUCCESS != (retval = this->__init_sends(this->qp_for_prior)))){
        NICC_WARN_C("failed to initialize the send ring buffer for prior component block");
        return retval;
    }
    if (unlikely(NICC_SUCCESS != (retval = this->__init_recvs(this->qp_for_next)))){
        NICC_WARN_C("failed to initialize the recv ring buffer for next component block");
        return retval;
    }
    if (unlikely(NICC_SUCCESS != (retval = this->__init_sends(this->qp_for_next)))){
        NICC_WARN_C("failed to initialize the send ring buffer for next component block");
        return retval;
    }

    return retval;
}

nicc_retval_t Channel_SoC::__init_recvs(RDMA_SoC_QP *qp) {
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
        qp->_recv_sgl[i].length = kRecvMbufSize;
        qp->_recv_sgl[i].lkey = ring_extent->lkey_;
        qp->_recv_sgl[i].addr = reinterpret_cast<uint64_t>(&buf[offset]);
        qp->_recv_wr[i].wr_id = i;
        qp->_recv_wr[i].sg_list = &qp->_recv_sgl[i];
        qp->_recv_wr[i].num_sge = 1;      /// Only one SGE per recv wr
        qp->_rx_ring[i] = new Buffer(&buf[offset], kRecvMbufSize, ring_extent->lkey_);  // RX ring entry
        qp->_rx_ring[i]->state_ = Buffer::kPOSTED;
        qp->_recv_wr[i].next = (i < kRQDepth - 1) ? &qp->_recv_wr[i + 1] : &qp->_recv_wr[0];
    }

    // Circular link rx ring
    for (size_t i = 0; i < kRQDepth; i++) {
        qp->_rx_ring[i]->next_ = (i < kRQDepth - 1) ? qp->_rx_ring[i + 1] : qp->_rx_ring[0];
    }

    // Does not post RECVs here, because the qp has not been connected yet (i.e., qp has not been changed to RTR state)

    return retval;
}

nicc_retval_t Channel_SoC::__init_sends(RDMA_SoC_QP *qp) {
    nicc_retval_t retval = NICC_SUCCESS;
    for (size_t i = 0; i < kSQDepth; i++) {
        qp->_send_wr[i].opcode = IBV_WR_SEND;
        qp->_send_wr[i].send_flags = IBV_SEND_SIGNALED;
        qp->_send_wr[i].sg_list = &qp->_send_sgl[i];
        qp->_send_wr[i].num_sge = 1;
        // Circular link send wr
        qp->_send_wr[i].next = (i < kSQDepth - 1) ? &qp->_send_wr[i + 1] : &qp->_send_wr[0];
    }
    return retval;
}

nicc_retval_t Channel_SoC::__connect_qp_to_component_block(RDMA_SoC_QP *qp, const ComponentBlock *neighbour_component_block, const QPInfo *local_qp_info) {
    // nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return NICC_ERROR_NOT_IMPLEMENTED;
}

nicc_retval_t Channel_SoC::__connect_qp_to_host(RDMA_SoC_QP *qp, const QPInfo *remote_qp_info, const QPInfo *local_qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    /// Transition QP to INIT state
    struct ibv_qp_attr init_attr;
    memset(static_cast<void *>(&init_attr), 0, sizeof(struct ibv_qp_attr));
    init_attr.qp_state = IBV_QPS_INIT;
    init_attr.pkey_index = 0;
    init_attr.port_num = static_cast<uint8_t>(this->_resolve.dev_port_id);
    init_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | 
                                IBV_ACCESS_REMOTE_WRITE | 
                                IBV_ACCESS_REMOTE_READ | 
                                IBV_ACCESS_REMOTE_ATOMIC;
    int attr_mask = IBV_QP_STATE | 
                    IBV_QP_PKEY_INDEX | 
                    IBV_QP_PORT | 
                    IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(qp->_qp, &init_attr, attr_mask) != 0) {
        NICC_WARN_C("failed to modify QP to INIT: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    /// RTR state
    struct ibv_qp_attr rtr_attr;
    memset(static_cast<void *>(&rtr_attr), 0, sizeof(struct ibv_qp_attr));
    rtr_attr.qp_state = IBV_QPS_RTR;
    switch(RDMA_SoC_QP::kMTU){
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
            NICC_WARN_C("unsupported MTU: %lu, only 1024, 2048, 4096 are supported", RDMA_SoC_QP::kMTU);
            return NICC_ERROR_HARDWARE_FAILURE;
    }
    rtr_attr.dest_qp_num = remote_qp_info->qp_num;
    rtr_attr.rq_psn = 0;
    rtr_attr.max_dest_rd_atomic = 1;
    rtr_attr.min_rnr_timer = 12;

    rtr_attr.ah_attr.sl = 0;
    rtr_attr.ah_attr.src_path_bits = 0;
    rtr_attr.ah_attr.port_num = 1;
    rtr_attr.ah_attr.dlid = remote_qp_info->lid;
    memcpy(&rtr_attr.ah_attr.grh.dgid, remote_qp_info->gid, 16);
    rtr_attr.ah_attr.is_global = 1;
    rtr_attr.ah_attr.grh.sgid_index = kDefaultGIDIndex;
    rtr_attr.ah_attr.grh.hop_limit = 2;
    rtr_attr.ah_attr.grh.traffic_class = 0;
    if (ibv_modify_qp(qp->_qp, &rtr_attr,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                      IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                      IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        NICC_WARN_C("failed to modify QP to RTR: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    /// create address handles
    if(unlikely(NICC_SUCCESS != (retval = this->__create_ah(local_qp_info, remote_qp_info, qp)))){
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
    if (ibv_modify_qp(qp->_qp, &rtr_attr,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                      IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
        NICC_WARN_C("failed to modify QP to RTS: retval(%u)", retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    qp->_remote_qp_id = remote_qp_info->qp_num;

    return retval;
}



nicc_retval_t Channel_SoC::__fill_recv_queue(RDMA_SoC_QP *qp) {
    nicc_retval_t retval = NICC_SUCCESS;
    // Fill the RECV queue. post_recvs() can use fast RECV and therefore not
    // actually fill the RQ, so post_recvs() isn't usable here.
    struct ibv_recv_wr *bad_wr;
    qp->_recv_wr[kRQDepth - 1].next = nullptr;  // Breaker of chains, mother of dragons

    int ret = ibv_post_recv(qp->_qp, &qp->_recv_wr[0], &bad_wr);
    if (unlikely(ret != 0)) {
        NICC_WARN_C("failed to fill RECV queue");
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    qp->_recv_wr[kRQDepth - 1].next = &qp->_recv_wr[0];  // Restore circularity
    return retval;
}

nicc_retval_t Channel_SoC::deallocate_channel() {
    nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return retval;
}

} // namespace nicc