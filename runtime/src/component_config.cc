#include <iostream>

#include "common.h"
#include "datapath_pipeline.h"

#include "datapath/dpa.h"
#include "datapath/flow_engine.h"
#include "datapath/dpa.h"

nicc::ComponentDesp_FlowEngine_t flow_engine_config = {
    .base_config = {
        .quota = 400000
    }
};

nicc::ComponentDesp_DPA_t dpa_config = {
    .base_config = {
        .quota = 256
    },
    .device_name = "mlx5_0",
};
