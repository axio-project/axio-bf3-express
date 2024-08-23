/**
 * @file datapath_pipeline.cc
 * @brief Init and allocate datapath components for all apps, then construct the datapath pipeline.
 */
#include <iostream>

#include "common.h"
#include "log.h"
#include "resource_pool.h"
#include "datapath_pipeline.h"
#include "datapath/flow_engine.h"

namespace nicc {

DatapathPipeline::DatapathPipeline(uint16_t enabled_components, ResourcePool& rpool) {
  nicc_retval_t retval = NICC_SUCCESS;
  /* Init datapath components */
  retval = this->__init(enabled_components, rpool);
  if(unlikely(retval != NICC_SUCCESS)){
    NICC_ERROR("failed to initialize datapath pipeline");
  }
  /* Allocate DPU resources */
  /* Register App context */
}

DatapathPipeline::~DatapathPipeline() {
  /* Free all resources */
}

nicc_retval_t DatapathPipeline::__init(uint16_t enabled_components, ResourcePool& rpool) {
  nicc_retval_t retval = NICC_SUCCESS;

  // if (enabled_components & kComponent_FlowEngine) {
  //   // TODO: change to allocate block
  //   ComponentDesp_FlowEngine_t fe_config;

  //   this->_flow_engine = new Component_FlowEngine();
  //   NICC_CHECK_POINTER(this->_flow_engine);

  //   retval = this->_flow_engine->init(&fe_config);
  //   if(unlikely(retval != NICC_SUCCESS)){
  //     NICC_WARN("failed to initialize flow engine");
  //     goto exit;
  //   }
  // }

  if (enabled_components & kComponent_DPA) {
    // Init DPA engine 
  }
  
  if (enabled_components & kComponent_ARM) {
    // Init SoC engine
  }
  
  if (enabled_components & kComponent_Decompress) {
    // Init decompress engine
  }
  
  if (enabled_components & kComponent_SHA) {
    // Init SHA engine
  }
  
  return retval;
}

} // namespace nicc
