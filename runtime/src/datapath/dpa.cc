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
    this->_state = reinterpret_cast<ComponentBaseState_t*>(dpa_state);
    this->_state->quota = this->_desp->quota;

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
    ComponentBlock_DPA *dpa_cb = nullptr;
    ComponentState_DPA_t *dpa_block_state;

    NICC_CHECK_POINTER(desp);
    NICC_CHECK_POINTER(cb);

    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_block<ComponentBlock_DPA>(desp, app_cxt, &dpa_cb)
    ))){
        NICC_WARN_C("failed to allocate block: retval(%u)", retval);
        goto exit;
    }
    NICC_CHECK_POINTER(*cb = dpa_cb);

    
    NICC_CHECK_POINTER(dpa_block_state = new ComponentState_DPA_t);
    dpa_cb->_state = reinterpret_cast<ComponentBaseState_t*>(dpa_block_state);
    ///!    \todo   transfer state between component and the created block

exit:
    return retval;
}


/*!
 *  \brief  return block of resource back to the component
 *  \param  app_cxt     app context which this block allocates to
 *  \param  cb          the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_DPA::deallocate_block(AppContext* app_cxt, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentBlock_DPA *dpa_cb = reinterpret_cast<ComponentBlock_DPA*>(cb);

    NICC_CHECK_POINTER(dpa_cb);

    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_block<ComponentBlock_DPA>(app_cxt, dpa_cb)
    ))){
        NICC_WARN_C("failed to deallocate block: retval(%u)", retval);
        goto exit;
    }

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
    ComponentFuncState_DPA_t *func_state; 

    NICC_CHECK_POINTER(app_func);
    if(unlikely(this->_function_state_map.count(app_func) > 0)){
        NICC_WARN_C("try to register duplicated functions, omited: app_func(%p)", app_func);
        retval = NICC_ERROR_DUPLICATED;
        goto exit;
    }

    // create and init function state on this component
    NICC_CHECK_POINTER(func_state = new ComponentFuncState_DPA_t());
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
        NICC_CHECK_POINTER(app_handler = app_func->handlers[i]);
        switch(app_handler->tid){
        case handler_typeid_t::Init:
            init_handler = app_handler;
            break;
        case handler_typeid_t::Event:
            event_handler = app_handler;
            break;
        default:
            NICC_ERROR_C_DETAIL("unregornized handler id for DPA, this is a bug: handler_id(%u)", app_handler->tid);
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

    // allocate resource for this event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_device_resources(app_func, func_state))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // init resources
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__init_device_resources(app_func, init_handler, func_state))
    )){
        NICC_WARN_C("failed to init reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // insert to function map
    this->_function_state_map.insert({ app_func, reinterpret_cast<ComponentFuncBaseState_t*>(func_state) });

 exit:
    // TODO: destory if failed
    return retval;
}


/*!
 *  \brief  deregister a application function
 *  \param  app_func the function to be deregistered from this compoennt
 *  \return NICC_SUCCESS for successful unregisteration
 */
nicc_retval_t ComponentBlock_DPA::unregister_app_function(AppFunction *app_func){
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentFuncState_DPA_t *func_state; 

    NICC_CHECK_POINTER(app_func);
    if(unlikely(this->_function_state_map.count(app_func) == 0)){
        NICC_WARN_C("try to unregister a unexist app function, omited: app_func(%p)", app_func);
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }

    // obtain function state
    func_state = reinterpret_cast<ComponentFuncState_DPA_t*>(this->_function_state_map[app_func]);
    NICC_CHECK_POINTER(func_state);

    // remove from state map
    this->_function_state_map.erase(app_func);

    // deallocate resource for event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__deallocate_device_resources(app_func, func_state))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // unregister event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__unregister_event_handler(func_state))
    )){
        NICC_WARN_C("failed to unregister event handler on DPA block: retval(%u)", retval);
        goto exit;
    }

    // delete the function state
    delete func_state;

