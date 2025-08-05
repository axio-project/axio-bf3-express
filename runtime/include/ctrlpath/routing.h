#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>

#include "common.h"
#include "log.h"
#include "types.h"
#include "datapath/channel.h" // Include datapath channel
#include "ctrlpath/mat.h"     // Base FlowMAT

namespace nicc {

// Forward declarations
class ComponentRouting;
class ComponentRouting_SoC;

/**
 * ----------------------General structure----------------------
 */ 

/**
 * \brief  routing state for control plane
 */
typedef struct RoutingState {
    // Map channel_id to actual datapath::Channel pointers
    std::map<channel_id_t, nicc::Channel*> datapath_channels;
    
    // Mapping from kernel return value to channel ID
    std::map<nicc_core_retval_t, channel_id_t> retval_to_channel_id_map;
    
    // Default channel ID if no specific mapping is found
    channel_id_t default_channel_id;
} RoutingState_t;

/**
 * \brief  routing descriptor for resource allocation
 */
typedef struct RoutingDesp {
    component_typeid_t component_type;
    const char* config_path;  // Optional configuration file path
} RoutingDesp_t;

/**
 * ----------------------Base Routing Class----------------------
 */ 

/**
 *  \brief  Base routing class for packet forwarding decisions
 *          Follows datapath architecture pattern with virtual methods
 */
class ComponentRouting {
public:
    ComponentRouting(component_typeid_t comp_type) : component_type(comp_type) {
        NICC_CHECK_POINTER(this->_state = new RoutingState_t);
        NICC_CHECK_POINTER(this->_desp = new RoutingDesp_t);
        this->_desp->component_type = comp_type;
        this->_desp->config_path = nullptr;
        
        // Initialize default channel to a "drop" equivalent
        this->_state->default_channel_id = "drop";
    }
    
    virtual ~ComponentRouting() {
        if (this->_state) delete this->_state;
        if (this->_desp) delete this->_desp;
    }

    /**
     *  \brief  register an actual datapath channel with a logical ID
     *  \param  id          logical channel identifier
     *  \param  channel     pointer to actual datapath channel
     *  \return NICC_SUCCESS for successful registration
     */
    virtual nicc_retval_t register_datapath_channel(const channel_id_t& id, nicc::Channel* channel);

    /**
     *  \brief  add a mapping from kernel return value to a logical channel ID
     *  \param  retval              kernel return value
     *  \param  target_channel_id   target logical channel ID
     *  \return NICC_SUCCESS for successful mapping
     */
    virtual nicc_retval_t add_retval_mapping(nicc_core_retval_t retval, const channel_id_t& target_channel_id);

    /**
     *  \brief  set the default channel for unmapped return values
     *  \param  default_id  default channel identifier
     *  \return NICC_SUCCESS for successful setting
     */
    virtual nicc_retval_t set_default_channel(const channel_id_t& default_id);

    /**
     *  \brief  lookup the target datapath channel based on kernel return value and flow
     *  \param  flow            packet flow information
     *  \param  kernel_retval   return value from user kernel
     *  \return pointer to target datapath channel, nullptr if not found
     */
    virtual nicc::Channel* lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval);

    /**
     *  \brief  load routing configuration from file
     *  \param  config_path     path to configuration file
     *  \return NICC_SUCCESS for successful loading
     */
    virtual nicc_retval_t load_config(const std::string& config_path);

    /**
     *  \brief  unregister a datapath channel
     *  \param  id  logical channel identifier to remove
     *  \return NICC_SUCCESS for successful unregistration
     */
    virtual nicc_retval_t unregister_datapath_channel(const channel_id_t& id);

    /**
     *  \brief  remove a return value mapping
     *  \param  retval  kernel return value to remove mapping for
     *  \return NICC_SUCCESS for successful removal
     */
    virtual nicc_retval_t remove_retval_mapping(nicc_core_retval_t retval);

    // Getter methods
    component_typeid_t get_component_type() const { return component_type; }
    const RoutingState_t* get_state() const { return _state; }
    const RoutingDesp_t* get_desp() const { return _desp; }

protected:
    component_typeid_t component_type;
    RoutingState_t* _state;
    RoutingDesp_t* _desp;

    /**
     *  \brief  component-specific channel registration logic (override in subclasses)
     *  \param  id          logical channel identifier
     *  \param  channel     pointer to actual datapath channel
     *  \return NICC_SUCCESS for successful registration
     */
    virtual nicc_retval_t __register_datapath_channel(const channel_id_t& id, nicc::Channel* channel) {
        return NICC_SUCCESS; // Default implementation
    }

    /**
     *  \brief  component-specific lookup logic (override in subclasses)
     *  \param  flow            packet flow information
     *  \param  kernel_retval   return value from user kernel
     *  \return pointer to target datapath channel, nullptr if not found
     */
    virtual nicc::Channel* __lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval) {
        return nullptr; // Default implementation returns nullptr
    }

    /**
     *  \brief  component-specific configuration loading (override in subclasses)
     *  \param  config_path     path to configuration file
     *  \return NICC_SUCCESS for successful loading
     */
    virtual nicc_retval_t __load_config(const std::string& config_path) {
        return NICC_SUCCESS; // Default implementation
    }
};

// Forward declarations for component-specific routing implementations
class ComponentRouting_SoC;

} // namespace nicc