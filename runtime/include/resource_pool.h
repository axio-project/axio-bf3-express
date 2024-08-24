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
    ResourcePool(
        component_typeid_t enabled_components, 
        std::map<component_typeid_t, ComponentBaseDesp_t*> &&config_map
    );

    ~ResourcePool(){
        // TODO: deinit components
    }


    /*!
     *  \brief  allocate resource from enabled components
     *  \param  cid     index of the component to allocate resource on
     *  \param  desp    configration description of the block to be allocated
     *  \param  app_cxt context of the application
     *  \param  cb      the allocated component block
     */
    nicc_retval_t allocate(
        component_typeid_t cid, ComponentBaseDesp_t *desp, AppContext *app_cxt, ComponentBlock** cb
    );


    /*!
     *  \brief  return back resource to enabled components
     *  \param  cid     index of the component to be returned
     *  \param  cb      the component block to be returned
     *  \param  app_cxt application context which the component block belongs to
     *  \return NICC_SUCCESS for succesfully returning
     */
    nicc_retval_t deallocate(
        component_typeid_t cid, ComponentBlock* cb, AppContext* app_cxt
    );

 private:
    // maskable code that identify which components are enabled
    component_typeid_t _enabled_components;

    // map of all enabled component
    std::map<component_typeid_t, Component*> _component_map;
};

} // namespace nicc
