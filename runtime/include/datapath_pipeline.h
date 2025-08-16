#pragma once

#include <iostream>
#include <chrono>
#include <thread>

#include "common.h"
#include "log.h"
#include "resources/resource_pool.h"
#include "utils/ibv_device.h"
#include "utils/app_dag.h"
#include "utils/qpinfo.hh"
#include "utils/mgnt_connection.h"
#include "datapath/block_impl/dpa.h"
#include "datapath/block_impl/flow_engine.h"
#include "datapath/block_impl/soc.h"
#include "ctrlpath/routing.h"

namespace nicc {


class DatapathPipeline {
 public:
    /*! 
     *  \brief  initialization of datapath pipeline
     *  \param  rpool               global resource pool
     *  \param  app_cxt             the application context of this datapath pipeline
     *  \param  device_state        global device state
     *  \param  app_dag             application DAG
     */
    DatapathPipeline(ResourcePool& rpool, AppContext *app_cxt, device_state_t& device_state, AppDAG *app_dag);
    ~DatapathPipeline();

 private:
    // app_func -> component block
    std::map<AppFunction*, ComponentBlock*> _component_app2block_map;

    // component block vector
    std::vector<ComponentBlock*> _component_blocks;
    
    // application context on this datapath pipeline
    AppContext *_app_cxt;

    // application DAG config
    AppDAG *_app_dag;

    // global device state
    device_state_t *_device_state;
    
    // global pipeline routing manager
    PipelineRouting *_pipeline_routing;

    /*!
     *  \brief  allocate component block from the resource pool
     *  \param  rpool               global resource pool
     *  \param  device_state        global device state
     */ 
    nicc_retval_t __allocate_component_blocks(ResourcePool& rpool, device_state_t& device_state);

    /*!
     *  \brief  return allocated component block to the resource pool
     *  \param  rpool               global resource pool
     *  \return NICC_SUCCESS for successfully deallocation
     */ 
    nicc_retval_t __deallocate_component_blocks(ResourcePool& rpool);

    /*!
     *  \brief  register all functions onto the component block after allocation, 
     *   this function will create wrapper, communication channels, and ctrl-plane MAT
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successfully registration
     */ 
    nicc_retval_t __register_functions(device_state_t &device_state);

    /**
     *  \brief  initialize control plane (e.g., update MAT and connect channels for each component)
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successfully initialization
     */
    nicc_retval_t __init_control_plane(device_state_t &device_state);

    /*!
     *  \brief  start to run the datapath pipeline
     *  \return NICC_SUCCESS for successfully deregistration
     *  \todo
     */
    nicc_retval_t __run_pipeline();

    /*!
     *  \brief  reschedule a specific block in the pipeline
     *  \return NICC_SUCCESS for successfully deregistration
     *  \todo
     */
    nicc_retval_t __reschedule_block();

};

} // namespace nicc
