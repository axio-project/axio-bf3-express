/**
 * @file datapath_pipeline.h
 * @brief All applications would be compiled into a single datapath pipeline. 
 * An instance of the datapath pipeline records all the allocated resources for each app, 
 * and exposes api for resource re-allocation. 
 */

#pragma once

#include <iostream>
#include "common.h"
#include "datapath/flow_engine.h"

namespace nicc
{
/*!
 *  \brief  mask for enabling dataplane component
 */
#define FLOW_ENGINE_ENABLED 0x01
#define DPA_ENGINE_ENABLED 0x02
#define SOC_ENGINE_ENABLED 0x04
#define DECOMPRESS_ENGINE_ENABLED 0x08
#define SHA_ENGINE_ENABLED 0x10

class DatapathPipeline {
  /**
   * ----------------------Parameters in Datapath level----------------------
   */ 

  /**
   * ----------------------Datapath internal structures----------------------
   */ 

  /**
   * ----------------------Datapath methods----------------------
   */ 

 public:
    DatapathPipeline(uint16_t enabled_components);
    ~DatapathPipeline();

  /**
   * ----------------------Util methods----------------------
   */ 
  
 private:
  // component: flow engine
  Component_FlowEngine *_flow_engine;
  
  /*!
   *  \brief  initialization of each dataplane component
   *  \param  enabled_components  mask of used dataplane component
   */ 
  nicc_retval_t __init(uint16_t enabled_components);

};

} // namespace nicc
