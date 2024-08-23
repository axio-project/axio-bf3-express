#pragma once

#include <iostream>

#if defined(NICC_DOCA_ENABLED)
    #include <doca_flow.h>
#else
    #include <rte_flow.h>
#endif // NICC_DOCA_ENABLED

#include "datapath/component.h"

namespace nicc {

} // namespace nicc
