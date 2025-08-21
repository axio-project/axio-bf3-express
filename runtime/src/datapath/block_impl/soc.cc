#include "datapath/block_impl/soc.h"
#include "ctrlpath/route_impl/soc_routing.h"

namespace nicc {
static void __soc_wrapper_thread_func(ComponentFuncState_SoC_t *func_state);

nicc_retval_t ComponentBlock_SoC::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    AppHandler *app_handler = nullptr;
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
            this->_init_handler = app_handler;
            break;
        case handler_typeid_t::Pkt_Handler:
            this->_pkt_handler = app_handler;
            break;
        case handler_typeid_t::Msg_Handler:
            this->_msg_handler = app_handler;
            break;
        case handler_typeid_t::Cleanup:
            this->_cleanup_handler = app_handler;
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
        (retval = this->__allocate_wrapper_resources(app_func))
    )){
        NICC_WARN_C("failed to allocate reosurce on SoC block: retval(%u)", retval);
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

nicc_retval_t ComponentBlock_SoC::connect_to_neighbour( const ComponentBlock *prior_component_block, 
                                                        const ComponentBlock *next_component_block,
                                                        bool is_connected_to_remote,
                                                        const QPInfo *remote_host_qp_info,
                                                        bool is_connected_to_local,
                                                        const QPInfo *local_host_qp_info){
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(this->_function_state->channel);

    if (is_connected_to_remote){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(true, nullptr, remote_host_qp_info)
        ))) {
            NICC_WARN_C("failed to connect to remote component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (is_connected_to_local){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(false, nullptr, local_host_qp_info)
        ))) {
            NICC_WARN_C("failed to connect to local component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (prior_component_block != nullptr){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(true, prior_component_block, nullptr)
        ))) {
            NICC_WARN_C("failed to connect to prior component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (next_component_block != nullptr){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(false, next_component_block, nullptr)
        ))) {
            NICC_WARN_C("failed to connect to next component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    return retval;
}

nicc_retval_t ComponentBlock_SoC::run_block(){
    nicc_retval_t retval = NICC_SUCCESS;
    // create wrapper process for the function
    if(unlikely(NICC_SUCCESS != (
        retval = this->__create_wrapper_process(this->_function_state)
    ))) {
        NICC_WARN_C("failed to create wrapper process for the function: nicc_retval(%u)", retval);
        goto exit;
    }
    /// wait thread join. 
    // this->_function_state->wrapper_thread->join();

exit:
    return retval;
}

QPInfo *ComponentBlock_SoC::get_qp_info(bool is_prior){
    return is_prior ? this->_function_state->channel->qp_for_prior_info : this->_function_state->channel->qp_for_next_info;
}

ComponentRouting* ComponentBlock_SoC::get_component_routing(){
    return this->routing;
}

nicc_retval_t ComponentBlock_SoC::register_local_channels(){
    nicc_retval_t retval = NICC_SUCCESS;
    
    if (!this->routing) {
        NICC_ERROR("Routing component not initialized");
        return NICC_ERROR;
    }
    
    if (!this->_function_state || !this->_function_state->channel) {
        NICC_ERROR("SoC channels not allocated yet");
        return NICC_ERROR;
    }
    
    Channel_SoC* soc_channel = this->_function_state->channel;
    
    retval = this->routing->register_local_channel("default", static_cast<nicc::Channel*>(soc_channel));
    if (retval != NICC_SUCCESS) {
        NICC_ERROR_C("Failed to register default channel: retval(%u)", retval);
        return retval;
    }
    
    // Set default channel for unmapped return values
    retval = this->routing->set_default_channel(static_cast<nicc::Channel*>(soc_channel));
    if (retval != NICC_SUCCESS) {
        NICC_ERROR_C("Failed to set default channel: retval(%u)", retval);
        return retval;
    }
    
    NICC_LOG("Successfully registered local channels for SoC component '%s'", this->get_block_name().c_str());
    
    return retval;
}

nicc_retval_t ComponentBlock_SoC::__allocate_wrapper_resources(AppFunction *app_func) {
    nicc_retval_t retval = NICC_SUCCESS;

    this->_function_state->channel = new Channel_SoC(Channel::RDMA, Channel::PAKT_UNORDERED, Channel::RDMA, Channel::PAKT_UNORDERED);
    // allocate channel
    if(unlikely(NICC_SUCCESS != (
        retval = this->_function_state->channel->allocate_channel(this->_desp->device_name, this->_desp->phy_port)
    ))) {
        NICC_WARN_C("failed to allocate and init SoC channel: nicc_retval(%u)", retval);
        goto exit;
    }   
    // allocate routing
    NICC_CHECK_POINTER(this->routing = new ComponentRouting_SoC());

exit:

    // TODO: destory if failed

    return retval;
}


nicc_retval_t ComponentBlock_SoC::__create_wrapper_process(ComponentFuncState_SoC_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(func_state->context = new SoCWrapper::SoCWrapperContext());
    // NICC_CHECK_POINTER(context->pkt_handler = func_state->pkt_handler);
    // NICC_CHECK_POINTER(context->match_action_table = func_state->match_action_table);
    NICC_CHECK_POINTER(func_state->context->qp_for_prior = func_state->channel->qp_for_prior);
    NICC_CHECK_POINTER(func_state->context->qp_for_next = func_state->channel->qp_for_next);

    // pass user defined handlers to wrapper context
    if (this->_init_handler) {
        func_state->context->init_handler = (soc_init_handler_t)this->_init_handler->binary.soc;
    } else {
        func_state->context->init_handler = nullptr;
    }
    
    if (this->_pkt_handler) {
        func_state->context->pkt_handler = (soc_pkt_handler_t)this->_pkt_handler->binary.soc;
    } else {
        func_state->context->pkt_handler = nullptr;
    }
    
    if (this->_msg_handler) {
        func_state->context->msg_handler = (soc_msg_handler_t)this->_msg_handler->binary.soc;
    } else {
        func_state->context->msg_handler = nullptr;
    }
    
    if (this->_cleanup_handler) {
        func_state->context->cleanup_handler = (soc_cleanup_handler_t)this->_cleanup_handler->binary.soc;
    } else {
        func_state->context->cleanup_handler = nullptr;
    }
    
    // user_state will be allocated by user's init_handler
    func_state->context->user_state = nullptr;
    func_state->context->user_state_size = 0;

    // create wrapper process for the function
    NICC_CHECK_POINTER(func_state->wrapper_thread = new std::thread(__soc_wrapper_thread_func, func_state));
    // bind the thread to the core
    size_t core = bind_to_core(*func_state->wrapper_thread, /*SoC only has numa 0*/0, /*thread id, \todo: enable multiple threads*/0);
    NICC_LOG("Successfully created SoC wrapper thread: core(%lu)", core);

    return retval;
}

static void __soc_wrapper_thread_func(ComponentFuncState_SoC_t *func_state) {
    // Create a SoCWrapper object and call its run method
    SoCWrapper wrapper(SoCWrapper::kSoC_Dispatcher, func_state->context);
}

} // namespace nicc
