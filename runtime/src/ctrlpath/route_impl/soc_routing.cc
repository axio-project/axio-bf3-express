#include "ctrlpath/route_impl/soc_routing.h"
#include <iostream> // For temporary logging

namespace nicc {

/**
 * ----------------------SoC ComponentRouting Implementation----------------------
 */ 

nicc_retval_t ComponentRouting_SoC::forward_packet_after_kernel(
    Buffer* packet, 
    nicc_core_retval_t kernel_retval, 
    nicc::Channel* default_tx_channel
) {
    if (!packet) {
        NICC_ERROR_C("Invalid packet buffer in forward_packet_after_kernel.");
        return NICC_ERROR;
    }

    // Create a dummy flow_t for now, as per current lookup signature
    // In a real scenario, flow_t would be extracted from the packet.
    flow_t dummy_flow = {0}; 

    // Use routing to lookup the target datapath channel
    nicc::Channel* target_channel = this->lookup_channel(dummy_flow, kernel_retval);

    if (target_channel) {
        // Here, you would use the target_channel to send the packet.
        // This involves calling the appropriate send method on the datapath channel.
        // For example, if target_channel is a Channel_SoC, you might call its send_msg method.
        // This part needs to be integrated with the actual datapath Channel's send logic.
        NICC_LOG("Packet routed to target channel based on kernel_retval %d.", kernel_retval);
        
        // TODO: Implement actual channel-specific sending logic
        // The challenge is that different channel types (Channel_SoC, Channel_RDMA, etc.)
        // have different send methods. We need a unified interface or type checking.
        // For now, just use the default_tx_channel as a placeholder/example
        if (default_tx_channel) {
            // This is a placeholder. The actual send logic would depend on the type of target_channel.
            // For SoCWrapper, it would likely involve enqueueing to _tmp_worker_tx_queue
            // and then flushing via __tx_flush on the appropriate QP.
            NICC_LOG("Using default_tx_channel for forwarding as a placeholder.");
            // Assuming default_tx_channel is a Channel_SoC, this would be:
            // static_cast<Channel_SoC*>(default_tx_channel)->enqueue_to_tx_queue(packet);
            // And then a flush mechanism.
            return NICC_SUCCESS;
        } else {
            NICC_WARN_C("Target channel found by routing, but no default_tx_channel provided for placeholder send.");
            return NICC_ERROR;
        }
    } else {
        NICC_WARN_C("Routing lookup returned no target channel for kernel_retval %d. Packet dropped.", kernel_retval);
        // Handle packet drop (e.g., free the buffer)
        return NICC_ERROR; // Indicate packet was dropped or not forwarded
    }
}

nicc_retval_t ComponentRouting_SoC::__register_datapath_channel(const channel_id_t& id, nicc::Channel* channel) {
    // SoC-specific channel registration logic
    NICC_LOG("SoC-specific registration for channel '%s'.", id.c_str());
    
    // TODO: Add SoC-specific validation or setup
    // For example, verify that the channel is compatible with SoC component
    
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting_SoC::__lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval) {
    // SoC-specific lookup logic with flow analysis
    // TODO: Implement SoC-specific routing rules based on flow fields
    // For example, route based on flow.nw_dst, flow.tp_dst, etc.
    
    NICC_LOG("SoC-specific lookup for kernel_retval %d.", kernel_retval);
    
    // For now, return nullptr to fall back to generic lookup
    return nullptr;
}

nicc_retval_t ComponentRouting_SoC::__load_config(const std::string& config_path) {
    NICC_LOG("Loading SoC-specific routing configuration from: %s", config_path.c_str());
    
    // TODO: Implement JSON parsing logic for SoC routing configuration
    // This would parse the JSON, call register_datapath_channel, add_retval_mapping, etc.
    // Example JSON structure:
    // {
    //   "channels": [
    //     {"id": "to_dpa", "type": "soc_channel"},
    //     {"id": "to_host", "type": "host_channel"}
    //   ],
    //   "routing_rules": [
    //     {"kernel_retval": 0, "target_channel": "to_host"},
    //     {"kernel_retval": 1, "target_channel": "to_dpa"}
    //   ],
    //   "default_channel": "drop"
    // }
    
    return NICC_SUCCESS;
}

} // namespace nicc