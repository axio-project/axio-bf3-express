#pragma once

#include <iostream>

#if defined(NICC_DOCA_ENABLED)
    #include <doca_flow.h>
#else
    #include <rte_flow.h>
#endif // NICC_DOCA_ENABLED

#include "resources/component.h"
#include "datapath/component_block.h"

namespace nicc {

} // namespace nicc
