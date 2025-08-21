#include "ctrlpath/routing.h"
#include "datapath/channel.h"

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
    NICC_DEBUG_C("Registered component router '%s'.", component_id.c_str());
    return NICC_SUCCESS;
}

nicc_retval_t PipelineRouting::load_from_app_dag(const AppDAG* app_dag) {
    
    if (!app_dag) {
        NICC_ERROR_C("AppDAG pointer is null");
        return NICC_ERROR;
    }
    
    try {
        // Get all components from AppDAG and iterate through them
        // Note: Since AppDAG doesn't expose a method to get all components directly,
        // we iterate through known component names. This could be improved by adding
        // a get_all_components() method to AppDAG in the future.
        
        std::vector<std::string> component_names = {"root", "soc", "dpa", "flow_engine", "decompress", "sha"};
        
        for (const auto& component_name : component_names) {
            const DAGComponent* dag_component = app_dag->get_component_config(component_name);
            
            if (!dag_component) {
                continue; // Component not found, skip
            }
            
            // Check if component has routing entries
            if (dag_component->ctrl_entries.empty()) {
                continue;
            }
            
            // Parse each routing entry
            for (const auto& entry : dag_component->ctrl_entries) {
                auto match_it = entry.find("match");
                auto action_it = entry.find("action");
                
                if (match_it == entry.end() || action_it == entry.end()) {
                    NICC_WARN_C("Skipping incomplete routing entry in component %s", component_name.c_str());
                    continue;
                }
                
                // Create DAG edge rule
                DAGEdgeRule_t rule;
                rule.source_component_name = component_name;
                
                // Parse match condition
                std::string match_str = match_it->second;
                rule.conditions.push_back(__parse_match_condition(match_str));
                
                // Parse action
                std::string action_str = action_it->second;
                rule.action = __parse_action_spec(action_str);
                
                // Fill legacy fields for backward compatibility
                rule.source_component = __component_name_to_type(component_name);
                rule.kernel_retval = __extract_retval_from_conditions(rule.conditions);
                rule.target_component_id = rule.action.target;
                rule.target_channel_name = rule.action.channel_name;
                
                // Add rule to state
                this->_state->dag_edge_rules.push_back(rule);
                
                NICC_DEBUG_C("Added routing rule: %s(%s) -> %s", 
                        component_name.c_str(), match_str.c_str(), action_str.c_str());
            }
        }
        
        return NICC_SUCCESS;
        
    } catch (const std::exception& e) {
        NICC_ERROR_C("Error loading routing configuration from AppDAG: %s", e.what());
        return NICC_ERROR;
    }
}

nicc_retval_t PipelineRouting::build_channel_connections() {

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
        NICC_DEBUG_C("Connected: component_type=%d retval=%d -> %s::%s", 
                rule.source_component, rule.kernel_retval, rule.target_component_id.c_str(), rule.target_channel_name.c_str());
    }
    
    return NICC_SUCCESS;
}

