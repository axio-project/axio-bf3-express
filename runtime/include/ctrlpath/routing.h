#pragma once
#include "common.h"
#include "log.h"
#include "types.h"
#include "datapath/channel.h" // Include datapath channel
#include "ctrlpath/mat.h"     // Base FlowMAT
#include "utils/app_dag.h"

namespace nicc {

// Forward declarations
class ComponentRouting;
class PipelineRouting;

/**
 * ----------------------Pipeline-Level Routing Structures----------------------
 */ 

/**
 * \brief  Match condition for routing rules
 */
typedef struct MatchCondition {
    std::string field;          // Field name (e.g., "retval", "udp.sport", "ipv4.sip")
    std::string operator_type;  // Operator (e.g., "=", "!=", ">", "<", "in")
    std::string value;          // Expected value (e.g., "1", "10086", "192.168.1.1")
} MatchCondition_t;

/**
 * \brief  Action specification for routing rules
 */
typedef struct ActionSpec {
    std::string action_type;    // Action type (e.g., "fwd", "drop", "mirror")
    std::string target;         // Target for action (e.g., "host", "soc", "dpa")
    std::string channel_name;   // Specific channel name (optional)
} ActionSpec_t;

/**
 * \brief  Enhanced DAG edge rule with flexible match conditions
 */
typedef struct DAGEdgeRule {
    std::string source_component_name;       // Source component name (e.g., "soc", "root")
    std::vector<MatchCondition_t> conditions; // Match conditions (AND logic between them)
    ActionSpec_t action;                     // Action to take when matched
    
    // Legacy fields for backward compatibility
    component_typeid_t source_component;      // Source component type (derived from name)
    nicc_core_retval_t kernel_retval;        // Kernel return value (derived from conditions)
    std::string target_component_id;         // Target component identifier (derived from action)
    std::string target_channel_name;         // Specific channel name (derived from action)
} DAGEdgeRule_t;

/**
 * \brief  Pipeline routing state for managing global DAG
 */
typedef struct PipelineRoutingState {
    // Global DAG edge rules: (source_comp, retval) -> (target_comp, channel)
    std::vector<DAGEdgeRule_t> dag_edge_rules;
    
    // Component ID to ComponentRouting mapping
    std::map<std::string, ComponentRouting*> component_routers;
    
    // Default routing policies
    std::map<component_typeid_t, std::string> default_targets;
} PipelineRoutingState_t;

/**
 * ----------------------Component-Level Routing Structures----------------------
 */ 

/**
 * \brief  Component routing state for local retval-to-channel mapping
 */
typedef struct ComponentRoutingState {
    // Mapping from kernel return value to local channel
    std::map<nicc_core_retval_t, nicc::Channel*> retval_to_channel_map;
    
    // Local channels managed by this component (for registration only)
    std::map<std::string, nicc::Channel*> local_channels;
    
    // Component metadata
    component_typeid_t component_type;
    std::string component_id;
    
    // Default channel for unmapped return values
    nicc::Channel* default_channel;
} ComponentRoutingState_t;

/**
 * ----------------------Pipeline Routing (Global DAG Management)----------------------
 */ 

/**
 *  \brief  Pipeline-level routing manager for global DAG edge rules
 *          Manages routing between components, not within components
 */
class PipelineRouting {
public:
    PipelineRouting() {
        NICC_CHECK_POINTER(this->_state = new PipelineRoutingState_t);
    }
    
    virtual ~PipelineRouting() {
        if (this->_state) delete this->_state;
    }

    /**
     *  \brief  Register a component router for DAG routing
     *  \param  component_id    unique identifier for the component
     *  \param  router          pointer to component's routing interface
     *  \return NICC_SUCCESS for successful registration
     */
    nicc_retval_t register_component_router(const std::string& component_id, ComponentRouting* router);

    /**
     *  \brief  Load routing configuration from parsed AppDAG
     *  \param  app_dag         pointer to parsed AppDAG containing routing rules
     *  \return NICC_SUCCESS for successful loading
     */
    nicc_retval_t load_from_app_dag(const AppDAG* app_dag);

