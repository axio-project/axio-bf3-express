#include "datapath/channel_impl/dpa_channel.h"

namespace nicc {

nicc_retval_t Channel_DPA::allocate_channel(struct ibv_pd *pd, 
                                            struct mlx5dv_devx_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct flexio_event_handler	*event_handler,
                                            struct ibv_context *ibv_ctx) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    NICC_CHECK_POINTER(pd);
    NICC_CHECK_POINTER(uar);
    NICC_CHECK_POINTER(flexio_process);
    NICC_CHECK_POINTER(event_handler);
    NICC_CHECK_POINTER(ibv_ctx);
    
    // allocate SQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_sq_cq(pd, uar, flexio_process, ibv_ctx)
    ))){
        NICC_WARN_C(
            "failed to allocate and init SQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // allocate RQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_rq_cq(pd, uar, flexio_process, event_handler, ibv_ctx)
    ))){
        NICC_WARN_C(
            "failed to allocate and init RQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // create Queue Pair
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_qp(pd, uar, flexio_process)
    ))){
        NICC_WARN_C("failed to create queue pair: nicc_retval(%u)", retval);
        goto exit;
    }

    // copy queue metadata to device
    this->_dev_queues = (struct dpa_data_queues *)calloc(1, sizeof(struct dpa_data_queues));
    NICC_CHECK_POINTER(this->_dev_queues);

    this->_dev_queues->sq_cq_data    = this->_sq_cq_transf;
    this->_dev_queues->sq_data       = this->_sq_transf;
    this->_dev_queues->rq_cq_data    = this->_rq_cq_transf;
    this->_dev_queues->rq_data       = this->_rq_transf;

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(
            flexio_process, this->_dev_queues,
            sizeof(struct dpa_data_queues), &this->d_dev_queues
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

    // TODO: destory if failed

    return retval;
}

