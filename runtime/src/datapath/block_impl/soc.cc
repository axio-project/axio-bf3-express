#include "datapath/block_impl/soc.h"

namespace nicc {

nicc_retval_t ComponentBlock_SoC::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    NICC_CHECK_POINTER(this->_function_state = new ComponentFuncState_SoC_t());

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
        case handler_typeid_t::Event:
            event_handler = app_handler;
            break;
        default:
            NICC_ERROR_C_DETAIL("unregornized handler id for DPA, this is a bug: handler_id(%u)", app_handler->tid);
        }
    }
    /* Step 2: Allocate and record funciton state */
    /* ...... */
    
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


} // namespace nicc