nicc_retval_t PipelineRouting::add_dag_edge_rule(const DAGEdgeRule_t& rule) {
    this->_state->dag_edge_rules.push_back(rule);
    NICC_DEBUG_C("Added DAG edge rule: %d -> %s::%s", 
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
    NICC_DEBUG_C("Registered local channel '%s' in component '%s'.", 
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
    NICC_DEBUG_C("Added retval mapping in component '%s': %d -> channel", 
            this->_state->component_id.c_str(), retval);
    return NICC_SUCCESS;
}

nicc_retval_t ComponentRouting::set_default_channel(nicc::Channel* channel) {
    this->_state->default_channel = channel;
    NICC_DEBUG_C("Set default channel in component '%s'.", this->_state->component_id.c_str());
    return NICC_SUCCESS;
}

nicc::Channel* ComponentRouting::lookup_channel(nicc_core_retval_t kernel_retval) {
    // Try retval mapping first
    auto it = this->_state->retval_to_channel_map.find(kernel_retval);
    if (it != this->_state->retval_to_channel_map.end()) {
        return it->second;
    }
    
    // Fall back to default channel
    if (this->_state->default_channel) {
        return this->_state->default_channel;
    }
    
    NICC_WARN_C("Component '%s': no mapping found for retval %d", 
               this->_state->component_id.c_str(), kernel_retval);
    return nullptr;
}

/**
 * ----------------------PipelineRouting Private Helper Methods----------------------
 */ 

MatchCondition_t PipelineRouting::__parse_match_condition(const std::string& match_str) {
    MatchCondition_t condition;
    
    // Find the operator (=, !=, >, <, etc.)
    size_t eq_pos = match_str.find('=');
    size_t neq_pos = match_str.find("!=");
    size_t gt_pos = match_str.find('>');
    size_t lt_pos = match_str.find('<');
    
    size_t op_pos = std::string::npos;
    std::string op;
    
    if (neq_pos != std::string::npos) {
        op_pos = neq_pos;
        op = "!=";
    } else if (eq_pos != std::string::npos) {
        op_pos = eq_pos;
        op = "=";
    } else if (gt_pos != std::string::npos) {
        op_pos = gt_pos;
        op = ">";
    } else if (lt_pos != std::string::npos) {
        op_pos = lt_pos;
        op = "<";
    }
    
    if (op_pos != std::string::npos) {
        condition.field = match_str.substr(0, op_pos);
        condition.operator_type = op;
        condition.value = match_str.substr(op_pos + op.length());
    } else {
        // No operator found, treat as field existence check
        condition.field = match_str;
        condition.operator_type = "exists";
        condition.value = "";
    }
    
    return condition;
}

ActionSpec_t PipelineRouting::__parse_action_spec(const std::string& action_str) {
    ActionSpec_t action;
    
    // Check for function-style actions like "fwd(host)" or "drop"
    size_t paren_pos = action_str.find('(');
    if (paren_pos != std::string::npos) {
        // Function-style action
        action.action_type = action_str.substr(0, paren_pos);
        
        size_t close_paren_pos = action_str.find(')', paren_pos);
        if (close_paren_pos != std::string::npos) {
            std::string target_spec = action_str.substr(paren_pos + 1, close_paren_pos - paren_pos - 1);
            
            // Check if target has channel specification like "host:channel1"
            size_t colon_pos = target_spec.find(':');
            if (colon_pos != std::string::npos) {
                action.target = target_spec.substr(0, colon_pos);
                action.channel_name = target_spec.substr(colon_pos + 1);
            } else {
                action.target = target_spec;
                action.channel_name = "default";
            }
        }
    } else {
        // Simple action like "drop"
        action.action_type = action_str;
        action.target = "";
        action.channel_name = "";
    }
    
    return action;
}

component_typeid_t PipelineRouting::__component_name_to_type(const std::string& component_name) {
    if (component_name == "soc") {
        return kComponent_SoC;
    } else if (component_name == "dpa") {
        return kComponent_DPA;
    } else if (component_name == "flow_engine") {
        return kComponent_FlowEngine;
    } else if (component_name == "root") {
        // Root is a special component, could map to FlowEngine or a special type
        return kComponent_FlowEngine;
    } else {
        NICC_WARN_C("Unknown component name: %s, defaulting to SoC", component_name.c_str());
        return kComponent_SoC;
    }
}

nicc_core_retval_t PipelineRouting::__extract_retval_from_conditions(const std::vector<MatchCondition_t>& conditions) {
    for (const auto& condition : conditions) {
        if (condition.field == "retval" && condition.operator_type == "=") {
            try {
                return static_cast<nicc_core_retval_t>(std::stoul(condition.value));
            } catch (const std::exception& e) {
                NICC_WARN_C("Failed to parse retval from condition value '%s': %s", 
                           condition.value.c_str(), e.what());
            }
        }
    }
    
    // Default return value if no retval condition found
    return 0;
}

} // namespace nicc