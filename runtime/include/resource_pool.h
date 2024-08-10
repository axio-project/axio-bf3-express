#pragma once

#include <iostream>
#include <map>

#include "common.h"
#include "log.h"
#include "datapath/flow_engine.h"

namespace nicc {

/*!
 *  \brief  mask for enabling dataplane component
 */
#define kComponent_FlowEngine 0x01
#define kComponent_DPA 0x02
#define kComponent_ARM 0x04
#define kComponent_Decompress 0x08
#define kComponent_SHA 0x10

class ResourcePool {
 public:
    ResourcePool(uint16_t enabled_components) : _enabled_components(enabled_components){
        this->_component_map.insert({
            { kComponent_FlowEngine, nullptr },
            { kComponent_DPA, nullptr },
            { kComponent_ARM, nullptr },
            { kComponent_Decompress, nullptr },
            { kComponent_SHA, nullptr } 
        });
        if(unlikely(NICC_SUCCESS != this->__init_components(enabled_components))){
            NICC_ERROR_C("failed to initialize datapath components");
        }
    }

    ~ResourcePool(){
        if(unlikely(NICC_SUCCESS != this->__deinit_components(this->_enabled_components))){
            NICC_ERROR_C("failed to deinitialize datapath components");
        }
    }
    
 private:
    // components
    uint16_t _enabled_components;
    std::map<uint8_t, void*> _component_map;
    Component_FlowEngine *_flow_engine;

    /*!
     *  \brief  initialize on-NIC components
     *  \param  enabled_components  mask of used dataplane component 
     *  \return NICC_SUCCESS for successfully initialization
     */
    nicc_retval_t __init_components(uint16_t enabled_components){
        nicc_retval_t retval = NICC_SUCCESS;
        ComponentConfig_FlowEngine_t fe_config;

        if (enabled_components & kComponent_FlowEngine) {
            this->_flow_engine = new Component_FlowEngine();
            NICC_CHECK_POINTER(this->_flow_engine);

            retval = this->_flow_engine->init(&fe_config);
            if(unlikely(retval != NICC_SUCCESS)){
              NICC_WARN("failed to initialize flow engine");
              goto exit;
        
            }
        }

    exit:
        return retval;
    }

    nicc_retval_t __deinit_components(uint16_t enabled_components){
        nicc_retval_t retval = NICC_SUCCESS;

    exit:
        return retval;
    }
};

} // namespace nicc

