#include "datapath/channel_impl/dpa_channel.h"

namespace nicc {

// GIDs are currently used only for RoCE. This default value works for most
// clusters, but we need a more robust GID selection method. Some observations:
//  * On physical clusters, gid depends on the Ethernet is three layer or two layer; i.e., if two layer, choose the one gid without ip address.
//  * On VM clusters (AWS/KVM), gid_index = 0 does not work, gid_index = 1 works
//  * Mellanox's `show_gids` script lists all GIDs on all NICs
static constexpr size_t kDefaultGIDIndex = 1;   

/**
 * ===========================Public methods===========================
 */
nicc_retval_t Channel_DPA::allocate_channel(struct ibv_pd *pd, 
                                            struct flexio_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct flexio_event_handler	*event_handler,
                                            struct ibv_context *ibv_ctx,
                                            const char *dev_name,
                                            uint8_t phy_port) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    NICC_CHECK_POINTER(pd);
    NICC_CHECK_POINTER(uar);
    NICC_CHECK_POINTER(flexio_process);
    NICC_CHECK_POINTER(event_handler);
    NICC_CHECK_POINTER(ibv_ctx);

    // query mlx5 port attributes
    common_resolve_phy_port(dev_name, phy_port, LOG2VALUE(DPA_LOG_WQ_DATA_ENTRY_BSIZE), this->_resolve);
    this->__roce_resolve_phy_port();

