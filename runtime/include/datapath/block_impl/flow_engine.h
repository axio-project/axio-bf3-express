#pragma once
#include <iostream>

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"

namespace nicc {
/**
 * ----------------------General structure----------------------
 */ 
/**
 * \brief  specific state of the component block,
 *         using for control plane, including rescheduling,
 *         inter-block communication channel, and MT
 */
typedef struct ComponentState_FlowEngine {
    /* ========== Common fields ========== */
    ComponentBaseState_t base_state;
    /* ========== Specific fields ========== */
    uint8_t mock_state;
} ComponentState_FlowEngine_t;

/**
 * \brief  specific descriptor of the component block,
 *         using for allocating / deallocating component blocks
 *         from the resource pool
 */
typedef struct ComponentDesp_FlowEngine {
    /* ========== Common fields ========== */
    ComponentBaseDesp_t base_desp;
    /* ========== Specific fields ========== */
} ComponentDesp_FlowEngine_t;


/**
 *  \brief  basic state of the function register 
 *          into the component block, 
 *          using for running the function on this component block
 *  \note   [1] func state can be modify by other component blocks
 */
typedef struct ComponentFuncState_FlowEngine {
    /* ========== Common fields ========== */
    ComponentFuncBaseState_t base_state;
    /* ========== Specific fields ========== */
} ComponentFuncState_FlowEngine_t;


class ComponentBlock_FlowEngine : public ComponentBlock {
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    ComponentBlock_FlowEngine() {
        NICC_CHECK_POINTER(this->_desp = new ComponentDesp_FlowEngine_t);
        NICC_CHECK_POINTER(this->_state = new ComponentState_FlowEngine_t);
        NICC_CHECK_POINTER(this->_base_desp = &this->_desp->base_desp);
        NICC_CHECK_POINTER(this->_base_state = &this->_state->base_state);
    }
    ~ComponentBlock_FlowEngine(){};

    /**
     *  \brief  typeid of handlers register into FlowEngine
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Event };

    /**
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this 
     *                    component, at least contains Init
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successful registeration
     */
    nicc_retval_t register_app_function(AppFunction *app_func, device_state_t &device_state) override;

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    nicc_retval_t unregister_app_function() override;
/**
 * ----------------------Internel Parameters----------------------
 */ 
    friend class Component_FlowEngine;
 protected:
    /**
     * descriptor of the component block, recording total 
     * hardware resources allocated from the component
     */
    ComponentDesp_FlowEngine_t *_desp;

    /**
     * state of the component block, recording runtime state 
     * for rescheduling, inter-block communication channel, and MT
     */
    ComponentState_FlowEngine_t *_state;

    /**
     * basic state of the function register into the component  
     * block, using for running the function on this component block
     */
    ComponentFuncState_FlowEngine_t *_function_state = nullptr;
    
};


} // namespace nicc
