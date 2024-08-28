#pragma once

#include <iostream>

#include <doca_flow.h>
#include <infiniband/mlx5dv.h>
#include <infiniband/mlx5_api.h>

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"


namespace nicc {


/*!
*  \brief  state of FlowEngine
*/
typedef struct ComponentState_FlowEngine {
    // basic state
    ComponentBaseState_t base_state;

    // state of FlowEngine
    uint8_t mock_state;
} ComponentState_FlowEngine_t;

/*!
*  \brief  descriptor of FlowEngine
*/
typedef struct ComponentDesp_FlowEngine {
    /* ========== Common fields ========== */
    // basic desriptor
    ComponentBaseDesp_t base_desp;
} ComponentDesp_FlowEngine_t;


/*!
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncState_FlowEngine {
    ComponentFuncBaseState_t base_state;

    struct dr_flow_table		*rx_flow_table;
	struct dr_flow_table		*tx_flow_table;
	struct dr_flow_table		*tx_flow_root_table;

	struct dr_flow_rule		*rx_rule;
	struct dr_flow_rule		*tx_rule;
	struct dr_flow_rule		*tx_root_rule;
} ComponentFuncState_FlowEngine_t;


class ComponentBlock_FlowEngine : public ComponentBlock {
 public:
    ComponentBlock_FlowEngine() {
        NICC_CHECK_POINTER(this->_desp = reinterpret_cast<ComponentBaseDesp_t*> (new ComponentDesp_FlowEngine_t));
        NICC_CHECK_POINTER(this->_state = reinterpret_cast<ComponentBaseState_t*> (new ComponentState_FlowEngine_t));
    }
    ~ComponentBlock_FlowEngine(){};

    /*!
     *  \brief  typeid of handlers register into FlowEngine
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

    friend class Component_FlowEngine;
 
 private:
    nicc_retval_t __create_rx_steering_rule(AppFunction *app_func, device_state_t &device_state);

    nicc_retval_t __create_tx_steering_rule(AppFunction *app_func, device_state_t &device_state);
};


} // namespace nicc
