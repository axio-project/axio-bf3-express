#pragma once

#pragma once

#include <iostream>

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"

namespace nicc {


/*!
*  \brief  state of TEMPLATE
*/
typedef struct ComponentState_TEMPLATE {
    // basic state
    ComponentBaseState_t base_state;
    // state of TEMPLATE
    uint8_t mock_state;
} ComponentState_TEMPLATE_t;

/*!
*  \brief  descriptor of TEMPLATE
*/
typedef struct ComponentDesp_TEMPLATE {
    /* ========== Common fields ========== */
    // basic desriptor
    ComponentBaseDesp_t base_desp;
} ComponentDesp_TEMPLATE_t;


/*!
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncState_TEMPLATE {
    ComponentFuncBaseState_t base_state;
} ComponentFuncState_TEMPLATE_t;


class ComponentBlock_TEMPLATE : public ComponentBlock {
 public:
    ComponentBlock_TEMPLATE() {
        NICC_CHECK_POINTER(this->_desp = reinterpret_cast<ComponentBaseDesp_t*> (new ComponentDesp_TEMPLATE_t));
        NICC_CHECK_POINTER(this->_state = reinterpret_cast<ComponentBaseState_t*> (new ComponentState_TEMPLATE_t));
    }
    ~ComponentBlock_TEMPLATE(){};

    /*!
     *  \brief  typeid of handlers register into TEMPLATE
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Event };


    /*!
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this component
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successful registeration
     */
    nicc_retval_t register_app_function(AppFunction *app_func, device_state_t &device_state) override;

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    nicc_retval_t unregister_app_function() override;

    friend class Component_TEMPLATE;
 private:
}


} // namespace nicc
