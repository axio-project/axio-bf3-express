#pragma once
#include "common.h"
#include "app_context.h"
#include "utils/ibv_device.h"
#include "utils/qp_info.hh"
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
     *  \brief  connect to a neighbor component, typically is the next component in the app DAG
     *  \param  next_qp_info      [in] the qp info of the next component
     *  \param  remote_qp_info    [in] the qp info of the remote component
     *  \return NICC_SUCCESS for successful connection
     */
    virtual nicc_retval_t connect_to_neighbor_component(const QPInfo *next_qp_info, 
                                                        const QPInfo *remote_qp_info){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     *  \brief  get the qp info of the current component
     *  \return the qp info of the current component
     */
    virtual QPInfo *get_qp_info(){
        return nullptr;
    }

    /* ========= wrapper functions for flow management ========= */
    /**
     *  \brief  create new table in the domain of this component block
     *  \param  level       level of the table
     *  \param  table       created table
     *  \return NICC_SUCCESS for successfully allocation
     */
    nicc_retval_t create_table(int level, FlowMAT** table);

    /**
     *  \brief  destory table in this domain (wrapper)
     *  \param  table       table to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    nicc_retval_t destory_table(FlowMAT* table);

/**
 * ----------------------Public Parameters----------------------
 */ 
 public:
    // component block id
    component_typeid_t component_id = kComponent_Unknown;
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