exit:
    return retval;
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

    NICC_CHECK_POINTER(func_state);

    dpa_desp = reinterpret_cast<ComponentDesp_DPA_t*>(this->_desp);
    NICC_CHECK_POINTER(dpa_desp);

    func_state->ibv_ctx = utils_ibv_open_device(dpa_desp->device_name);
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
        if(func_state->uar != nullptr){
            mlx5dv_devx_free_uar(func_state->uar);
        }

        if(func_state->ibv_ctx != nullptr){
            ibv_close_device(func_state->ibv_ctx);
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
    struct flexio_event_handler_attr event_handler_attr = {0};

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(func_state);
    NICC_CHECK_POINTER(app_handler->binary.dpa_binary);
    NICC_CHECK_POINTER(app_handler->host_stub.dpa_host_stub);
    
    dpa_desp = reinterpret_cast<ComponentDesp_DPA_t*>(this->_desp);
    NICC_CHECK_POINTER(dpa_desp);
    dpa_state = reinterpret_cast<ComponentState_DPA_t*>(this->_state);
    NICC_CHECK_POINTER(dpa_state);

    // create flexio process
    if(unlikely(FLEXIO_STATUS_SUCCESS != 
        (res = flexio_process_create(func_state->ibv_ctx, app_handler->binary.dpa_binary, nullptr, &func_state->flexio_process))
    )){
        NICC_WARN_C("failed to create event handler on flexio driver on DPA block: flexio_retval(%d)", res);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // obtain uar from the created process
    func_state->flexio_uar = flexio_process_get_uar(func_state->flexio_process);
    if(unlikely(func_state->flexio_uar == nullptr)){
        NICC_WARN_C("no uar extracted");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // creat event handler
    event_handler_attr.host_stub_func = app_handler->host_stub.dpa_host_stub;
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
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t ComponentBlock_DPA::__allocate_device_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state);

    // allocate SQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_sq_cq(func_state)
    ))){
        NICC_WARN_C(
            "failed to allocate and init SQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // allocate RQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__allocate_rq_cq(func_state)
    ))){
        NICC_WARN_C(
            "failed to allocate and init RQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }
    
    // copy queue metadata to device
    func_state->dev_queues = (struct dpa_data_queues *)calloc(1, sizeof(struct dpa_data_queues));
    NICC_CHECK_POINTER(func_state->dev_queues);

    func_state->dev_queues->sq_cq_data = func_state->sq_cq_transf;
    func_state->dev_queues->sq_data = func_state->sq_transf;
    func_state->dev_queues->rq_cq_data = func_state->rq_cq_transf;
    func_state->dev_queues->rq_data = func_state->rq_transf;

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_copy_from_host(
            func_state->flexio_process, func_state->dev_queues,
            sizeof(struct dpa_data_queues), &func_state->d_dev_queues
        )
    ))){
        NICC_WARN_C(
            "failed to copy queue metadata to DPA heap memories: "
            "flexio_process(%p), flexio_retval(%u), ",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

 exit:

    // TODO: destory if failed

    return retval;
}


/*!
 *  \brief  init on-device resource for handlers running on DPA
 *  \note   this function is called within register_app_function
 *  \param  app_func        application function which the event handler comes from
 *  \param  app_handler     the init handler to be registered
 *  \param  func_state      state of the function on this DPA block
 *  \return NICC_SUCCESS for successful initialization
 */
nicc_retval_t ComponentBlock_DPA::__init_device_resources(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state){
    int ret = 0;
    uint64_t rpc_ret_val;
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(app_handler->host_stub.dpa_host_stub);
    NICC_CHECK_POINTER(func_state);

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_process_call(func_state->flexio_process, *(app_handler->host_stub.dpa_host_stub), &rpc_ret_val, func_state->d_dev_queues)
    ))){
        NICC_WARN_C(
            "failed to call init handler for initializing on-device resources: "
            "flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

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
    flexio_status ret;
    uint32_t cq_num;

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
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_wq_memory(
            func_state->flexio_process, DPA_LOG_SQ_RING_DEPTH, &func_state->sq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    
    // create SQ on flexio driver
    sq_attr.wq_ring_qmem.daddr = func_state->sq_transf.wq_ring_daddr;
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
    
    // create mkey for SQ data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ func_state->flexio_process,
            /* pd */ func_state->pd,
            /* daddr */ func_state->sq_transf.wqd_daddr,
            /* log_bsize */ DPA_LOG_SQ_RING_DEPTH + DPA_LOG_WQ_DATA_ENTRY_BSIZE,
            /* access */ IBV_ACCESS_LOCAL_WRITE,
            /* mkey */ &func_state->sqd_mkey
        )
    ))){
        NICC_WARN_C(
            "failed to create mkey for SQ data buffers: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    func_state->sq_transf.wqd_mkey_id = flexio_mkey_get_id(func_state->sqd_mkey);

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
    uint32_t cq_num;	    // CQ number
    uint32_t wq_num;	    // WQ number
    __be32 dbr[2] = { 0, 0 };

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

    // create CQ on flexio driver
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (ret = flexio_cq_create(func_state->flexio_process, NULL, &rqcq_attr, &func_state->flexio_rq_cq_ptr))
    )){
        NICC_WARN_C(
            "failed to create flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    cq_num = flexio_cq_get_cq_num(func_state->flexio_rq_cq_ptr);
    func_state->rq_cq_transf.cq_num = cq_num;
    func_state->rq_cq_transf.log_cq_depth = DPA_LOG_RQ_RING_DEPTH;
    
    // allocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_wq_memory(
            func_state->flexio_process, DPA_LOG_RQ_RING_DEPTH, &func_state->rq_transf
        )
    ))){
        NICC_WARN_C(
            "failed to allocate RQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }

    // create mkey for RQ data buffers
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_dpa_mkey(
            /* process */ func_state->flexio_process,
            /* pd */ func_state->pd,
            /* daddr */ func_state->rq_transf.wqd_daddr,
            /* log_bsize */ DPA_LOG_RQ_RING_DEPTH + DPA_LOG_WQ_DATA_ENTRY_BSIZE,
            /* access */ IBV_ACCESS_LOCAL_WRITE,
            /* mkey */ &func_state->rqd_mkey
        )
    ))){
        NICC_WARN_C(
            "failed to create mkey for RQ data buffers: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }
    func_state->rq_transf.wqd_mkey_id = flexio_mkey_get_id(func_state->sqd_mkey);

    // init WQEs on the RQ ring
    if(unlikely(NICC_SUCCESS != (
        retval = __init_rq_ring_wqes(
            /* process */ func_state->flexio_process,
            /* rq_ring_daddr */ func_state->rq_transf.wq_ring_daddr,
            /* log_depth */ DPA_LOG_RQ_RING_DEPTH,
            /* data_daddr */ func_state->rq_transf.wqd_daddr,
            /* wqd_mkey_id */ func_state->rq_transf.wqd_mkey_id
        )
    ))){
        NICC_WARN_C("failed to initialize WQEs within RQ ring");
        goto exit;
    }

    // create RQ on flexio driver
    rq_attr.wq_dbr_qmem.memtype = FLEXIO_MEMTYPE_DPA;
    rq_attr.wq_dbr_qmem.daddr = func_state->rq_transf.wq_dbr_daddr;
    rq_attr.wq_ring_qmem.daddr = func_state->rq_transf.wq_ring_daddr;
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_rq_create(func_state->flexio_process, NULL, cq_num, &rq_attr, &func_state->flexio_rq_ptr)
    ))){
        NICC_WARN_C(
            "failed to create flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    wq_num = flexio_rq_get_wq_num(func_state->flexio_rq_ptr);
    func_state->rq_transf.wq_num = wq_num;

    // modify RQ's DBR record to count for the number of WQEs
    dbr[0] = htobe32(LOG2VALUE(DPA_LOG_RQ_RING_DEPTH) & 0xffff);    // recv counter
    dbr[1] = htobe32(0 & 0xffff);                                   // send counter
    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_host2dev_memcpy(func_state->flexio_process, dbr, sizeof(dbr), func_state->rq_transf.wq_dbr_daddr)
    ))){
        NICC_WARN_C(
            "failed to modify DBR for RQ for counting all allocated slot: flexio_retval(%u), flexio_process(%p)",
            ret, func_state->flexio_process
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

 exit:

    // TODO: release DPA memories if failed

    return retval;
}


/*!
 *  \brief  allocate memory resource for SQ/RQ ring and data buffers
 *  \note   this function is called within __allocate_sq_cq/__allocate_rq_cq
 *  \param  process         flexIO process
 *  \param  log_depth       log2 of the SQ/RQ depth
 *  \param  wq_transf       SQ/RQ resource
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__allocate_wq_memory(
    struct flexio_process *process, int log_depth, struct dpa_wq *wq_transf
){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    // TODO: for debug, remove later
    NICC_ASSERT(DPA_LOG_WQE_BSIZE == 6);

    NICC_CHECK_POINTER(process);
    NICC_CHECK_POINTER(wq_transf);

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
        (ret = flexio_buf_dev_alloc(process, LOG2VALUE(log_depth + DPA_LOG_WQE_BSIZE), &wq_transf->wq_ring_daddr))
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


/*!
 *  \brief  initialize WQEs on RQ ring (after allocation)
 *  \note   this function is called within __allocate_rq_cq
 *  \param  process         flexIO process
 *  \param  rq_ring_daddr   base address of the allocated RQ ring
 *  \param  log_depth       log2 of the RQ ring depth
 *  \param  data_daddr      base address of the RQ data buffers
 *  \param  wqd_mkey_id     mkey id of the RQ's data buffers
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__init_rq_ring_wqes(
    struct flexio_process *process, flexio_uintptr_t rq_ring_daddr, 
    int log_depth, flexio_uintptr_t data_daddr, uint32_t wqd_mkey_id
){
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


/*!
 *  \brief  allocates doorbell record and return its address on the device's memory
 *  \note   this function is called within __allocate_cq_memory, __allocate_wq_memory and __allocate_rq_cq
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


/*!
 *  \brief  unregister event handler on DPA block
 *  \note   this function is called within unregister_app_function
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful unregistering
 */
nicc_retval_t ComponentBlock_DPA::__unregister_event_handler(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(func_state);

    if(likely(func_state->flexio_process != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_process_destroy(func_state->flexio_process) 
        ))){
            NICC_WARN_C("failed to destory process on DPA block: flexio_retval(%d)", ret);
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

exit:
    return retval;
}


/*!
 *  \brief  deallocate on-device resource for handlers running on DPA
 *  \note   this function is called within unregister_app_function
 *  \param  app_func    application function which the event handler comes from
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_device_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state);

    // deallocate SQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__deallocate_sq_cq(func_state)
    ))){
        NICC_WARN_C(
            "failed to deallocate and init SQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // deallocate RQ/CQ
    if(unlikely(NICC_SUCCESS !=(
        retval = this->__deallocate_rq_cq(func_state)
    ))){
        NICC_WARN_C(
            "failed to deallocate and init RQ and corresponding CQ/DBR: nicc_retval(%u)", retval
        );
        goto exit;
    }

    // free on-host queue metadata
    if(likely(func_state->dev_queues != nullptr)){
        free(func_state->dev_queues);
    }

    // free on-device queue metadata
    if(likely(func_state->d_dev_queues != static_cast<flexio_uintptr_t>(0x00))){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_buf_dev_free(func_state->flexio_process, func_state->d_dev_queues)
        ))){
            NICC_WARN_C(
                "failed to free on-device queue metadata: "
                "flexio_process(%p), flexio_retval(%u), ",
                func_state->flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

exit:
    return retval;
}


/*!
 *  \brief  deallocate SQ and corresponding CQ for DPA process
 *  \note   these functions are called within __deallocate_device_resources
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_sq_cq(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(func_state);

    // destory SQ on flexio driver
    if(likely(func_state->flexio_sq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_sq_destroy(func_state->flexio_sq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                func_state->flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

    // destory CQ on flexio driver
    if(likely(func_state->flexio_sq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_cq_destroy(func_state->flexio_sq_cq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio SQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                func_state->flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(func_state->flexio_process, &func_state->sq_cq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }

    // deallocate memory for SQ
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(func_state->flexio_process, &func_state->sq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate SQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }

exit:
    return retval;
}


/*!
 *  \brief  deallocate RQ and corresponding CQ for DPA process
 *  \note   these functions are called within __deallocate_device_resources
 *  \param  func_state  state of the function on this DPA block
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_rq_cq(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(func_state);

    // destory RQ on flexio driver
    if(likely(func_state->flexio_rq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_rq_destroy(func_state->flexio_rq_ptr)
        ))){
            NICC_WARN_C(
                "failed to destory flexio RQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                func_state->flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        func_state->flexio_rq_ptr = nullptr;
    }

    // destory CQ on flexio driver
    if(likely(func_state->flexio_rq_cq_ptr != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS !=
            (ret = flexio_cq_destroy(func_state->flexio_rq_cq_ptr))
        )){
            NICC_WARN_C(
                "failed to destory flexio RQ's CQ on flexio driver: flexio_process(%p), flexio_retval(%u)",
                func_state->flexio_process, ret
            );
            retval = NICC_ERROR_HARDWARE_FAILURE;
            goto exit;
        }
        func_state->flexio_rq_cq_ptr = nullptr;
    }

    // deallocate CQ & doorbell on DPA heap memory
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_cq_memory(func_state->flexio_process, &func_state->rq_cq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate RQ's CQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }

    // deallocate RQ memories (data buffers and the ring)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_wq_memory(func_state->flexio_process, &func_state->rq_transf)
    ))){
        NICC_WARN_C(
            "failed to deallocate RQ and doorbell on DPA heap memory: flexio_process(%p), retval(%u)",
            func_state->flexio_process, retval
        );
        goto exit;
    }

 exit:
    return retval;
}


/*!
 *  \brief  deallocate memory resource for SQ/RQ
 *  \note   this function is called within __deallocate_sq_cq / __deallocate_rq_cq
 *  \param  process         flexIO process
 *  \param  sq_transf       created SQ/RQ resource
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_wq_memory(struct flexio_process *process, struct dpa_wq *wq_transf){
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


/*!
 *  \brief  deallocate memory resource for SQ/CQ
 *  \note   this function is called within __deallocate_sq_cq / __deallocate_rq_cq
 *  \param  process   flexIO process
 *  \param  app_cq    CQ resource
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_cq_memory(struct flexio_process *process, struct dpa_cq *app_cq){
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


/*!
 *  \brief  deallocates doorbell record and return its address on the device's memory
 *  \note   this function is called within __deallocate_cq_memory, __deallocate_sq_memory and __deallocate_rq_cq
 *  \param  process   flexIO process
 *  \param  dbr_daddr doorbell record address on the device's memory
 *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
 */
nicc_retval_t ComponentBlock_DPA::__deallocate_dbr(struct flexio_process *process, flexio_uintptr_t dbr_daddr){
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


} // namespace nicc