    /* allocate dev_queues for prior and next */
    // allocate SQ's CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_sq_cq(pd, uar, flexio_process, ibv_ctx, dev_queues_for_prior, &_flexio_queues_handler_for_prior)
    ))){
        NICC_WARN_C(
            "failed to allocate and init SQ and corresponding CQ/DBR for prior: nicc_retval(%u)", retval
        );
        goto exit;
    }
    if (unlikely(NICC_SUCCESS != (
        retval = this->__allocate_sq_cq(pd, uar, flexio_process, ibv_ctx, dev_queues_for_next, &_flexio_queues_handler_for_next)
    ))){
        NICC_WARN_C(
            "failed to allocate and init SQ and corresponding CQ/DBR for next: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // allocate RQ's CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_rq_cq(pd, uar, flexio_process, event_handler, ibv_ctx, dev_queues_for_prior, &_flexio_queues_handler_for_prior)
    ))){
        NICC_WARN_C(
            "failed to allocate and init RQ and corresponding CQ/DBR for prior: nicc_retval(%u)", retval
        );
        goto exit;
    }
    if (unlikely(NICC_SUCCESS != (
        retval = this->__allocate_rq_cq(pd, uar, flexio_process, event_handler, ibv_ctx, dev_queues_for_next, &_flexio_queues_handler_for_next)
    ))){
        NICC_WARN_C(
            "failed to allocate and init RQ and corresponding CQ/DBR for next: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // create Queue Pair for prior
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_qp(pd, uar, flexio_process, dev_queues_for_prior, &_flexio_queues_handler_for_prior, this->_typeid_of_prior)
    ))){
        NICC_WARN_C("failed to create prior queue pair: nicc_retval(%u)", retval);
        goto exit;
    }
    
    // create Queue Pair for next
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_qp(pd, uar, flexio_process, dev_queues_for_next, &_flexio_queues_handler_for_next, this->_typeid_of_next)
    ))){
        NICC_WARN_C("failed to create next queue pair: nicc_retval(%u)", retval);
        goto exit;
    }

    // /// Try to get gid
    // if (this->__get_gid(ibv_ctx) != NICC_SUCCESS) {
    //     NICC_WARN_C("failed to get gid: dev_port_id(%u)", 1);
    //     return NICC_ERROR_HARDWARE_FAILURE;
    // }

    // Set QP info for prior and next
    if (this->_typeid_of_prior == Channel::channel_typeid_t::RDMA) {
        this->__set_local_qp_info(this->qp_for_prior_info, dev_queues_for_prior);
    }
    if (this->_typeid_of_next == Channel::channel_typeid_t::RDMA) {
        this->__set_local_qp_info(this->qp_for_next_info, dev_queues_for_next);
    }

    /* copy queue metadata to device */
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(
            flexio_process, dev_queues_for_prior,
            sizeof(struct dpa_data_queues), &this->dev_metadata_for_prior
        )
    ))){
        NICC_WARN_C(
            "failed to copy queue metadata to DPA heap memories: "
            "flexio_process(%p), flexio_retval(%u), ",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(
            flexio_process, dev_queues_for_next,
            sizeof(struct dpa_data_queues), &this->dev_metadata_for_next
        )
    ))){
        NICC_WARN_C(
            "failed to copy queue metadata to DPA heap memories: "
            "flexio_process(%p), flexio_retval(%u), ",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

 exit:
    if(retval != NICC_SUCCESS){
        // TODO: destory if failed
    }

    return retval;
}

nicc_retval_t Channel_DPA::deallocate_channel(struct flexio_process *flexio_process) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    NICC_CHECK_POINTER(flexio_process);

    // deallocate prior component resources
    // deallocate SQ CQ for prior
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_sq_cq(flexio_process, dev_queues_for_prior, &_flexio_queues_handler_for_prior)
    ))){
        NICC_WARN_C(
            "failed to deallocate prior SQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate RQ CQ for prior
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_rq_cq(flexio_process, dev_queues_for_prior, &_flexio_queues_handler_for_prior)
    ))){
        NICC_WARN_C(
            "failed to deallocate prior RQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }
    
    // deallocate next component resources
    // deallocate SQ CQ for next
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_sq_cq(flexio_process, dev_queues_for_next, &_flexio_queues_handler_for_next)
    ))){
        NICC_WARN_C(
            "failed to deallocate next SQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate RQ CQ for next
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_rq_cq(flexio_process, dev_queues_for_next, &_flexio_queues_handler_for_next)
    ))){
        NICC_WARN_C(
            "failed to deallocate next RQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate QP for prior
    if(unlikely(NICC_SUCCESS != (
        retval = this->__destroy_qp(flexio_process, dev_queues_for_prior, &_flexio_queues_handler_for_prior)
    ))){
        NICC_WARN_C(
            "failed to deallocate prior SQ/RQ: nicc_retval(%u)", retval
        );
        goto exit;
    }
    
    // deallocate QP for next
    if(unlikely(NICC_SUCCESS != (
        retval = this->__destroy_qp(flexio_process, dev_queues_for_next, &_flexio_queues_handler_for_next)
    ))){
        NICC_WARN_C(
            "failed to deallocate next SQ/RQ: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // free on-host queue metadata
    if(likely(this->dev_queues_for_prior != nullptr)){
        free(this->dev_queues_for_prior);
    }
    if(likely(this->dev_queues_for_next != nullptr)){
        free(this->dev_queues_for_next);
    }

    // free on-device queue metadata
    if(likely(this->dev_metadata_for_prior != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_buf_dev_free(flexio_process, this->dev_metadata_for_prior)
        ))){
            NICC_WARN_C(
                "failed to free on-device queue metadata: "
                "flexio_process(%p), flexio_retval(%u), ",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }
    if(likely(this->dev_metadata_for_next != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_buf_dev_free(flexio_process, this->dev_metadata_for_next)
        ))){
            NICC_WARN_C(
                "failed to free on-device queue metadata: "
                "flexio_process(%p), flexio_retval(%u), ",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

exit:
    return retval;
}

nicc_retval_t Channel_DPA::connect_qp(bool is_prior, 
                                      const ComponentBlock *neighbour_component_block, 
                                      const QPInfo *qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    dpa_data_queues *dev_queues = is_prior ? this->dev_queues_for_prior : 
                                      this->dev_queues_for_next;
    struct flexio_queues_handler *flexio_queues_handler = is_prior ? 
                                                        &_flexio_queues_handler_for_prior : 
                                                        &_flexio_queues_handler_for_next;
    QPInfo *local_qp_info = is_prior ? this->qp_for_prior_info : this->qp_for_next_info;
    NICC_CHECK_POINTER(dev_queues);
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
        if(unlikely(NICC_SUCCESS != (retval = this->__connect_qp_to_component_block(dev_queues, neighbour_component_block, local_qp_info)))){
            NICC_WARN_C("failed to connect QP to component block: retval(%u)", retval);
            return retval;
        }
    } else {
        NICC_CHECK_POINTER(qp_info);
        if(unlikely(NICC_SUCCESS != (retval = this->__connect_qp_to_host(dev_queues, 
                                                                         flexio_queues_handler,
                                                                         qp_info, local_qp_info)))){
            NICC_WARN_C("failed to connect QP to host: retval(%u), is_prior(%d)", retval, is_prior);
            return retval;
        }
        NICC_DEBUG_C("QP connected to host: qp_num(%u), is_prior(%d)", qp_info->qp_num, is_prior);
        /// fill the RECV queue
        // if(unlikely(NICC_SUCCESS != (retval = this->__fill_recv_queue(dev_queues)))){
        //     NICC_WARN_C("failed to fill RECV queue for QP: retval(%u)", retval);
        //     return retval;
        // }
    }
    this->_state |= (is_prior ? kChannel_State_Prior_Connected : kChannel_State_Next_Connected);
    return retval;
}

/**
 * ===========================Internel methods===========================
 */
nicc_retval_t Channel_DPA::__roce_resolve_phy_port() {
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
    // struct ibv_gid_entry gid_entry;
    // if (ibv_query_gid_ex(this->_resolve.ib_ctx, this->_resolve.dev_port_id, kDefaultGIDIndex, &gid_entry, 0)) {
    //     NICC_WARN_C("failed to query gid: dev_port_id(%u), gid_index(%lu), retval(%u)", 
    //                this->_resolve.dev_port_id, kDefaultGIDIndex, retval);
    //     return NICC_ERROR_HARDWARE_FAILURE;
    // }

    // // Validate GID
    // if (gid_entry.gid_type != IBV_GID_TYPE_ROCE_V2) {
    //     NICC_WARN_C("invalid gid type: expected RoCE v2, got %d", gid_entry.gid_type);
    //     return NICC_ERROR_HARDWARE_FAILURE;
    // }

    
    // Copy GID information
    // memcpy(&this->_resolve.gid, &gid_entry.gid, sizeof(union ibv_gid));
    // this->_resolve.gid_index = gid_entry.gid_index;

    // Manually assign GID, seems that the GID should be obtained from the host side
    // \todo: obtain the GID from the host side
    // rocep202s0f0    1       1       fe80:0000:0000:0000:a288:c2ff:febf:9b10
    this->_resolve.gid_index = 1;
    this->_resolve.gid.raw[0] = 0xfe;
    this->_resolve.gid.raw[1] = 0x80;
    this->_resolve.gid.raw[2] = 0x00;
    this->_resolve.gid.raw[3] = 0x00;
    this->_resolve.gid.raw[4] = 0x00;
    this->_resolve.gid.raw[5] = 0x00;
    this->_resolve.gid.raw[6] = 0x00;
    this->_resolve.gid.raw[7] = 0x00;
    this->_resolve.gid.raw[8] = 0xa2;
    this->_resolve.gid.raw[9] = 0x88;
    this->_resolve.gid.raw[10] = 0xc2;
    this->_resolve.gid.raw[11] = 0xff;
    this->_resolve.gid.raw[12] = 0xfe;
    this->_resolve.gid.raw[13] = 0xbf;
    this->_resolve.gid.raw[14] = 0x9b;
    this->_resolve.gid.raw[15] = 0x10;

    // Get interface name from index
    char ifname[IF_NAMESIZE];
    // if (if_indextoname(gid_entry.ndev_ifindex, ifname) == nullptr) {
    //     NICC_WARN_C("failed to get interface name for index %u", gid_entry.ndev_ifindex);
    //     return NICC_ERROR_HARDWARE_FAILURE;
    // }
    // Manually assign interface name
    strncpy(ifname, "p0", IFNAMSIZ - 1);
    ifname[IFNAMSIZ - 1] = '\0';

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

nicc_retval_t Channel_DPA::__get_gid(struct ibv_context *ibv_ctx) {
    nicc_retval_t retval = NICC_SUCCESS;
    struct ibv_port_attr port_attr;
    if (ibv_query_port(ibv_ctx, 1, &port_attr) != 0) {
        NICC_WARN_C("failed to query port: dev_port_id(%u), retval(%u)", 1, retval);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    ibv_gid testgid;
    int rc = ibv_query_gid(ibv_ctx, 1, 0, &testgid);
    
    struct ibv_gid_entry *gid_tbl_entries = new struct ibv_gid_entry[32];
    memset(gid_tbl_entries, 0, 32 * sizeof(struct ibv_gid_entry));

	auto num_entries = ibv_query_gid_table(ibv_ctx, gid_tbl_entries, 32, 0);

    printf("num_entries: %d\n", num_entries);

    if (num_entries == 0) {
        NICC_WARN_C("no GID entries found");
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    return retval;
}

nicc_retval_t Channel_DPA::__allocate_sq_cq(struct ibv_pd *pd, 
                                            struct flexio_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct ibv_context *ibv_ctx,
                                            struct dpa_data_queues *dev_queues,
                                            struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    uint32_t cq_num;

    // attributes of CQ
    struct flexio_cq_attr sqcq_attr = {
      .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
      // SQ does not need APU CQ
      .element_type = FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ,   
      .uar_id = flexio_uar_get_id(uar)
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            flexio_process, DPA_LOG_CQ_RING_DEPTH, &(dev_queues->sq_cq_data)
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        return retval;
    }
    sqcq_attr.cq_dbr_daddr = dev_queues->sq_cq_data.cq_dbr_daddr;
    sqcq_attr.cq_ring_qmem.daddr = dev_queues->sq_cq_data.cq_ring_daddr;

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_cq_create(flexio_process, ibv_ctx, &sqcq_attr, &flexio_queues_handler->flexio_sq_cq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    cq_num = flexio_cq_get_cq_num(flexio_queues_handler->flexio_sq_cq_ptr);
    dev_queues->sq_cq_data.cq_num = cq_num;
    dev_queues->sq_cq_data.log_cq_depth = DPA_LOG_CQ_RING_DEPTH;

    return retval;
}


nicc_retval_t Channel_DPA::__allocate_rq_cq(struct ibv_pd *pd, 
                                            struct flexio_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct flexio_event_handler	*event_handler,
                                            struct ibv_context *ibv_ctx,
                                            struct dpa_data_queues *dev_queues,
                                            struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    uint32_t cq_num;	    // CQ number

    // RQ's CQ attributes
    struct flexio_cq_attr rqcq_attr = {
        .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
        .element_type = FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD,
        .thread = flexio_event_handler_get_thread(event_handler),
        .uar_id = flexio_uar_get_id(uar)
        // .uar_base_addr = uar->base_addr      // seems not needed in doca v2.9.2
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            flexio_process, DPA_LOG_CQ_RING_DEPTH, &dev_queues->rq_cq_data
        )
    ))){
        NICC_WARN_C(
            "failed to allocate RQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        return retval;
    }
    rqcq_attr.cq_dbr_daddr = dev_queues->rq_cq_data.cq_dbr_daddr;
    rqcq_attr.cq_ring_qmem.daddr = dev_queues->rq_cq_data.cq_ring_daddr;

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_cq_create(flexio_process, NULL, &rqcq_attr, &flexio_queues_handler->flexio_rq_cq_ptr))
    )){
        NICC_WARN_C(
            "failed to create flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    cq_num = flexio_cq_get_cq_num(flexio_queues_handler->flexio_rq_cq_ptr);
    dev_queues->rq_cq_data.cq_num = cq_num;
    dev_queues->rq_cq_data.log_cq_depth = DPA_LOG_RQ_RING_DEPTH;

    return retval;
}

nicc_retval_t Channel_DPA::__create_qp(struct ibv_pd *pd,
                                       struct flexio_uar *uar,
                                       struct flexio_process *flexio_process,
                                       struct dpa_data_queues *dev_queues,
                                       struct flexio_queues_handler *flexio_queues_handler,
                                       channel_typeid_t type){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(pd);
    NICC_CHECK_POINTER(uar);
    NICC_CHECK_POINTER(flexio_process);
    NICC_CHECK_POINTER(dev_queues);
    NICC_CHECK_POINTER(flexio_queues_handler);

    switch(type){
        case Channel::channel_typeid_t::RDMA:
            dev_queues->type = Channel::channel_typeid_t::RDMA;
            retval = this->__create_rdma_qp(pd, uar, flexio_process, dev_queues, flexio_queues_handler);
            break;
        case Channel::channel_typeid_t::ETHERNET:
            dev_queues->type = Channel::channel_typeid_t::ETHERNET;
            retval = this->__create_ethernet_qp(pd, uar, flexio_process, dev_queues, flexio_queues_handler);
            break;
        default:
            retval = NICC_ERROR_NOT_FOUND;
            break;
    }
    return retval;
}

nicc_retval_t Channel_DPA::__create_ethernet_qp(struct ibv_pd *pd,
                                                struct flexio_uar *uar,
                                                struct flexio_process *flexio_process,
                                                struct dpa_data_queues *dev_queues,
                                                struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    __be32 dbr[2] = { 0, 0 };
    // attributes of SQ
    struct flexio_wq_attr sq_attr = {
      .log_wq_depth = DPA_LOG_SQ_RING_DEPTH,
      .uar_id = flexio_uar_get_id(uar),
      .pd = pd
    };
    // RQ attributes
    struct flexio_wq_attr rq_attr = {
        .log_wq_depth = DPA_LOG_RQ_RING_DEPTH,
        .pd = pd
    };

    // ------------------------- SQ -------------------------
    // allocate memory for SQ
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_wq_memory(
            flexio_process, DPA_LOG_SQ_RING_DEPTH, 64, &dev_queues->sq_data
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    // create SQ on flexio driver
    sq_attr.wq_ring_qmem.daddr = dev_queues->sq_data.wq_ring_daddr;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_sq_create(flexio_process, NULL, dev_queues->sq_cq_data.cq_num, &sq_attr, &flexio_queues_handler->flexio_sq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%d)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    dev_queues->sq_data.wq_num = flexio_sq_get_wq_num(flexio_queues_handler->flexio_sq_ptr);
    
    // create mkey for SQ data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ flexio_process,
            /* pd */ pd,
            /* daddr */ dev_queues->sq_data.wqd_daddr,
            /* log_bsize */ DPA_LOG_SQ_RING_DEPTH + DPA_LOG_WQ_DATA_ENTRY_BSIZE,
            /* access */ IBV_ACCESS_LOCAL_WRITE,
            /* mkey */ &this->_sqd_mkey
        )
    ))){
        NICC_WARN_C(
            "failed to create mkey for SQ data buffers: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

    dev_queues->sq_data.wqd_mkey_id = flexio_mkey_get_id(this->_sqd_mkey);

    // ------------------------- RQ -------------------------
    // allocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_wq_memory(
            flexio_process, DPA_LOG_RQ_RING_DEPTH, sizeof(struct mlx5_wqe_data_seg), &dev_queues->rq_data
        )
    ))){
        NICC_WARN_C(
            "failed to allocate RQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    // create mkey for RQ data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ flexio_process,
            /* pd */ pd,
            /* daddr */ dev_queues->rq_data.wqd_daddr,
            /* log_bsize */ DPA_LOG_RQ_RING_DEPTH + DPA_LOG_WQ_DATA_ENTRY_BSIZE,
            /* access */ IBV_ACCESS_LOCAL_WRITE,
            /* mkey */ &this->_rqd_mkey
        )
    ))){
        NICC_WARN_C(
            "failed to create mkey for RQ data buffers: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    dev_queues->rq_data.wqd_mkey_id = flexio_mkey_get_id(this->_rqd_mkey);

    // init WQEs on the RQ ring
    if(unlikely(NICC_SUCCESS != (
        retval = __init_rq_ring_wqes(
            /* process */ flexio_process,
            /* rq_ring_daddr */ dev_queues->rq_data.wq_ring_daddr,
            /* log_depth */ DPA_LOG_RQ_RING_DEPTH,
            /* data_daddr */ dev_queues->rq_data.wqd_daddr,
            /* wqd_mkey_id */ dev_queues->rq_data.wqd_mkey_id
        )
    ))){
        NICC_WARN_C("failed to initialize WQEs within RQ ring");
        goto exit;
    }

    // create RQ on flexio driver
    rq_attr.wq_dbr_qmem.memtype = FLEXIO_MEMTYPE_DPA;
    rq_attr.wq_dbr_qmem.daddr = dev_queues->rq_data.wq_dbr_daddr;
    rq_attr.wq_ring_qmem.daddr = dev_queues->rq_data.wq_ring_daddr;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_rq_create(flexio_process, NULL, dev_queues->rq_cq_data.cq_num, &rq_attr, &flexio_queues_handler->flexio_rq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    dev_queues->rq_data.wq_num = flexio_rq_get_wq_num(flexio_queues_handler->flexio_rq_ptr);

    // modify RQ's DBR record to count for the number of WQEs
    dbr[0] = htobe32(LOG2VALUE(DPA_LOG_RQ_RING_DEPTH) & 0xffff);    // recv counter
    dbr[1] = htobe32(0 & 0xffff);                                   // send counter
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_host2dev_memcpy(flexio_process, dbr, sizeof(dbr), dev_queues->rq_data.wq_dbr_daddr)
    ))){
        NICC_WARN_C(
            "failed to modify DBR for RQ for counting all allocated slot: flexio_retval(%u), flexio_process(%p)",
            ret, flexio_process
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    // TODO: if unsuccessful, free allocated memories

    return retval;
}

nicc_retval_t Channel_DPA::__create_rdma_qp(struct ibv_pd *pd,
                                            struct flexio_uar *uar,
                                            struct flexio_process *flexio_process,
                                            struct dpa_data_queues *dev_queues,
                                            struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    struct flexio_qp_attr flexio_qp_fattr;
    memset(&flexio_qp_fattr, 0, sizeof(struct flexio_qp_attr));

    // allocate doorbell record
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_dbr(flexio_process, &dev_queues->qp_data.qp_dbr_daddr))
    )){
        NICC_WARN_C("failed to allocate doorbell record for QP: flexio_process(%p), doca_retval(%u), ", flexio_process, retval);
        return retval;
    }
    // allocate QP memory resources
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_qp_memory(
            flexio_process,
            &dev_queues->qp_data,
            DPA_LOG_SQ_RING_DEPTH,
            DPA_LOG_RQ_RING_DEPTH,
            sizeof(struct mlx5_wqe_data_seg)
        )
    ))){
        NICC_WARN_C("failed to allocate QP data memory: flexio_process(%p), doca_retval(%u), ", flexio_process, retval);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    dev_queues->qp_data.qpd_daddr = this->_qpd_mbuf_start_addr;

    // create mkey for QP data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ flexio_process,
            /* pd */ pd,
            /* daddr */ this->_qpd_mbuf_start_addr,
            /* log_bsize */ DPA_LOG_SQ_RING_DEPTH + 1 + DPA_LOG_WQ_DATA_ENTRY_BSIZE,    // this is ugly, we assume SQ Ring Depth = RQ Ring Depth, thus we add 1 to the log_bsize
            /* access */ IBV_ACCESS_LOCAL_WRITE  |
                         IBV_ACCESS_REMOTE_WRITE |
                         IBV_ACCESS_REMOTE_READ  |
                         IBV_ACCESS_RELAXED_ORDERING,
            /* mkey */ &this->_qpd_mkey
        )
    ))){
        NICC_WARN_C("failed to create mkey for QP data buffers: flexio_process(%p), doca_retval(%u), ", flexio_process, retval);
        goto exit;
    }
    dev_queues->qp_data.qpd_mkey_id = flexio_mkey_get_id(this->_qpd_mkey);
    /// create mr and lkey for qpd, then we can post receive for this qp
    this->_qpd_mr = new struct ibv_mr;
    memset(this->_qpd_mr, 0, sizeof(struct ibv_mr));
    this->_qpd_mr->lkey = dev_queues->qp_data.qpd_mkey_id;
    this->_qpd_mr->rkey = this->_qpd_mr->lkey;
    dev_queues->qp_data.qpd_lkey = this->_qpd_mr->lkey;

    // create QP
    flexio_qp_fattr.transport_type              = FLEXIO_QPC_ST_RC;
    flexio_qp_fattr.log_sq_depth                = DPA_LOG_SQ_RING_DEPTH;
    flexio_qp_fattr.log_rq_depth                = DPA_LOG_RQ_RING_DEPTH;
    flexio_qp_fattr.qp_wq_buff_qmem.daddr       = dev_queues->qp_data.qp_rq_daddr;
    flexio_qp_fattr.qp_wq_dbr_qmem.memtype      = FLEXIO_MEMTYPE_DPA;
    flexio_qp_fattr.qp_wq_dbr_qmem.daddr        = dev_queues->qp_data.qp_dbr_daddr;
    flexio_qp_fattr.uar_id                      = flexio_uar_get_id(uar);
    flexio_qp_fattr.rq_cqn                      = dev_queues->rq_cq_data.cq_num;
    flexio_qp_fattr.sq_cqn                      = dev_queues->sq_cq_data.cq_num;
    flexio_qp_fattr.rq_type                     = FLEXIO_QP_QPC_RQ_TYPE_REGULAR;
    flexio_qp_fattr.pd                          = pd;
    flexio_qp_fattr.qp_access_mask              = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    flexio_qp_fattr.ops_flag                    = FLEXIO_QP_WR_RDMA_WRITE | 
                                                  FLEXIO_QP_WR_RDMA_READ |
                                                  FLEXIO_QP_WR_ATOMIC_CMP_AND_SWAP;

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_qp_create(flexio_process, nullptr, &flexio_qp_fattr, &flexio_queues_handler->flexio_qp_ptr)
    ))){
        NICC_WARN_C("failed to create flexio QP on flexio driver: flexio_process(%p), flexio_retval(%u)", flexio_process, ret);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    dev_queues->qp_data.qp_num = flexio_qp_get_qp_num(flexio_queues_handler->flexio_qp_ptr);
    dev_queues->qp_data.log_qp_sq_depth = DPA_LOG_SQ_RING_DEPTH;
    dev_queues->qp_data.log_qp_rq_depth = DPA_LOG_RQ_RING_DEPTH;
    
exit:
    // TODO: if unsuccessful, free allocated memories

    return retval;
}

