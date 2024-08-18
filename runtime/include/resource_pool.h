#pragma once

#include <iostream>
#include <map>

#include "common.h"
#include "log.h"
#include "datapath/component.h"
#include "datapath/flow_engine.h"
#include "datapath/dpa.h"

namespace nicc {


class ResourcePool {
 public:
    /*!
     *  \brief  initialization of resource pool
     *  \param  enabled_components  identify which component to be activated
     *  \param  config_map          component id -> component configuration
     */
    ResourcePool(nicc_component_id_t enabled_components, std::map<nicc_component_id_t, void*> &&config_map);

    ~ResourcePool(){
        // TODO: deinit components
    }
    
    /*!
     *  \brief  allocate resource from enabled components
     *  \param  cid     index of the componen to allocate resource on
     *  \param  desp    configration description of the block to be allocated
     *  \param  cb      the allocated component block
     */
    nicc_retval_t allocate(nicc_component_id_t cid, void *desp, ComponentBlock** cb);

 private:
    // maskable code that identify which components are enabled
    nicc_component_id_t _enabled_components;

    // map of all enabled component
    std::map<uint8_t, void*> _component_map;
};

} // namespace nicc
