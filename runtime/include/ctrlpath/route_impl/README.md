# Component Routing Implementations

This directory contains component-specific routing implementations that inherit from the base `ComponentRouting` class.

## Current Implementations

### SoC Routing (`soc_routing.h/cc`)
- **Class**: `ComponentRouting_SoC`
- **Purpose**: Routing logic for SoC (System on Chip) components
- **Integration**: Used by `SoCWrapper` for packet forwarding decisions
- **Features**:
  - Kernel return value-based routing
  - SoC-specific channel validation
  - JSON configuration support

## Adding New Component Routing

To add routing for a new component (e.g., DPA), follow this pattern:

### 1. Create Header File (`dpa_routing.h`)
```cpp
#pragma once
#include "ctrlpath/routing.h"

namespace nicc {

class ComponentRouting_DPA : public ComponentRouting {
public:
    ComponentRouting_DPA() : ComponentRouting(kComponent_DPA) {
        NICC_LOG("ComponentRouting_DPA initialized for DPA component");
    }
    
    virtual ~ComponentRouting_DPA() = default;

    // DPA-specific methods
    nicc_retval_t forward_packet_after_kernel(
        Buffer* packet, 
        nicc_core_retval_t kernel_retval, 
        nicc::Channel* default_tx_channel
    );

protected:
    // Override virtual methods for DPA-specific logic
    nicc_retval_t __register_datapath_channel(const channel_id_t& id, nicc::Channel* channel) override;
    nicc::Channel* __lookup_channel(flow_t& flow, nicc_core_retval_t kernel_retval) override;
    nicc_retval_t __load_config(const std::string& config_path) override;
};

} // namespace nicc
```

### 2. Create Implementation File (`dpa_routing.cc`)
```cpp
#include "ctrlpath/route_impl/dpa_routing.h"

namespace nicc {

nicc_retval_t ComponentRouting_DPA::forward_packet_after_kernel(
    Buffer* packet, 
    nicc_core_retval_t kernel_retval, 
    nicc::Channel* default_tx_channel
) {
    // DPA-specific forwarding logic
    // ...
}

// Implement other virtual methods...

} // namespace nicc
```

### 3. Integration Points
- Include the new header in the DPA wrapper or component block
- Create instances in `ComponentBlock_DPA`
- Configure routing rules specific to DPA requirements

## Architecture Benefits

This structure provides:
- **Modularity**: Each component has its own routing logic
- **Extensibility**: Easy to add new components without affecting existing ones
- **Maintainability**: Clear separation of concerns
- **Consistency**: All implementations follow the same base interface