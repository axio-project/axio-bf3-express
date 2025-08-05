#pragma once
#include "ctrlpath/routing.h"

namespace nicc {

/**
 * ----------------------SoC Routing Implementation----------------------
 */ 

/**
 *  \brief  SoC-specific routing implementation
 *          Inherits from ComponentRouting and provides SoC-specific logic
 */
class ComponentRouting_SoC : public ComponentRouting {
public:
    ComponentRouting_SoC() : ComponentRouting(kComponent_SoC) {
        NICC_LOG("ComponentRouting_SoC initialized for SoC component");
    }
    
    virtual ~ComponentRouting_SoC() = default;

    /**
     *  \brief  SoC-specific packet forwarding with routing decision
     *          This method integrates with SoCWrapper's packet processing
     *  \param  packet          packet buffer to forward
     *  \param  kernel_retval   return value from user kernel
     *  \param  default_tx_channel  fallback channel if routing fails
     *  \return NICC_SUCCESS for successful forwarding
     */
    nicc_retval_t forward_packet_after_kernel(
        Buffer* packet, 
        nicc_core_retval_t kernel_retval, 
        nicc::Channel* default_tx_channel
    );

protected:
    /**
     *  \brief  SoC-specific channel registration logic
     *  \param  id          logical channel identifier
     *  \param  channel     pointer to actual datapath channel
     *  \return NICC_SUCCESS for successful registration
     */
    nicc_retval_t __register_datapath_channel(const channel_id_t& id, nicc::Channel* channel) override;

    /**
     *  \brief  SoC-specific lookup logic with flow analysis
     *  \param  flow            packet flow information
     *  \param  kernel_retval   return value from user kernel
     *  \return pointer to target datapath channel, nullptr if not found
     */
    nicc::Channel* __lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval) override;

    /**
     *  \brief  SoC-specific configuration loading with JSON parsing
     *  \param  config_path     path to configuration file
     *  \return NICC_SUCCESS for successful loading
     */
    nicc_retval_t __load_config(const std::string& config_path) override;
};

} // namespace nicc