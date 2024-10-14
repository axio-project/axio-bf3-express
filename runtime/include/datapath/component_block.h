#pragma once
#include "common.h"
#include "app_context.h"
#include "utils/ibv_device.h"
#include "ctrlpath/mat.h"

namespace nicc {

/*!
 *  \brief  block of dataplane component, allocated to specific application
 */
class ComponentBlock {
 public:
    ComponentBlock() {}
    virtual ~ComponentBlock(){}

    /*!
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this component
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successful registeration
     */
    virtual nicc_retval_t register_app_function(AppFunction *app_func, device_state_t &device_state){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    virtual nicc_retval_t unregister_app_function(){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

/**
 * ----------------------Protected Parameters----------------------
 */ 
 protected:
    // descriptor of the component block
    ComponentBaseDesp_t *_base_desp;

    // state of the component block
    ComponentBaseState_t *_base_state;

    // state of the function register into the component block
    ComponentFuncBaseState_t *_base_function_state = nullptr;

    // domain of flows in current component block
    FlowDomain *_base_domain;
};

} // namespace nicc