nicc_retval_t Channel_DPA::__allocate_wq_memory(struct flexio_process *process, 
                                                int log_depth, 
                                                uint64_t wqe_size, 
                                                struct dpa_eth_wq *wq_transf){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(wq_transf);
    NICC_ASSERT(wqe_size > 0 && wqe_size % 2 == 0);

    wq_transf->wqd_daddr = static_cast<flexio_uintptr_t>(0x00);
    wq_transf->wq_ring_daddr = static_cast<flexio_uintptr_t>(0x00);

    // allocate data buffers, these memories is used by dpa kernel
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_buf_dev_alloc(process, LOG2VALUE(log_depth + DPA_LOG_WQ_DATA_ENTRY_BSIZE), &wq_transf->wqd_daddr))
    )) {
        NICC_WARN_C(
            "failed to allocate WQ data buffer: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // allocate descriptor (wqe) queue, these memories is used by flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_buf_dev_alloc(process, LOG2VALUE(log_depth) * wqe_size, &wq_transf->wq_ring_daddr))
    )){
        NICC_WARN_C(
            "failed to allocate WQ ring: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // allocate doorbell record
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_dbr(process, &wq_transf->wq_dbr_daddr))
    )){
        NICC_WARN_C("failed to allocate doorbell record for WQ: flexio_process(%p), doca_retval(%u), ", process, retval);
        goto exit;
    }
    
exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(wq_transf->wqd_daddr != static_cast<flexio_uintptr_t>(0x00)){
            ret = flexio_buf_dev_free(process, wq_transf->wqd_daddr);
            NICC_ASSERT(ret == FLEXIO_STATUS_SUCCESS);
        }

        if(wq_transf->wq_ring_daddr != static_cast<flexio_uintptr_t>(0x00)){
            ret = flexio_buf_dev_free(process, wq_transf->wq_ring_daddr);
            NICC_ASSERT(ret == FLEXIO_STATUS_SUCCESS);
        }
    }

    return retval;
}

nicc_retval_t Channel_DPA::__allocate_qp_memory(struct flexio_process *process,
                                                struct dpa_qp *qp_transf,
                                                int log_sq_depth,
                                                int log_rq_depth,
                                                uint64_t wqe_size){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    flexio_uintptr_t buff_daddr;
	size_t buff_bsize = 0;
	size_t rq_bsize = 0;
	size_t sq_bsize = 0;

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(qp_transf);
    NICC_ASSERT(wqe_size > 0 && wqe_size % 2 == 0);

    // allocate data buffers, these memories is used by dpa kernel
    /// mempool has sq ring depth + rq ring depth mbufs
    /// then, total size is (sq ring depth + rq ring depth) * MTU size
    /// _qpd_mbuf_start_addr is used to store the start address of the first mbuf
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_buf_dev_alloc(process, (LOG2VALUE(log_sq_depth) + LOG2VALUE(log_rq_depth)) * LOG2VALUE(DPA_LOG_WQ_DATA_ENTRY_BSIZE), &this->_qpd_mbuf_start_addr))
    )) {
        NICC_WARN_C(
            "failed to allocate QP data buffer: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    // allocate wqe queue, these memories is used by flexio driver
    rq_bsize = LOG2VALUE(log_rq_depth) * wqe_size;
    sq_bsize = LOG2VALUE(log_sq_depth) * wqe_size;
    buff_bsize = rq_bsize + sq_bsize;

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_buf_dev_alloc(process, buff_bsize, &buff_daddr)
    ))){
        NICC_WARN_C(
            "failed to allocate buffer for QP: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    /* ring buffer start from RQ, and after RQ is SQ*/
    qp_transf->qp_rq_daddr = buff_daddr;
    qp_transf->qp_sq_daddr = buff_daddr + rq_bsize;

    return retval;
}


nicc_retval_t Channel_DPA::__allocate_cq_memory(struct flexio_process *process, 
                                                int log_depth, 
                                                struct dpa_cq *app_cq){
    nicc_retval_t retval = NICC_SUCCESS;
    struct mlx5_cqe64 *cq_ring_src = nullptr, *cqe = nullptr;
    size_t ring_bsize;
    int i, num_of_cqes;
    const int log_cqe_bsize = 6; // CQE size is 64 bytes
    doca_error_t result = DOCA_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(app_cq);

    // allocate doorbell record
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_dbr(process, &app_cq->cq_dbr_daddr)
    ))){
        NICC_WARN_C("failed to allocate CQ DB record: flexio_process(%p), doca_retval(%u), ", process, result);
        return retval;
    }

    // allocate temp CQ at DRAM
    num_of_cqes = LOG2VALUE(log_depth);
    ring_bsize = num_of_cqes * LOG2VALUE(log_cqe_bsize);
    cq_ring_src = (struct mlx5_cqe64*)calloc(num_of_cqes, LOG2VALUE(log_cqe_bsize));
    if(unlikely(cq_ring_src == nullptr)){
        NICC_WARN_C("failed to allocate temp CQ ring on DRAM, not enough memory: flexio_process(%p)", process);
        return NICC_ERROR_EXSAUSTED;
    }

    cqe = cq_ring_src;
    for (i = 0; i < num_of_cqes; i++)
        mlx5dv_set_cqe_owner(cqe++, 1);

    // copy CQEs from host to FlexIO CQ ring
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(
            /* process */ process,
            /* src_haddr */ cq_ring_src,
            /* buff_bsize */ ring_bsize,
            /* dest_daddr_p */ &app_cq->cq_ring_daddr
        )
    ))){
        NICC_WARN_C(
            "failed to copy temp CQ ring to DPA: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    // release tmp CQ
    if(cq_ring_src != nullptr){
        free(cq_ring_src);
    }
    
    return retval;
}


nicc_retval_t Channel_DPA::__init_rq_ring_wqes(struct flexio_process *process, 
                                               flexio_uintptr_t rq_ring_daddr, 
                                               int log_depth, 
                                               flexio_uintptr_t data_daddr, 
                                               uint32_t wqd_mkey_id){
    nicc_retval_t retval = NICC_SUCCESS;
    struct mlx5_wqe_data_seg *mock_rx_ring, *mock_rx_ring_ptr;
    uint64_t i;
    flexio_status ret;

    NICC_CHECK_POINTER(process);
    NICC_ASSERT(rq_ring_daddr != static_cast<flexio_uintptr_t>(0x00));
    NICC_ASSERT(data_daddr != static_cast<flexio_uintptr_t>(0x00));

    // allocate temp memories
    mock_rx_ring = (struct mlx5_wqe_data_seg*)calloc(LOG2VALUE(log_depth), LOG2VALUE(DPA_LOG_WQE_BSIZE));
    NICC_CHECK_POINTER(mock_rx_ring);

    // seting data pointer within each WQE
    mock_rx_ring_ptr = mock_rx_ring;
    for (i=0; i<LOG2VALUE(log_depth); i++) {
        mlx5dv_set_data_seg(mock_rx_ring_ptr, DPA_LOG_WQ_DATA_ENTRY_BSIZE, wqd_mkey_id, data_daddr);
        mock_rx_ring_ptr++;
        data_daddr += DPA_LOG_WQ_DATA_ENTRY_BSIZE;
    }

    // copy RX WQEs from host to FlexIO RQ ring
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_host2dev_memcpy(process, mock_rx_ring, LOG2VALUE(log_depth + DPA_LOG_WQE_BSIZE), rq_ring_daddr)
    ))){
        NICC_WARN_C(
            "failed to copy initialized RQ to DPA heap memory: flexio_retval(%u), flexio_process(%p)",
            ret, process
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    if(likely(mock_rx_ring != nullptr)){
        free(mock_rx_ring);
    }
    return retval;
}


nicc_retval_t Channel_DPA::__allocate_dbr(struct flexio_process *process, 
                                          flexio_uintptr_t *dbr_daddr){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    __be32 dbr[2] = { 0, 0 };

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(dbr_daddr);

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(process, dbr, sizeof(dbr), dbr_daddr)
    ))){
        NICC_WARN_C("failed to copy DBR to device memory: flexio_process(%p), flexio_retval(%u)", process, ret);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    return retval;
}


