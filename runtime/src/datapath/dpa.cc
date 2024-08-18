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
    AppHandler *app_handler;
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
            
            break;
        
        case Event:
            if(unlikely(NICC_SUCCESS !=
                (retval = this->__register_event_handler(app_func, app_handler, func_state))
            )){
                NICC_WARN_C("failed to register handler on DPA block: handler_tid(%u), retval(%u)", Event, retval);
                goto exit;
            }
            break;
        default:
            NICC_ERROR_C_DETIAL("unregornized handler id for DPA, this is a bug: handler_id(%u)", handler_type);
        }
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

} // namespace nicc
