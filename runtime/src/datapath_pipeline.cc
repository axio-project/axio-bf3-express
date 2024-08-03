/**
 * @file datapath_pipeline.cc
 * @brief Init and allocate datapath components for all apps, then construct the datapath pipeline.
 */
#include <iostream>

#include "common.h"
#include "log.h"
#include "datapath_pipeline.h"
#include "datapath/flow_engine.h"

namespace nicc {

DatapathPipeline::DatapathPipeline(uint16_t enabled_components) {
  nicc_retval_t retval = NICC_SUCCESS;
  /* Init datapath components */
  retval = this->__init(enabled_components);
  if(unlikely(retval != NICC_SUCCESS)){
    NICC_ERROR("failed to initialize datapath pipeline");
  }

  /* Allocate DPU resources */

  /* Register App context */

exit:
  ;
}

DatapathPipeline::~DatapathPipeline() {
  /* Free all resources */
}

nicc_retval_t DatapathPipeline::__init(uint16_t enabled_components) {
  nicc_retval_t retval = NICC_SUCCESS;

  if (enabled_components & FLOW_ENGINE_ENABLED) {
    this->_flow_engine = new Component_FlowEngine();
    NICC_CHECK_POINTER(this->_flow_engine);
    retval = this->_flow_engine->init();
    if(unlikely(retval != NICC_SUCCESS)){
      NICC_WARN("failed to initialize flow engine");
      goto exit;
    }
  }

  if (enabled_components & DPA_ENGINE_ENABLED) {
    // Init DPA engine 
  }
  
  if (enabled_components & SOC_ENGINE_ENABLED) {
    // Init SoC engine
  }
  
  if (enabled_components & DECOMPRESS_ENGINE_ENABLED) {
    // Init decompress engine
  }
  
  if (enabled_components & SHA_ENGINE_ENABLED) {
    // Init SHA engine
  }

exit:
  return retval;
}

} // namespace nicc
