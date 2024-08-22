#include "datapath/dpa.h"

namespace nicc {

/*!
 *  \brief  initialization of the components
 *  \param  desp    descriptor to initialize the component
 *  \return NICC_SUCCESS for successful initialization
 */
nicc_retval_t Component_DPA::init(ComponentBaseDesp_t* desp) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentState_DPA_t *dpa_state;

    // bind descriptor
    NICC_CHECK_POINTER(this->_desp = desp);

    // allocate and bind state
    NICC_CHECK_POINTER(dpa_state = new ComponentState_DPA_t);
    this->_state = reinterpret_cast<ComponentState_t*>(dpa_state);

    if(unlikely(
        NICC_SUCCESS != (retval = this->__setup_ibv_device())
    )){
        NICC_WARN_C("failed to setup ibv device for DPA");
        goto exit;
    }

exit:
    return retval;
}


/*!
 *  \brief  apply block of resource from the component
 *  \param  desp    descriptor for allocation
 *  \param  app_cxt app context which this block allocates to
 *  \param  cb      the handle of the allocated block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t Component_DPA::allocate_block(ComponentBaseDesp_t* desp, AppContext* app_cxt, ComponentBlock** cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    retval = this->__allocate_block<ComponentBlock_DPA>(desp, app_cxt, cb);

    // TODO: transfer any desp/state between component and its block

exit:
    return retval;
}


/*!
 *  \brief  return block of resource back to the component
 *  \param  cb      the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_DPA::deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;

    retval = this->__deallocate_block<ComponentBlock_DPA>(cb);

exit:
    return retval;
}


/*!
 *  \brief  register a new application function into this component
 *  \param  app_func    the function to be registered into this component
 *  \return NICC_SUCCESS for successful registeration
 */
