#include "datapath/block_impl/template.h"

namespace nicc {

/*!
 *  \brief  register a new application function into this component
 *  \param  app_func  the function to be registered into this component
 *  \param  device_state        global device state
 *  \return NICC_SUCCESS for successful registeration
 */
nicc_retval_t ComponentBlock_TEMPLATE::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    
 exit:
    return retval;
}


nicc_retval_t ComponentBlock_TEMPLATE::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


} // namespace nicc
