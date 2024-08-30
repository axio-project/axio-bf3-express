#pragma once

#include <iostream>
#include <map>

#include "common.h"
#include "log.h"
#include "resources/component.h"
#include "resources/component_impl/dpa_component.h"

namespace nicc {

class ResourcePool {
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    /**
     *  \brief  initialization of resource pool
     *  \param  enabled_components  identify which component to be activated
     *  \param  config_map          component id -> component configuration,
     *                              the configuration is a component 
     *                              descriptor, recording total component 
     *                              hardware resources
     */
    ResourcePool(
        component_typeid_t enabled_components, 
        std::map<component_typeid_t, ComponentBaseDesp_t*> &&config_map
    );

    ~ResourcePool(){
        // TODO: deinit components
    }

    /**
     *  \brief  allocate a component block from corresponding component
     *  \param  cid     component type
     *  \param  desp    description of the block to be allocated
     *  \param  cb      the allocated component block
     *  \return NICC_SUCCESS for successful allocation
     *  \note   an allocate_block() function is defined in each component,
     *          called by this function
     */
    nicc_retval_t allocate(
        component_typeid_t cid, ComponentBaseDesp_t *desp, ComponentBlock* cb
    );


    /*!
     *  \brief  return back resource to enabled components
     *  \param  cid     index of the component to be returned
     *  \param  cb      the component block to be returned
     *  \return NICC_SUCCESS for succesfully returning
     */
    nicc_retval_t deallocate(
        component_typeid_t cid, ComponentBlock* cb
    );
/**
 * ----------------------Internel Parameters----------------------
 */ 
 private:
    // maskable code that identify which components are enabled
    component_typeid_t _enabled_components;

    // map of all enabled component
    std::map<component_typeid_t, Component*> _component_map;
};

} // namespace nicc
