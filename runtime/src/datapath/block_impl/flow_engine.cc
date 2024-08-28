#include "datapath/block_impl/flow_engine.h"

namespace nicc {


nicc_retval_t ComponentBlock_FlowEngine::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;

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


} // namespace nicc
