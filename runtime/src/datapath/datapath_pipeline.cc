/**
 * @file datapath_pipeline.cc
 * @brief Init and allocate datapath components for all apps, then construct the datapath pipeline.
 */
#include "datapath_pipeline.h"

namespace nicc {
  DatapathPipeline::DatapathPipeline(uint16_t enabled_components) {
    /* Init datapath components */
    doca_error_t res = datapath_init(enabled_components);

    /* Allocate DPU resources */

    /* Register App context */

    /* Construct pipeline */

  }

  DatapathPipeline::~DatapathPipeline() {
    /* Free all resources */
  }

  doca_error_t
  DatapathPipeline::datapath_init(uint16_t enabled_components) {
    if (enabled_components & FLOW_ENGINE_ENABLED) {
      // Init flow engine 
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
    return DOCA_SUCCESS;
  }

} // namespace nicc