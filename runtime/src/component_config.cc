#include <iostream>
#include <string>

#include "common.h"
#include "datapath_pipeline.h"

#include "datapath/block_impl/dpa.h"
#include "datapath/block_impl/flow_engine.h"

// nicc::ComponentDesp_FlowEngine_t flow_engine_config = {
//     .base_config = {
//         .quota = 400000
//     }
// };

nicc::ComponentDesp_DPA_t dpa_config = {
    .base_desp = { .quota = 128 },
};