    /**
     *  \brief  Build channel connections based on DAG edge rules
     *          Called after all components have registered their local channels
     *  \return NICC_SUCCESS for successful connection building
     */
    nicc_retval_t build_channel_connections();

    /**
     *  \brief  Add a DAG edge rule: source_component(retval) -> target_component(channel)
     *  \param  rule            DAG edge rule to add
     *  \return NICC_SUCCESS for successful addition
     */
    nicc_retval_t add_dag_edge_rule(const DAGEdgeRule_t& rule);

private:
    /**
     *  \brief  Parse match condition string (e.g., "retval=1", "udp.sport=10086")
     *  \param  match_str       match condition string to parse
     *  \return parsed MatchCondition_t structure
     */
    MatchCondition_t __parse_match_condition(const std::string& match_str);

    /**
     *  \brief  Parse action string (e.g., "fwd(host)", "drop", "fwd(soc)")
     *  \param  action_str      action string to parse
     *  \return parsed ActionSpec_t structure
     */
    ActionSpec_t __parse_action_spec(const std::string& action_str);

    /**
     *  \brief  Convert component name to component type ID
     *  \param  component_name  component name string
     *  \return corresponding component_typeid_t
     */
    component_typeid_t __component_name_to_type(const std::string& component_name);

    /**
     *  \brief  Extract retval from match conditions for backward compatibility
     *  \param  conditions      list of match conditions
     *  \return extracted retval or default value
     */
    nicc_core_retval_t __extract_retval_from_conditions(const std::vector<MatchCondition_t>& conditions);

public:

protected:
    PipelineRoutingState_t* _state;
};

/**
 * ----------------------Component Routing (Local Channel Management)----------------------
 */ 

/**
 *  \brief  Component-level routing for local channel management
 *          Only manages channels within the component, routing decisions come from PipelineRouting
 */
class ComponentRouting {
public:
    ComponentRouting(component_typeid_t comp_type, const std::string& comp_id) {
        NICC_CHECK_POINTER(this->_state = new ComponentRoutingState_t);
        this->_state->component_type = comp_type;
        this->_state->component_id = comp_id;
        this->_state->default_channel = nullptr;
    }
    
    virtual ~ComponentRouting() {
        if (this->_state) delete this->_state;
    }

    /**
     *  \brief  Register a local datapath channel
     *  \param  channel_name    name of the channel within this component
     *  \param  channel         pointer to actual datapath channel
     *  \return NICC_SUCCESS for successful registration
     */
    virtual nicc_retval_t register_local_channel(const std::string& channel_name, nicc::Channel* channel);

    /**
     *  \brief  Get a local channel by name
     *  \param  channel_name    name of the channel to retrieve
     *  \return pointer to channel, nullptr if not found
     */
    virtual nicc::Channel* get_local_channel(const std::string& channel_name);

    /**
     *  \brief  Add mapping from kernel return value to local channel
     *  \param  retval          kernel return value
     *  \param  channel         target local channel
     *  \return NICC_SUCCESS for successful mapping
     */
    virtual nicc_retval_t add_retval_mapping(nicc_core_retval_t retval, nicc::Channel* channel);

    /**
     *  \brief  Set default channel for unmapped return values
     *  \param  channel         default channel
     *  \return NICC_SUCCESS for successful setting
     */
    virtual nicc_retval_t set_default_channel(nicc::Channel* channel);

    /**
     *  \brief  Lookup local channel by kernel return value (main routing method)
     *  \param  kernel_retval   return value from user kernel
     *  \return pointer to target local channel, nullptr if not found
     */
    virtual nicc::Channel* lookup_channel(nicc_core_retval_t kernel_retval);

    // Getter methods
    component_typeid_t get_component_type() const { return _state->component_type; }
    const std::string& get_component_id() const { return _state->component_id; }
    const ComponentRoutingState_t* get_state() const { return _state; }

protected:
    ComponentRoutingState_t* _state;
};

// Forward declarations for component-specific routing implementations
class ComponentRouting_SoC;

} // namespace nicc