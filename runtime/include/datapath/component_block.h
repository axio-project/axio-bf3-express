#pragma once
#include "common.h"
#include "app_context.h"
#include "utils/ibv_device.h"
#include "utils/qpinfo.hh"
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
     *  \brief  connect to a neighbor component, this method will be called multiple times, and each may connect to prior or next or both components
     *  \param  prior_component_block [in] the previous component block
     *  \param  next_component_block  [in] the next component block
     *  \param  is_connected_to_remote [in] whether the current component is connected to the remote component
     *  \param  remote_qp_info    [in] the qp info of the remote component
     *  \param  is_connected_to_local [in] whether the current component is connected to the local component
     *  \param  local_qp_info     [in] the qp info of the local component
     *  \return NICC_SUCCESS for successful connection
     */
    virtual nicc_retval_t connect_to_neighbour( const ComponentBlock *prior_component_block, 
                                                const ComponentBlock *next_component_block,
                                                bool is_connected_to_remote,
                                                const QPInfo *remote_qp_info,
                                                bool is_connected_to_local,
                                                const QPInfo *local_qp_info){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     *  \brief  run the component block
     *  \return NICC_SUCCESS for successful run
     */
    virtual nicc_retval_t run_block(){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     *  \brief  get the qp info of the current component
     *  \param  is_prior  [in] whether the qp is for the prior component
     *  \return the qp info of the current component
     */
    virtual QPInfo *get_qp_info(bool is_prior){
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
 * ----------------------Util Methods----------------------
 */
 public:
    std::string get_block_name(){
        return std::string(this->block_name);
    }

/**
 * ----------------------Public Parameters----------------------
 */ 
 public:
    // component block id
    component_typeid_t component_id = kComponent_Unknown;
    // component block name
    char block_name[64];
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