nicc_retval_t Channel_DPA::__create_dpa_mkey(struct flexio_process *process, 
                                             struct ibv_pd *pd,
                                             flexio_uintptr_t daddr, 
                                             int log_bsize, 
                                             int access, 
                                             struct flexio_mkey **mkey){
    nicc_retval_t retval = NICC_SUCCESS;
    struct flexio_mkey_attr mkey_attr = {0};
    flexio_status ret;
    
    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(pd);
    NICC_ASSERT(daddr != static_cast<flexio_uintptr_t>(0x00));
    NICC_CHECK_POINTER(mkey);

    mkey_attr.pd = pd;
    mkey_attr.daddr = daddr;
    mkey_attr.len = LOG2VALUE(log_bsize);
    mkey_attr.access = access;
    
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_device_mkey_create(process, &mkey_attr, mkey))
    )){
        NICC_WARN_C(
            "failed to create mkey for memory region: flexio_process(%p), daddr(%lx)",
            process, daddr
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    return retval;
}

void Channel_DPA::__set_local_qp_info(QPInfo *qp_info, struct dpa_data_queues *dev_queues){
    NICC_ASSERT(qp_info->is_initialized == false);
    NICC_CHECK_POINTER(qp_info);
    NICC_CHECK_POINTER(dev_queues);
    qp_info->qp_num = dev_queues->qp_data.qp_num;
    qp_info->lid = this->_resolve.port_lid;
    for (size_t i = 0; i < 16; i++) {
        qp_info->gid[i] = this->_resolve.gid.raw[i];
    }
    qp_info->gid_table_index = this->_resolve.gid_index;
    qp_info->mtu = LOG2VALUE(DPA_LOG_WQ_DATA_ENTRY_BSIZE);
    strncpy(qp_info->hostname, "DPA", MAX_HOSTNAME_LEN - 1);
    qp_info->hostname[MAX_HOSTNAME_LEN - 1] = '\0';  // Ensure null termination
    memcpy(qp_info->nic_name, this->_resolve.ib_ctx->device->name, MAX_NIC_NAME_LEN);
    memcpy(qp_info->mac_addr, this->_resolve.mac_addr, 6);
    qp_info->is_initialized = true;
    std::cout << "Local QP Info:" << std::endl;
    qp_info->print();
}

nicc_retval_t Channel_DPA::__connect_qp_to_component_block(dpa_data_queues *dev_queues, 
                                                           const ComponentBlock *neighbour_component_block, 
                                                           const QPInfo *local_qp_info) {
    // nicc_retval_t retval = NICC_SUCCESS;
    /* ...... */
    return NICC_ERROR_NOT_IMPLEMENTED;
}

nicc_retval_t Channel_DPA::__connect_qp_to_host(dpa_data_queues *dev_queues, 
                                                struct flexio_queues_handler *flexio_queues_handler,
                                                const QPInfo *qp_info, 
                                                const QPInfo *local_qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    struct flexio_qp_attr_opt_param_mask qp_fattr_opt_param_mask;
    struct flexio_qp_attr qp_fattr;
    memset(&qp_fattr, 0, sizeof(struct flexio_qp_attr));
    memset(&qp_fattr_opt_param_mask, 0, sizeof(struct flexio_qp_attr_opt_param_mask));

    NICC_CHECK_POINTER(qp_info);
    if(unlikely(qp_info->is_initialized == false)){
        NICC_WARN_C("QP info is not initialized: qp_num(%u)", qp_info->qp_num);
        return NICC_ERROR_NOT_FOUND;
    }

    /// Print local and remote QPinfo
    // std::cout << "========== Local QP Info: ==========" << std::endl;
    // local_qp_info->print();
    // std::cout << "========== Remote QP Info: ==========" << std::endl;
    // qp_info->print();
    memcpy(this->_remote_mac_addr, qp_info->mac_addr, 6);
    memcpy(&this->_remote_gid, qp_info->gid, sizeof(union ibv_gid));
	qp_fattr.remote_qp_num     = qp_info->qp_num;
    qp_fattr.dest_mac          = this->_remote_mac_addr;
    qp_fattr.rgid_or_rip       = this->_remote_gid;
	qp_fattr.gid_table_index   = qp_info->gid_table_index;
	qp_fattr.rlid              = qp_info->lid;
	qp_fattr.fl                = 0;
	qp_fattr.min_rnr_nak_timer = 1;
    switch(LOG2VALUE(DPA_LOG_WQ_DATA_ENTRY_BSIZE)){
        case 1024:
            qp_fattr.path_mtu          = FLEXIO_QP_QPC_MTU_BYTES_1K;
            break;
        case 2048:
            qp_fattr.path_mtu          = FLEXIO_QP_QPC_MTU_BYTES_2K;
            break;
        case 4096:
            qp_fattr.path_mtu          = FLEXIO_QP_QPC_MTU_BYTES_4K;
            break;
        default:
            NICC_WARN_C("unsupported MTU size: %lu, only support 1024, 2048, 4096", LOG2VALUE(DPA_LOG_WQ_DATA_ENTRY_BSIZE));
            break;
    }
	qp_fattr.retry_count       = 0;
	qp_fattr.vhca_port_num     = 0x1;
	qp_fattr.udp_sport         = 0xc000;
	qp_fattr.grh               = 0x1;
    qp_fattr.qp_access_mask    = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
	qp_fattr.next_state = FLEXIO_QP_STATE_INIT;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_qp_modify(flexio_queues_handler->flexio_qp_ptr, &qp_fattr, &qp_fattr_opt_param_mask)))){
        NICC_WARN_C("failed to modify QP to INIT state: flexio_retval(%u)", ret);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    qp_fattr.next_state = FLEXIO_QP_STATE_RTR;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_qp_modify(flexio_queues_handler->flexio_qp_ptr, &qp_fattr, &qp_fattr_opt_param_mask)))){
        NICC_WARN_C("failed to modify QP to RTR state: flexio_retval(%u)", ret);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    qp_fattr.next_state = FLEXIO_QP_STATE_RTS;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_qp_modify(flexio_queues_handler->flexio_qp_ptr, &qp_fattr, &qp_fattr_opt_param_mask)))){
        NICC_WARN_C("failed to modify QP to RTS state: flexio_retval(%u)", ret);
        return NICC_ERROR_HARDWARE_FAILURE;
    }

    return retval;
}




