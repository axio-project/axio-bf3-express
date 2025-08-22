#pragma once
#include "ctrlpath/routing.h"

namespace nicc {

/**
 * ----------------------SoC Routing Implementation----------------------
 */ 

/**
 *  \brief  SoC-specific routing implementation
 *          Manages local SoC channels and delegates inter-component routing to PipelineRouting
 */
class ComponentRouting_SoC : public ComponentRouting {
public:
    ComponentRouting_SoC(const std::string& component_id = "soc") 
        : ComponentRouting(kComponent_SoC, component_id) {
        NICC_DEBUG_C("ComponentRouting_SoC initialized for component '%s'", component_id.c_str());
    }
    
    virtual ~ComponentRouting_SoC() = default;

    // Override base methods for SoC-specific behavior  
    /**
     *  \brief  Register SoC-specific local channels (e.g., "to_next", "to_host", "rx_from_prior")
     *  \param  channel_name    name of the channel within SoC component
     *  \param  channel         pointer to actual datapath channel
     *  \return NICC_SUCCESS for successful registration
     */
    nicc_retval_t register_local_channel(const std::string& channel_name, nicc::Channel* channel) override;

    /**
     *  \brief  SoC-specific retval to local channel lookup
     *  \param  kernel_retval   return value from user kernel
     *  \return pointer to target local channel, nullptr if not found
     */
    nicc::Channel* lookup_channel(nicc_core_retval_t kernel_retval) override;
};

} // namespace nicc