#include "datapath/block_impl/dpa.h"

namespace nicc {


nicc_retval_t ComponentBlock_DPA::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint8_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    ComponentFuncState_DPA_t *func_state; 

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state = this->_function_state);

    // TODO: this is ugly, remove ibv_ctx from func_state!
    NICC_CHECK_POINTER(func_state->ibv_ctx = device_state.ibv_ctx);
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
        (retval = this->__allocate_wrapper_resources(app_func, func_state))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // init resources
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__init_wrapper_resources(app_func, init_handler, func_state))
    )){
        NICC_WARN_C("failed to init reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // insert to function map
    this->_function_state = func_state;

 exit:
    // TODO: destory if failed
    return retval;
}


nicc_retval_t ComponentBlock_DPA::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;

    // obtain function state
    NICC_CHECK_POINTER( this->_function_state );

    // deallocate resource for event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__deallocate_wrapper_resources( 
                        this->_function_state ))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // unregister event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__unregister_event_handler( 
                        this->_function_state ))
    )){
        NICC_WARN_C("failed to unregister event handler on DPA block: retval(%u)", retval);
        goto exit;
    }

    // delete the function state
    delete this->_function_state;

exit:
    return retval;
}


nicc_retval_t ComponentBlock_DPA::__setup_ibv_device(ComponentFuncState_DPA_t *func_state) {
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(func_state);

    func_state->uar = mlx5dv_devx_alloc_uar(func_state->ibv_ctx, MLX5DV_UAR_ALLOC_TYPE_NC);
    if (unlikely(func_state->uar == nullptr)) {
        NICC_WARN_C("failed to allocate UAR on device %s", this->_desp->device_name);
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


nicc_retval_t ComponentBlock_DPA::__register_event_handler(
    AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state
){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status res;
    struct flexio_event_handler_attr event_handler_attr = {0};

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(func_state);
    NICC_CHECK_POINTER(app_handler->binary.dpa.kernel);
    NICC_CHECK_POINTER(app_handler->binary.dpa.host_stub);

    // create flexio process
    if(unlikely(FLEXIO_STATUS_SUCCESS != 
        (res = flexio_process_create(
            func_state->ibv_ctx,
            reinterpret_cast<flexio_app*>(app_handler->binary.dpa.kernel),
            nullptr,
            &func_state->flexio_process
        ))
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
    event_handler_attr.host_stub_func = reinterpret_cast<flexio_func_t*>(app_handler->binary.dpa.host_stub);
    event_handler_attr.affinity.type = FLEXIO_AFFINITY_STRICT;
    event_handler_attr.affinity.id = this->_desp->core_id;
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


nicc_retval_t ComponentBlock_DPA::__allocate_wrapper_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state);

    this->_function_state->channel = new Channel_DPA(Channel::ETHERNET, Channel::PAKT_UNORDERED);

    // allocate channel
    if(unlikely(NICC_SUCCESS !=(
        retval = _function_state->channel->allocate_channel(func_state->pd, func_state->uar, func_state->flexio_process, func_state->event_handler, func_state->ibv_ctx)
    ))){
        NICC_WARN_C(
            "failed to allocate and init DPA channel: nicc_retval(%u)", retval
        );
        goto exit;
    }

 exit:

    // TODO: destory if failed

    return retval;
}


nicc_retval_t ComponentBlock_DPA::__init_wrapper_resources(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state){
    int ret = 0;
    uint64_t rpc_ret_val;
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(app_handler->binary.dpa.host_stub);
    NICC_CHECK_POINTER(func_state);

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_process_call(
            func_state->flexio_process,
            *(reinterpret_cast<flexio_func_t*>(app_handler->binary.dpa.host_stub)),
            &rpc_ret_val,
            func_state->channel->d_dev_queues
        )
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


nicc_retval_t ComponentBlock_DPA::__deallocate_wrapper_resources(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(func_state);

    // deallocate channel
    if(unlikely(NICC_SUCCESS !=(
        retval = _function_state->channel->deallocate_channel(func_state->flexio_process)
    ))){
        NICC_WARN_C(
            "failed to allocate and init DPA channel: nicc_retval(%u)", retval
        );
        goto exit;
    }    

exit:
    return retval;
}


} // namespace nicc
