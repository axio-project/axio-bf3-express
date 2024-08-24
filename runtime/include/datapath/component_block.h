#pragma once
#include "common.h"
#include "app_context.h"

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
     *  \return NICC_SUCCESS for successful registeration
     */
    virtual nicc_retval_t register_app_function(AppFunction *app_func){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    virtual nicc_retval_t unregister_app_function(){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    friend class Component;
    friend class Component_DPA;

 protected:
    // descriptor of the component block
    ComponentBaseDesp_t *_desp;

    // state of the component block
    ComponentBaseState_t *_state;

    // map of function states which has been registered in to this component block
    ComponentFuncBaseState_t *_function_state = nullptr;
};

} // namespace nicc