nicc_retval_t ComponentBlock_DPA::register_app_function(AppFunction *app_func){
    nicc_retval_t retval = NICC_SUCCESS;
    uint8_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    flexio_func_t *cast_handler;
    ComponentFuncState_DPA_t *func_state; 

    // create and init function state on this component
    NICC_CHECK_POINTER(func_state = new ComponentFuncState_DPA_t);
    if(unlikely(
        NICC_SUCCESS != (retval = this->__setup_ibv_device(func_state))
    )){
        NICC_WARN_C("failed to setu ibv device for newly applocated function, abort");
        goto exit;
    }
    
    if(unlikely(app_func->handlers.size() == 0)){
        NICC_WARN_C("no handlers included in app_func context, nothing registered");
        goto exit;
    }

    for(i=0; i<app_func->handlers.size(); i++){
        NICC_CHECK_POINTER(app_handler = handlers[i]);

        swicth(app_handler->tid){
        case Init:
            init_handler = app_handler;
            break;
        case Event:
            event_handler = app_handler;
            break;
        default:
            NICC_ERROR_C_DETIAL("unregornized handler id for DPA, this is a bug: handler_id(%u)", handler_type);
        }
    }

    // check handlers
    if(unlikely(init_handler == nullptr)){
        NICC_WARN_C("no init handlers included in app_func context, abort registering");
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }
    if(unlikely(event_handler == nullptr)){
        NICC_WARN_C("no event handlers included in app_func context, abort registering");
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }

    // register event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__register_event_handler(app_func, event_handler, func_state))
    )){
        NICC_WARN_C("failed to register handler on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }





 exit:
    return retval;
}


/*!
 *  \brief  deregister a application function
 *  \param  func the function to be deregistered from this compoennt
 *  \return NICC_SUCCESS for successful unregisteration
 */
nicc_retval_t ComponentBlock_DPA::unregister_app_function(AppFunction *func){

}


/*!
 *  \brief  setup mlnx_device for this DPA block
 *  \param  func_state  state of the function which intend to init new rdma revice
 *  \return NICC_SUCCESS on success;
 *          NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__setup_ibv_device(ComponentFuncState_DPA_t *func_state) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentDesp_DPA_t *dpa_desp;
    ComponentState_DPA_t *dpa_state;

    NICC_CHECK_POINTER(func_state);

    dpa_desp = reinterpret_cast<ComponentDesp_DPA_t*>(this->_desp);
    NICC_CHECK_POINTER(dpa_desp);
    dpa_state = reinterpret_cast<ComponentState_DPA_t*>(this->_state);
    NICC_CHECK_POINTER(dpa_state);

    func_state->ibv_ctx = nicc_utils_ibv_open_device(dpa_desp->device_name);
    if (unlikely(func_state->ibv_ctx == nullptr)) {
        NICC_WARN_C("failed open ibv device %s", dpa_desp->device_name);
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }

    func_state->uar = mlx5dv_devx_alloc_uar(func_state->ibv_ctx, MLX5DV_UAR_ALLOC_TYPE_NC);
    if (unlikely(func_state->uar == nullptr)) {
        NICC_WARN_C("failed to allocate UAR on device %s", dpa_desp->device_name);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    func_state->pd = ibv_alloc_pd(func_state->ibv_ctx);
    if (unlikely(func_state->pd == nullptr)) {
        NICC_WARN_C("failed to allocate UAR");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(this->_config->uar != nullptr){
            mlx5dv_devx_free_uar(this->_config->uar);
        }

        if(this->_config->ibv_ctx != nullptr){
            ibv_close_device(this->_config->ibv_ctx);
        }
    }
    
    return retval;
}


/*!
 *  \brief  register event handler on DPA block
 *  \param  app_func        application function which the event handler comes from
 *  \param  app_handler     the handler to be registered
 *  \param  func_state      state of the function on this DPA block
 *  \return NICC_SUCCESS for successful registering
 */
nicc_retval_t ComponentBlock_DPA::__register_event_handler(
    AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state
){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status res;
    ComponentDesp_DPA_t *dpa_desp;
    ComponentState_DPA_t *dpa_state;
    struct flexio_process_attr process_attr = {0};
    struct flexio_event_handler_attr event_handler_attr = {0};

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(app_handler->binary);
    NICC_CHECK_POINTER(func_state);

    dpa_desp = reinterpret_cast<ComponentDesp_DPA_t*>(this->_desp);
    NICC_CHECK_POINTER(dpa_desp);
    dpa_state = reinterpret_cast<ComponentState_DPA_t*>(this->_state);
    NICC_CHECK_POINTER(dpa_state);

    // create flexio process
    process_attr.uar = func_state->uar;
    if(unlikely(FLEXIO_STATUS_SUCCESS != 
        (res = flexio_process_create(func_state->ibv_ctx, app_handler->binary, &process_attr, &func_state->flexio_process))
    )){
        NICC_WARN_C("failed to create event handler on flexio driver on DPA block: flexio_retval(%d)", res);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // create flexio uar
    if(unlikely(FLEXIO_STATUS_SUCCESS != 
        (res = flexio_uar_create(func_state->flexio_process, func_state->uar, &func_state->flexio_uar))
    )){
        NICC_WARN_C("failed to create flexio uar on flexio driver on DPA block: flexio_retval(%d)", res);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // creat event handler
    event_handler_attr.host_stub_func = *(app_handler->host_stub);
    event_handler_attr.affinity.type = FLEXIO_AFFINITY_STRICT;
    event_handler_attr.affinity.id = dpa_desp->core_id;
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (res = flexio_event_handler_create(func_state->flexio_process, &event_handler_attr, &func_state->event_handler))
    )){
        NICC_WARN_C("failed to create flexio event handler on flexio driver on DPA block: flexio_retval(%d)", res);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

 exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(func_state->uar != nullptr){
            flexio_uar_destroy(func_state->uar);
        }
        if(func_state->flexio_process != nullptr){
            flexio_process_destroy(func_state->flexio_process);
        }
    }

    return retval;
}


/*!
 *  \brief  allocate on-device resource for DPA process
 *  \note   this function is called within register_app_function
 *  \param  app_func    application function which the event handler comes from
 *  \param  handler     the event handler to be registered
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t ComponentBlock_DPA::__allocate_device_resources(
    AppFunction *app_func, flexio_func_t *handler, ComponentFuncState_DPA_t *func_state
){
    nicc_retval_t retval = NICC_SUCCESS;
    doca_error_t result;
    flexio_status ret;
   
    

 exit:
    return retval;
}


/*!
 *  \brief  allocate SQ and corresponding CQ for DPA process
 *  \note   this function are called within __allocate_device_resources
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t ComponentBlock_DPA::__allocate_sq_cq(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    doca_error_t result;
    flexio_status ret;
    uint32_t cq_num;
    uint32_t log_sqd_bsize;

    NICC_CHECK_POINTER(func_state);

    // attributes of CQ
    struct flexio_cq_attr sqcq_attr = {
      .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
      // SQ does not need APU CQ
      .element_type = FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ,
      .uar_id = func_state->uar->page_id
    };

    // attributes of SQ
    struct flexio_wq_attr sq_attr = {
      .log_wq_depth = DPA_LOG_SQ_RING_DEPTH,
      .uar_id = func_state->uar->page_id,
      .pd = func_state->pd
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            func_state->flexio_process, DPA_LOG_CQ_RING_DEPTH, &func_state->sq_cq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    sqcq_attr.cq_dbr_daddr = func_state->sq_cq_transf.cq_dbr_daddr;
    sqcq_attr.cq_ring_qmem.daddr = func_state->sq_cq_transf.cq_ring_daddr;

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_cq_create(func_state->flexio_process, func_state->ibv_ctx, &sqcq_attr, &func_state->flexio_sq_cq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    cq_num = flexio_cq_get_cq_num(func_state->flexio_sq_cq_ptr);
    func_state->sq_cq_transf.cq_num = cq_num;
    func_state->sq_cq_transf.log_cq_depth = DPA_LOG_CQ_RING_DEPTH;

    // allocate memory for SQ
    log_sqd_bsize = DPA_LOG_WQ_DATA_ENTRY_BSIZE + DPA_LOG_SQ_RING_DEPTH;
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_sq_memory(
            func_state->flexio_process, DPA_LOG_SQ_RING_DEPTH, log_sqd_bsize, &func_state->sq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    sq_attr.wq_ring_qmem.daddr = func_state->sq_transf.wq_ring_daddr;

    // create SQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_sq_create(func_state->flexio_process, NULL, cq_num, &sq_attr, &func_state->flexio_sq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    func_state->sq_transf.wq_num = flexio_sq_get_wq_num(func_state->flexio_sq_ptr);
    
    // create SQ TX mkey
    if(unlikely(NICC_SUCCESS != (
        result = this->__create_dpa_mkey(
            func_state->flexio_process, func_state->pd, func_state->sq_transf.wqd_daddr,
            log_sqd_bsize, IBV_ACCESS_LOCAL_WRITE, &func_state->sqd_mkey
        )
    ))){
        NICC_WARN_C(
            "failed to create SQ TX mkey: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    dpa_config_->sq_transf.wqd_mkey_id = flexio_mkey_get_id(dpa_config_->sqd_mkey);

 exit:

    // TODO: if unsuccessful, free allocated memories
    
    return retval;
}


/*!
 *  \brief  allocate RQ and corresponding CQ for DPA process
 *  \note   this function are called within __allocate_device_resources
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t ComponentBlock_DPA::__allocate_rq_cq(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    uint32_t mkey_id;
    uint32_t cq_num;	    // CQ number
    uint32_t wq_num;	    // WQ number
    uint32_t log_rqd_bsize;	// SQ data buffer size

    // RQ's CQ attributes
    struct flexio_cq_attr rqcq_attr = {
        .log_cq_depth = DPA_LOG_CQ_RING_DEPTH,
        .element_type = FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD,
        .thread = flexio_event_handler_get_thread(func_state->event_handler),
        .uar_id = func_state->uar->page_id,
        .uar_base_addr = func_state->uar->base_addr
    };

    // RQ attributes
    struct flexio_wq_attr rq_attr = {
        .log_wq_depth = DPA_LOG_RQ_RING_DEPTH,
        .pd = func_state->pd
    };

    // allocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_cq_memory(
            func_state->flexio_process, DPA_LOG_CQ_RING_DEPTH, &func_state->rq_cq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate RQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    rqcq_attr.cq_dbr_daddr = func_state->rq_cq_transf.cq_dbr_daddr;
    rqcq_attr.cq_ring_qmem.daddr = func_state->rq_cq_transf.cq_ring_daddr;

 exit:
    return retval;
}


/*!
 *  \brief  allocate memory resource for SQ
 *  \note   this function is called within __allocate_sq_cq
 *  \param  process         flexIO process
 *  \param  log_depth       log2 of the SQ depth
 *  \param  log_data_bsize  log2 of the SQ data buffer size
 *  \param  sq_transf       SQ resource
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__allocate_sq_memory(
    struct flexio_process *process, int log_depth, int log_data_bsize, struct dpa_wq *sq_transf
){
    nicc_retval_t retval = NICC_SUCCESS;
    const int log_wqe_bsize = 6; /* WQE size is 64 bytes */

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(sq_transf);

    sq_transf->wqd_daddr = nullptr;
    sq_transf->wq_ring_daddr = nullptr;

    // allocate data buffer queue, these memories is used by dpa kernel
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_buf_dev_alloc(process, LOG2VALUE(log_data_bsize), &sq_transf->wqd_daddr))
    )) {
        NICC_WARN_C(
            "failed to allocate SQ data buffer: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // allocate descriptor (wqe) queue, these memories is used by flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_buf_dev_alloc(process, LOG2VALUE(log_depth + log_wqe_bsize), &sq_transf->wq_ring_daddr))
    )){
        NICC_WARN_C(
            "failed to allocate SQ ring: flexio_process(%p), flexio_retval(%u)",
            process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // allocate doorbell record
    if(unlikely(NICC_SUCCESS !=
        (result = this->__allocate_dbr(process, &sq_transf->wq_dbr_daddr))
    )){
        NICC_WARN_C("failed to allocate doorbell record for SQ: flexio_process(%p), doca_retval(%u), ", process, result);
        goto exit;
    }
    
exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(sq_transf->wqd_daddr != nullptr){
            ret = flexio_buf_dev_free(process, sq_transf->wqd_daddr);
            NICC_ASSERT(ret == FLEXIO_STATUS_SUCCESS);
        }

        if(sq_transf->wq_ring_daddr != nullptr){
            ret = flexio_buf_dev_free(process, sq_transf->wq_ring_daddr);
            NICC_ASSERT(ret == FLEXIO_STATUS_SUCCESS);
        }
    }

    return retval;
}


