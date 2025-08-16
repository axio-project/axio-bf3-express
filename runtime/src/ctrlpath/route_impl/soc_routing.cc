#include "ctrlpath/route_impl/soc_routing.h"

namespace nicc {

/**
 * ----------------------SoC ComponentRouting Implementation----------------------
 */ 


nicc_retval_t ComponentRouting_SoC::register_local_channel(const std::string& channel_name, nicc::Channel* channel) {
    NICC_LOG("SoC: Registering local channel '%s'.", channel_name.c_str());
    
    // Call base class implementation
    nicc_retval_t ret = ComponentRouting::register_local_channel(channel_name, channel);
    if (ret != NICC_SUCCESS) {
        return ret;
    }
    
    // TODO: Add SoC-specific channel setup if needed
    // For example, configure channel parameters specific to SoC
    
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting_SoC::lookup_channel(nicc_core_retval_t kernel_retval) {
    NICC_LOG("SoC: Looking up channel for kernel_retval=%d.", kernel_retval);
    
    // Call base class implementation
    nicc::Channel* channel = ComponentRouting::lookup_channel(kernel_retval);
    
    if (channel) {
        NICC_LOG("SoC: Found target channel for retval=%d.", kernel_retval);
    } else {
        NICC_WARN_C("SoC: No channel mapping found for retval=%d.", kernel_retval);
    }
    
    return channel;
}

} // namespace nicc