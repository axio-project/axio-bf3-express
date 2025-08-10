#include "ctrlpath/routing.h"

namespace nicc {

/**
 * ----------------------PipelineRouting Implementation----------------------
 */ 

nicc_retval_t PipelineRouting::register_component_router(const std::string& component_id, ComponentRouting* router) {
    if (!router) {
        NICC_ERROR_C("Attempted to register null component router for ID '%s'.", component_id.c_str());
        return NICC_ERROR;
    }
    
    if (this->_state->component_routers.count(component_id) > 0) {
        NICC_WARN_C("Component router '%s' already registered. Overwriting.", component_id.c_str());
    }
    
    this->_state->component_routers[component_id] = router;
    NICC_LOG("Registered component router '%s'.", component_id.c_str());
    return NICC_SUCCESS;
}

nicc_retval_t PipelineRouting::load_from_kernel_config(const std::string& config_path) {
    NICC_LOG("Loading DAG configuration from kernel config: %s", config_path.c_str());
    
    // TODO: Parse kernel_config.json and extract ctrl_path entries
    // For each component's ctrl_path.entries, create DAGEdgeRule_t
    // Example: {"match": "retval=1", "action": "fwd(host)"} 
    // -> DAGEdgeRule{kComponent_SoC, 1, "host", "default"}
    
    return NICC_SUCCESS;
}

nicc_retval_t PipelineRouting::build_channel_connections() {
    NICC_LOG("Building channel connections based on DAG edge rules...");
    
    // For each DAG edge rule, find source and target components
    // Then establish the connection between channels
    for (const auto& rule : this->_state->dag_edge_rules) {
        // Find source component router by type
        ComponentRouting* source_router = nullptr;
        for (const auto& pair : this->_state->component_routers) {
            if (pair.second->get_component_type() == rule.source_component) {
                source_router = pair.second;
                break;
            }
        }
        
        if (!source_router) {
            NICC_WARN_C("Source component type %d not found for DAG rule.", rule.source_component);
            continue;
        }
        
        // Find target component router  
        auto target_it = this->_state->component_routers.find(rule.target_component_id);
        if (target_it == this->_state->component_routers.end()) {
            NICC_WARN_C("Target component '%s' not found for DAG rule.", rule.target_component_id.c_str());
            continue;
        }
        
        // Get target channel
        nicc::Channel* target_channel = target_it->second->get_local_channel(rule.target_channel_name);
        if (!target_channel) {
            NICC_WARN_C("Target channel '%s' not found in component '%s'.", 
                       rule.target_channel_name.c_str(), rule.target_component_id.c_str());
            continue;
        }
        
        // Add mapping in source component: retval -> target_channel
        source_router->add_retval_mapping(rule.kernel_retval, target_channel);
        NICC_LOG("Connected: component_type=%d retval=%d -> %s::%s", 
                rule.source_component, rule.kernel_retval, rule.target_component_id.c_str(), rule.target_channel_name.c_str());
    }
    
    return NICC_SUCCESS;
}

nicc_retval_t PipelineRouting::add_dag_edge_rule(const DAGEdgeRule_t& rule) {
    this->_state->dag_edge_rules.push_back(rule);
    NICC_LOG("Added DAG edge rule: %d -> %s::%s", 
            rule.kernel_retval, rule.target_component_id.c_str(), rule.target_channel_name.c_str());
    return NICC_SUCCESS;
}

/**
 * ----------------------ComponentRouting Implementation----------------------
 */ 

nicc_retval_t ComponentRouting::register_local_channel(const std::string& channel_name, nicc::Channel* channel) {
    if (!channel) {
        NICC_ERROR_C("Attempted to register null channel '%s'.", channel_name.c_str());
        return NICC_ERROR;
    }
    
    if (this->_state->local_channels.count(channel_name) > 0) {
        NICC_WARN_C("Channel '%s' already registered in component '%s'. Overwriting.", 
                   channel_name.c_str(), this->_state->component_id.c_str());
    }
    
    this->_state->local_channels[channel_name] = channel;
    NICC_LOG("Registered local channel '%s' in component '%s'.", 
            channel_name.c_str(), this->_state->component_id.c_str());
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting::get_local_channel(const std::string& channel_name) {
    auto it = this->_state->local_channels.find(channel_name);
    if (it != this->_state->local_channels.end()) {
        return it->second;
    }
    return nullptr;
}

nicc_retval_t ComponentRouting::add_retval_mapping(nicc_core_retval_t retval, nicc::Channel* channel) {
    if (!channel) {
        NICC_ERROR_C("Attempted to map retval %d to null channel.", retval);
        return NICC_ERROR;
    }
    
    if (this->_state->retval_to_channel_map.count(retval) > 0) {
        NICC_WARN_C("Retval %d already mapped in component '%s'. Overwriting.", 
                   retval, this->_state->component_id.c_str());
    }
    
    this->_state->retval_to_channel_map[retval] = channel;
    NICC_LOG("Added retval mapping in component '%s': %d -> channel", 
            this->_state->component_id.c_str(), retval);
    return NICC_SUCCESS;
}

nicc_retval_t ComponentRouting::set_default_channel(nicc::Channel* channel) {
    this->_state->default_channel = channel;
    NICC_LOG("Set default channel in component '%s'.", this->_state->component_id.c_str());
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting::lookup_channel(nicc_core_retval_t kernel_retval) {
    // Try retval mapping first
    auto it = this->_state->retval_to_channel_map.find(kernel_retval);
    if (it != this->_state->retval_to_channel_map.end()) {
        NICC_LOG("Component '%s': retval %d mapped to channel", 
                this->_state->component_id.c_str(), kernel_retval);
        return it->second;
    }
    
    // Fall back to default channel
    if (this->_state->default_channel) {
        NICC_LOG("Component '%s': using default channel for retval %d", 
                this->_state->component_id.c_str(), kernel_retval);
        return this->_state->default_channel;
    }
    
    NICC_WARN_C("Component '%s': no mapping found for retval %d", 
               this->_state->component_id.c_str(), kernel_retval);
    return nullptr;
}

} // namespace nicc