/*!
 *  \brief  allocate memory resource for CQ
 *  \note   this function is called within __allocate_rq_cq
 *  \param  process   flexIO process
 *  \param  log_depth log2 of the CQ depth
 *  \param  app_cq    CQ resource
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__allocate_cq_memory(struct flexio_process *process, int log_depth, struct dpa_cq *app_cq){
    nicc_retval_t retval = NICC_SUCCESS;
    struct mlx5_cqe64 *cq_ring_src = nullptr, *cqe = nullptr;
    size_t ring_bsize;
    int i, num_of_cqes;
    const int log_cqe_bsize = 6; // CQE size is 64 bytes
    doca_error_t result = DOCA_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(dbr_daddr);

    // allocate doorbell record
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_dbr(process, &app_cq->cq_dbr_daddr);)
    )){
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


/*!
 *  \brief  allocates doorbell record and return its address on the device's memory
 *  \note   this function is called within __allocate_cq_memory, __allocate_sq_memory and __allocate_rq_cq
 *  \param  process   flexIO process
 *  \param  dbr_daddr doorbell record address on the device's memory
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__allocate_dbr(struct flexio_process *process, flexio_uintptr_t *dbr_daddr){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;
    __be32 dbr[2] = { 0, 0 };

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(dbr_daddr);

    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_copy_from_host(process, dbr, sizeof(dbr), dbr_daddr))
    )){
        NICC_WARN_C(
            "failed to copy DBR to device memory: flexio_process(%p), flexio_retval(%u)",
            flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    return retval;
}

/*!
 *  \brief  create mkey for the given memory region
 *  \param  process     flexIO process
 *  \param  pd          protection domain
 *  \param  daddr       device address of the memory region
 *  \param  log_bsize   log2 of the memory region size
 *  \param  access      access flags
 *  \param  mkey        mkey
 *  \return NICC_SUCCESS on success
 */
nicc_retval_t ComponentBlock_DPA::__create_dpa_mkey(
    struct flexio_process *process, struct ibv_pd *pd,
    flexio_uintptr_t daddr, int log_bsize, int access, struct flexio_mkey **mkey
){
    nicc_retval_t retval = NICC_SUCCESS;
    struct flexio_mkey_attr mkey_attr = {0};
    flexio_status ret;
    mkey_attr.pd = pd;
    mkey_attr.daddr = daddr;
    mkey_attr.len = LOG2VALUE(log_bsize);
    mkey_attr.access = access;

    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_device_mkey_create(process, &mkey_attr, mkey))
    )){
        NICC_WARN_C(
            "failed to create mkey for memory region: flexio_process(%p), daddr(%p)",
            process, daddr
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    return retval;
}


} // namespace nicc