/* ----------------------------- deallocate ----------------------------- */
nicc_retval_t Channel_DPA::__deallocate_sq_cq(struct flexio_process *flexio_process, struct dpa_data_queues *dev_queues, struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory CQ on flexio driver
    if(likely(flexio_queues_handler->flexio_sq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_cq_destroy(flexio_queues_handler->flexio_sq_cq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            return NICC_ERROR_HARDWARE_FAILURE;
        }
        flexio_queues_handler->flexio_sq_cq_ptr = nullptr;
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(flexio_process, &dev_queues->sq_cq_data)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    return retval;
}


nicc_retval_t Channel_DPA::__deallocate_rq_cq(struct flexio_process *flexio_process, struct dpa_data_queues *dev_queues, struct flexio_queues_handler *flexio_queues_handler){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory CQ on flexio driver
    if(likely(flexio_queues_handler->flexio_rq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_cq_destroy(flexio_queues_handler->flexio_rq_cq_ptr))
        )){
            NICC_WARN_C(
                "failed to destory flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        flexio_queues_handler->flexio_rq_cq_ptr = nullptr;
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(flexio_process, &dev_queues->rq_cq_data)
    ))){
        NICC_WARN_C(
            "failed to deallocate RQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

 exit:
    return retval;
}

nicc_retval_t Channel_DPA::__destroy_qp(struct flexio_process *flexio_process, 
                                       struct dpa_data_queues *dev_queues,
                                       struct flexio_queues_handler *flexio_queues_handler) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory SQ on flexio driver
    if(likely(flexio_queues_handler->flexio_sq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_sq_destroy(flexio_queues_handler->flexio_sq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        flexio_queues_handler->flexio_sq_ptr = nullptr;
    }

    // deallocate memory for SQ
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(flexio_process, &dev_queues->sq_data)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

    // destory RQ on flexio driver
    if(likely(flexio_queues_handler->flexio_rq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_rq_destroy(flexio_queues_handler->flexio_rq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        flexio_queues_handler->flexio_rq_ptr = nullptr;
    }
    // deallocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(flexio_process, &dev_queues->rq_data)
    ))){
        NICC_WARN_C(
            "failed to deallocate RQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

exit:
    return retval;
}


nicc_retval_t Channel_DPA::__deallocate_wq_memory(struct flexio_process *process, struct dpa_eth_wq *wq_transf){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(wq_transf);

    // deallocate data buffers
    if(likely(wq_transf->wqd_daddr != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_buf_dev_free(process, wq_transf->wqd_daddr))
        )) {
            NICC_WARN_C(
                "failed to deallocate WQ data buffer: flexio_process(%p), flexio_retval(%u)",
                process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        wq_transf->wqd_daddr = static_cast<flexio_uintptr_t>(0x00);
    }

    // deallocate descriptor (wqe) queue, these memories is used by flexio driver
    if(likely(wq_transf->wq_ring_daddr != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_buf_dev_free(process, wq_transf->wq_ring_daddr))
        )){
            NICC_WARN_C(
                "failed to deallocate WQ ring: flexio_process(%p), flexio_retval(%u)",
                process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        wq_transf->wq_ring_daddr = static_cast<flexio_uintptr_t>(0x00);
    }
    
    // deallocate doorbell record
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__deallocate_dbr(process, wq_transf->wq_dbr_daddr))
    )){
        NICC_WARN_C(
            "failed to deallocate doorbell record for WQ: "
            "flexio_process(%p), doca_retval(%u), ",
            process, retval
        );
        goto exit;
    }

exit:
    return retval;
}


nicc_retval_t Channel_DPA::__deallocate_cq_memory(struct flexio_process *process, struct dpa_cq *app_cq){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    
    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(app_cq);

    // deallocate doorbell record
    if(likely(app_cq->cq_dbr_daddr != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(NICC_SUCCESS != (
            retval = this->__deallocate_dbr(process, app_cq->cq_dbr_daddr)
        ))){
            NICC_WARN_C("failed to deallocate CQ DB record: flexio_process(%p), doca_retval(%u), ", process, retval);
            goto exit;
        }
        app_cq->cq_dbr_daddr = static_cast<flexio_uintptr_t>(0x00);
    }

    // deallocate FlexIO CQ ring
    if(likely(app_cq->cq_ring_daddr != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_buf_dev_free(process, app_cq->cq_ring_daddr))
        )){
            NICC_WARN_C(
                "failed to deallocate CQ ring: flexio_process(%p), flexio_retval(%u)",
                process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        app_cq->cq_ring_daddr = static_cast<flexio_uintptr_t>(0x00);
    }

 exit:
    return retval;
}


nicc_retval_t Channel_DPA::__deallocate_dbr(struct flexio_process *process, flexio_uintptr_t dbr_daddr){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(process);

    if(likely(dbr_daddr != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_buf_dev_free(process, dbr_daddr))
        )){
            NICC_WARN_C(
                "failed to deallocate DBR from device memory: flexio_process(%p), flexio_retval(%u)",
                process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }
    
exit:
    return retval;
}

}