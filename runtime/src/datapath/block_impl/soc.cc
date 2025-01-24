#include "datapath/block_impl/soc.h"

namespace nicc {
static void __soc_wrapper_thread_func(ComponentFuncState_SoC_t *func_state);

nicc_retval_t ComponentBlock_SoC::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    ComponentFuncState_SoC_t *func_state;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state = this->_function_state);

    /* Step 1: Register app_func */
    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(this->_desp);    // register func must be after component block alloaction
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
        case handler_typeid_t::Pkt_Handler:
            event_handler = app_handler;
            break;
        default:
            NICC_ERROR_C_DETAIL("unregornized handler id for SoC, this is a bug: handler_id(%u)", app_handler->tid);
        }
    }
    // check handlers
    /* ...... */
    
    /* Step 2: Allocate and record funciton state */
    // allocate wrapper resources, including channel
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_wrapper_resources(app_func, func_state))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: retval(%u)", retval);
        goto exit;
    }
    
 exit:
    return retval;
}


nicc_retval_t ComponentBlock_SoC::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;
    // obtain function state
    NICC_CHECK_POINTER( this->_function_state );
    // deallocate all resources for registered funcitons
    /* ...... */
exit:
    return retval;
}

nicc_retval_t ComponentBlock_SoC::__allocate_wrapper_resources(AppFunction *app_func, ComponentFuncState_SoC_t *func_state) {
    nicc_retval_t retval = NICC_SUCCESS;

    this->_function_state->channel = new Channel_SoC(Channel::RDMA, Channel::PAKT_UNORDERED);
    // allocate channel
    if(unlikely(NICC_SUCCESS != (
        retval = this->_function_state->channel->allocate_channel(this->_desp->device_name, this->_desp->phy_port)
    ))) {
        NICC_WARN_C("failed to allocate and init SoC channel: nicc_retval(%u)", retval);
        goto exit;
    }   

    // create wrapper process for the function
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_wrapper_process(func_state)
    ))) {
        NICC_WARN_C("failed to create wrapper process for the function: nicc_retval(%u)", retval);
        goto exit;
    }

exit:

    // TODO: destory if failed

    return retval;
}


nicc_retval_t ComponentBlock_SoC::__create_wrapper_process(ComponentFuncState_SoC_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(func_state->context = new SoCWrapper::SoCWrapperContext());
    // NICC_CHECK_POINTER(context->pkt_handler = func_state->pkt_handler);
    // NICC_CHECK_POINTER(context->match_action_table = func_state->match_action_table);
    NICC_CHECK_POINTER(func_state->context->prior_qp = func_state->channel->prior_qp);
    NICC_CHECK_POINTER(func_state->context->next_qp = func_state->channel->next_qp);

    // create wrapper process for the function
    NICC_CHECK_POINTER(func_state->wrapper_thread = new std::thread(__soc_wrapper_thread_func, func_state));
    // bind the thread to the core
    size_t core = bind_to_core(*func_state->wrapper_thread, /*SoC only has numa 0*/0, /*thread id, \todo: enable multiple threads*/0);
    NICC_LOG("Successfully created SoC wrapper thread: core(%lu)", core);

    /// wait thread join. \todo move this to __pipeline_run()
    func_state->wrapper_thread->join();
    return retval;
}

static void __soc_wrapper_thread_func(ComponentFuncState_SoC_t *func_state) {
    // Create a SoCWrapper object and call its run method
    SoCWrapper wrapper(SoCWrapper::kSoC_Dispatcher, func_state->context);
}

} // namespace nicc
