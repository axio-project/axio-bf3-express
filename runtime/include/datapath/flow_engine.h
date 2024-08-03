#pragma once

#include <iostream>

#include "datapath/components.h"

namespace nicc {

/*!
 *  \brief  configuration of flow engine
 */
typedef struct ComponentConfig_FlowEngine {
    ComponentBaseConfig_t base_config;
} ComponentConfig_FlowEngine_t;

class Component_FlowEngine : public Component {
 public:
    Component_FlowEngine() : Component() {}
    ~Component_FlowEngine(){}

 private:
    
};

} // namespace nicc