nicc_retval_t Channel_DPA::deallocate_channel(struct flexio_process *flexio_process) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    NICC_CHECK_POINTER(flexio_process);

    // deallocate SQ CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__deallocate_sq_cq(flexio_process)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate RQ CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__deallocate_rq_cq(flexio_process)
    ))){
        NICC_WARN_C(
            "failed to deallocate RQ CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate QP
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__destroy_qp(flexio_process)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ/RQ: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // free on-host queue metadata
    if(likely(this->_dev_queues != nullptr)){
        free(this->_dev_queues);
    }

    // free on-device queue metadata
    if(likely(this->d_dev_queues != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_buf_dev_free(flexio_process, this->d_dev_queues)
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


nicc_retval_t Channel_DPA::__allocate_sq_cq(struct ibv_pd *pd, 
                                            struct mlx5dv_devx_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct ibv_context *ibv_ctx){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    uint32_t cq_num;

    // attributes of CQ
    struct flexio_cq_attr sqcq_attr = {
      .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
      // SQ does not need APU CQ
      .element_type = FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ,   
      .uar_id = uar->page_id
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            flexio_process, DPA_LOG_CQ_RING_DEPTH, &(this->_sq_cq_transf)
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    sqcq_attr.cq_dbr_daddr = this->_sq_cq_transf.cq_dbr_daddr;
    sqcq_attr.cq_ring_qmem.daddr = this->_sq_cq_transf.cq_ring_daddr;

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_cq_create(flexio_process, ibv_ctx, &sqcq_attr, &this->_flexio_sq_cq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    cq_num = flexio_cq_get_cq_num(this->_flexio_sq_cq_ptr);
    this->_sq_cq_transf.cq_num = cq_num;
    this->_sq_cq_transf.log_cq_depth = DPA_LOG_CQ_RING_DEPTH;

 exit:

    // TODO: if unsuccessful, free allocated memories
    
    return retval;
}


nicc_retval_t Channel_DPA::__allocate_rq_cq(struct ibv_pd *pd, 
                                            struct mlx5dv_devx_uar *uar, 
                                            struct flexio_process *flexio_process,
                                            struct flexio_event_handler	*event_handler,
                                            struct ibv_context *ibv_ctx){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    uint32_t cq_num;	    // CQ number

    // RQ's CQ attributes
    struct flexio_cq_attr rqcq_attr = {
        .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
        .element_type = FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD,
        .thread = flexio_event_handler_get_thread(event_handler),
        .uar_id = uar->page_id,
        .uar_base_addr = uar->base_addr
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            flexio_process, DPA_LOG_CQ_RING_DEPTH, &this->_rq_cq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate RQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    rqcq_attr.cq_dbr_daddr = this->_rq_cq_transf.cq_dbr_daddr;
    rqcq_attr.cq_ring_qmem.daddr = this->_rq_cq_transf.cq_ring_daddr;

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_cq_create(flexio_process, NULL, &rqcq_attr, &this->_flexio_rq_cq_ptr))
    )){
        NICC_WARN_C(
            "failed to create flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    cq_num = flexio_cq_get_cq_num(this->_flexio_rq_cq_ptr);
    this->_rq_cq_transf.cq_num = cq_num;
    this->_rq_cq_transf.log_cq_depth = DPA_LOG_RQ_RING_DEPTH;

 exit:
    // TODO: release DPA memories if failed

    return retval;
}

nicc_retval_t Channel_DPA::__create_qp(struct ibv_pd *pd,
                                       struct mlx5dv_devx_uar *uar,
                                       struct flexio_process *flexio_process){
    nicc_retval_t retval = NICC_SUCCESS;

    switch(this->_typeid){
        case Channel::channel_typeid_t::RDMA:
            // TODO: create RDMA QP
            // retval = this->__create_rdma_qp();
            retval = NICC_ERROR_NOT_IMPLEMENTED;
            break;
        case Channel::channel_typeid_t::ETHERNET:
            retval = this->__create_ethernet_qp(pd, uar, flexio_process);
            break;
        default:
            retval = NICC_ERROR_NOT_FOUND;
            break;
    }
    return retval;
}

nicc_retval_t Channel_DPA::__create_ethernet_qp(struct ibv_pd *pd,
                                                struct mlx5dv_devx_uar *uar,
                                                struct flexio_process *flexio_process){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    __be32 dbr[2] = { 0, 0 };
    // attributes of SQ
    struct flexio_wq_attr sq_attr = {
      .log_wq_depth = DPA_LOG_SQ_RING_DEPTH,
      .uar_id = uar->page_id,
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
            flexio_process, DPA_LOG_SQ_RING_DEPTH, 64, &this->_sq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }
    // create SQ on flexio driver
    sq_attr.wq_ring_qmem.daddr = this->_sq_transf.wq_ring_daddr;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_sq_create(flexio_process, NULL, this->_sq_cq_transf.cq_num, &sq_attr, &this->_flexio_sq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%d)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    this->_sq_transf.wq_num = flexio_sq_get_wq_num(this->_flexio_sq_ptr);
    
    // create mkey for SQ data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ flexio_process,
            /* pd */ pd,
            /* daddr */ this->_sq_transf.wqd_daddr,
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

    this->_sq_transf.wqd_mkey_id = flexio_mkey_get_id(this->_sqd_mkey);

    // ------------------------- RQ -------------------------
    // allocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_wq_memory(
            flexio_process, DPA_LOG_RQ_RING_DEPTH, sizeof(struct mlx5_wqe_data_seg), &this->_rq_transf
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
            /* daddr */ this->_rq_transf.wqd_daddr,
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
    this->_rq_transf.wqd_mkey_id = flexio_mkey_get_id(this->_rqd_mkey);

    // init WQEs on the RQ ring
    if(unlikely(NICC_SUCCESS != (
        retval = __init_rq_ring_wqes(
            /* process */ flexio_process,
            /* rq_ring_daddr */ this->_rq_transf.wq_ring_daddr,
            /* log_depth */ DPA_LOG_RQ_RING_DEPTH,
            /* data_daddr */ this->_rq_transf.wqd_daddr,
            /* wqd_mkey_id */ this->_rq_transf.wqd_mkey_id
        )
    ))){
        NICC_WARN_C("failed to initialize WQEs within RQ ring");
        goto exit;
    }

    // create RQ on flexio driver
    rq_attr.wq_dbr_qmem.memtype = FLEXIO_MEMTYPE_DPA;
    rq_attr.wq_dbr_qmem.daddr = this->_rq_transf.wq_dbr_daddr;
    rq_attr.wq_ring_qmem.daddr = this->_rq_transf.wq_ring_daddr;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_rq_create(flexio_process, NULL, this->_rq_cq_transf.cq_num, &rq_attr, &this->_flexio_rq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    this->_rq_transf.wq_num = flexio_rq_get_wq_num(this->_flexio_rq_ptr);

    // modify RQ's DBR record to count for the number of WQEs
    dbr[0] = htobe32(LOG2VALUE(DPA_LOG_RQ_RING_DEPTH) & 0xffff);    // recv counter
    dbr[1] = htobe32(0 & 0xffff);                                   // send counter
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_host2dev_memcpy(flexio_process, dbr, sizeof(dbr), this->_rq_transf.wq_dbr_daddr)
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

nicc_retval_t Channel_DPA::__allocate_wq_memory(struct flexio_process *process, 
                                                int log_depth, 
                                                uint64_t wqe_size, 
                                                struct dpa_wq *wq_transf){
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
        goto exit;
    }

    // allocate temp CQ at DRAM
    num_of_cqes = LOG2VALUE(log_depth);
    ring_bsize = num_of_cqes * LOG2VALUE(log_cqe_bsize);
    cq_ring_src = (struct mlx5_cqe64*)calloc(num_of_cqes, LOG2VALUE(log_cqe_bsize));
    if(unlikely(cq_ring_src == nullptr)){
        NICC_WARN_C("failed to allocate temp CQ ring on DRAM, not enough memory: flexio_process(%p)", process);
        retval = NICC_ERROR_EXSAUSTED;
        goto exit;
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
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
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
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    return retval;
}

nicc_retval_t Channel_DPA::__deallocate_sq_cq(struct flexio_process *flexio_process){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory CQ on flexio driver
    if(likely(this->_flexio_sq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_cq_destroy(this->_flexio_sq_cq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(flexio_process, &this->_sq_cq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

exit:
    return retval;
}


nicc_retval_t Channel_DPA::__deallocate_rq_cq(struct flexio_process *flexio_process){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory CQ on flexio driver
    if(likely(this->_flexio_rq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_cq_destroy(this->_flexio_rq_cq_ptr))
        )){
            NICC_WARN_C(
                "failed to destory flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        this->_flexio_rq_cq_ptr = nullptr;
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(flexio_process, &this->_rq_cq_transf)
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

nicc_retval_t Channel_DPA::__destroy_qp(struct flexio_process *flexio_process) {
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // destory SQ on flexio driver
    if(likely(this->_flexio_sq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_sq_destroy(this->_flexio_sq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

    // deallocate memory for SQ
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(flexio_process, &this->_sq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            flexio_process, retval
        );
        goto exit;
    }

    // destory RQ on flexio driver
    if(likely(this->_flexio_rq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_rq_destroy(this->_flexio_rq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        this->_flexio_rq_ptr = nullptr;
    }
    // deallocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(flexio_process, &this->_rq_transf)
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


nicc_retval_t Channel_DPA::__deallocate_wq_memory(struct flexio_process *process, struct dpa_wq *wq_transf){
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