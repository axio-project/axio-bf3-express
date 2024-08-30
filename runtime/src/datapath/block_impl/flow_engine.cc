#include "datapath/block_impl/flow_engine.h"

namespace nicc {


nicc_retval_t ComponentBlock_FlowEngine::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentFuncState_FlowEngine_t *func_state; 

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(device_state.rx_domain);
    NICC_CHECK_POINTER(device_state.tx_domain);
    NICC_CHECK_POINTER(device_state.fdb_domain);
        
    // create and init function state on this component
    NICC_CHECK_POINTER(func_state = new ComponentFuncState_FlowEngine_t());



 exit:
    return retval;
}


/*!
 *  \brief  deregister a application function
 *  \param  app_func the function to be deregistered from this compoennt
 *  \return NICC_SUCCESS for successful unregisteration
 */
nicc_retval_t ComponentBlock_FlowEngine::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


nicc_retval_t ComponentBlock_FlowEngine::__create_rx_steering_rule(
    ComponentFuncState_FlowEngine_t *func_state, device_state_t &device_state
){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(func_state);

    // func_state->rx_flow_table = mlx5dv_dr_table_create(device_state.rx_domain, 0);
    if(unlikely(func_state->rx_flow_table == nullptr)){
        NICC_WARN_C("failed to create rx flow table on rx domain");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

 exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(func_state->rx_flow_table != nullptr){
            // mlx5dv_dr_table_destory(func_state->rx_flow_table);
        }

    }

    return retval;
}


nicc_retval_t ComponentBlock_FlowEngine::__create_tx_steering_rule(
    ComponentFuncState_FlowEngine_t *func_state, device_state_t &device_state
){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


} // namespace nicc
