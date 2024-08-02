#include <iostream>

#include "datapath_pipeline.h"

int main(){
    /* Parse config file */

    /* Init app contexts, including function and state for each datapath component */

    /* Init and allocate datapath components for all apps, then construct the datapath pipeline */
    uint16_t enabled_components = FLOW_ENGINE_ENABLED | DPA_ENGINE_ENABLED | SOC_ENGINE_ENABLED | DECOMPRESS_ENGINE_ENABLED | SHA_ENGINE_ENABLED;

    nicc::DatapathPipeline dp_pipeline (enabled_components);

    /* Init control path, including user-request channel, just-in-time verifier, and rule loader (SONiC) */
        /* Init control path MTs */

        /* Update control path rules to construct pipeline */

}
