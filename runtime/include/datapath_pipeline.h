#pragma once

#include <iostream>

#include "common.h"
#include "log.h"
#include "resource_pool.h"
#include "datapath/flow_engine.h"
#include "datapath/dpa.h"

namespace nicc {

class DatapathPipeline {
 public:
    /*! 
     *  \brief  initialization of datapath pipeline
     *  \param  rpool               global resource pool
     *  \param  app_cxt             the application context of this datapath pipeline
     */
    DatapathPipeline(ResourcePool& rpool, AppContext *app_cxt);
    ~DatapathPipeline();

 private:
    // app_func -> component block
    std::map<AppFunction*, ComponentBlock*> _component_block_map;
    
    // application context on this datapath pipeline
    AppContext *_app_cxt;

    /*!
     *  \brief  allocate component block from the resource pool
     *  \param  rpool               global resource pool
     */ 
    nicc_retval_t __allocate_component_blocks(ResourcePool& rpool);

    /*!
     *  \brief  register all functions onto the component block after allocation
     *  \return NICC_SUCCESS for successfully registration
     */ 
    nicc_retval_t __register_functions();

    /*!
     *  \brief  return allocated component block to the resource pool
     *  \param  rpool               global resource pool
     *  \return NICC_SUCCESS for successfully deallocation
     */ 
    nicc_retval_t __deallocate_component_blocks(ResourcePool& rpool);
};

} // namespace nicc
