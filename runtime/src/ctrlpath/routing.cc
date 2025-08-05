#include "ctrlpath/routing.h"
#include <iostream> // For temporary logging

namespace nicc {

/**
 * ----------------------Base ComponentRouting Implementation----------------------
 */ 

nicc_retval_t ComponentRouting::register_datapath_channel(const channel_id_t& id, nicc::Channel* channel) {
    if (this->_state->datapath_channels.count(id) > 0) {
        NICC_WARN_C("Channel ID '%s' already registered.", id.c_str());
        return NICC_ERROR_DUPLICATED;
    }
    if (!channel) {
        NICC_ERROR_C("Attempted to register null channel for ID '%s'.", id.c_str());
        return NICC_ERROR;
    }
    
    // Call component-specific registration logic
    nicc_retval_t ret = this->__register_datapath_channel(id, channel);
    if (ret != NICC_SUCCESS) {
        return ret;
    }
    
    this->_state->datapath_channels[id] = channel;
    NICC_LOG("Registered datapath channel '%s'.", id.c_str());
    return NICC_SUCCESS;
}

nicc_retval_t ComponentRouting::add_retval_mapping(nicc_core_retval_t retval, const channel_id_t& target_channel_id) {
    if (this->_state->retval_to_channel_id_map.count(retval) > 0) {
        NICC_WARN_C("Return value %d already has a mapping. Overwriting.", retval);
    }
    this->_state->retval_to_channel_id_map[retval] = target_channel_id;
    NICC_LOG("Added retval mapping: %d -> '%s'.", retval, target_channel_id.c_str());
    return NICC_SUCCESS;
}

nicc_retval_t ComponentRouting::set_default_channel(const channel_id_t& default_id) {
    this->_state->default_channel_id = default_id;
    NICC_LOG("Default channel set to '%s'.", default_id.c_str());
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting::lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval) {
    // First try component-specific lookup logic
    nicc::Channel* result = this->__lookup_channel(flow, kernel_retval);
    if (result) {
        return result;
    }
    
    // Fallback to generic lookup by kernel_retval
    auto it = this->_state->retval_to_channel_id_map.find(kernel_retval);
    channel_id_t target_id;

    if (it != this->_state->retval_to_channel_id_map.end()) {
        target_id = it->second;
        NICC_LOG("Lookup by kernel_retval %d found target '%s'.", kernel_retval, target_id.c_str());
    } else {
        // Fallback to default channel if no specific mapping
        target_id = this->_state->default_channel_id;
        NICC_LOG("No specific mapping for kernel_retval %d. Using default channel '%s'.", kernel_retval, target_id.c_str());
    }

    // Now, find the actual datapath channel pointer
    auto channel_it = this->_state->datapath_channels.find(target_id);
    if (channel_it != this->_state->datapath_channels.end()) {
        NICC_LOG("Resolved target channel ID '%s' to datapath channel pointer.", target_id.c_str());
        return channel_it->second;
    } else {
        NICC_ERROR_C("Target channel ID '%s' not found in registered datapath channels.", target_id.c_str());
        return nullptr; // Or a special "drop" channel pointer
    }
}

nicc_retval_t ComponentRouting::load_config(const std::string& config_path) {
    NICC_LOG("Loading routing configuration from: %s", config_path.c_str());
    
    // Update descriptor
    this->_desp->config_path = config_path.c_str();
    
    // Call component-specific configuration loading
    return this->__load_config(config_path);
}

nicc_retval_t ComponentRouting::unregister_datapath_channel(const channel_id_t& id) {
    auto it = this->_state->datapath_channels.find(id);
    if (it == this->_state->datapath_channels.end()) {
        NICC_WARN_C("Channel ID '%s' not found for unregistration.", id.c_str());
        return NICC_ERROR_NOT_FOUND;
    }
    
    this->_state->datapath_channels.erase(it);
    NICC_LOG("Unregistered datapath channel '%s'.", id.c_str());
    return NICC_SUCCESS;
}

nicc_retval_t ComponentRouting::remove_retval_mapping(nicc_core_retval_t retval) {
    auto it = this->_state->retval_to_channel_id_map.find(retval);
    if (it == this->_state->retval_to_channel_id_map.end()) {
        NICC_WARN_C("Return value %d mapping not found for removal.", retval);
        return NICC_ERROR_NOT_FOUND;
    }
    
    this->_state->retval_to_channel_id_map.erase(it);
    NICC_LOG("Removed retval mapping for return value %d.", retval);
    return NICC_SUCCESS;
}



} // namespace nicc