# Control Path - Routing Implementation

This directory contains the simplified control path implementation for packet routing decisions, following datapath architecture patterns.

## Core Files

### 1. `mat.h` / `mat.cc`
- **Purpose**: Base MAT framework (FlowMAT, FlowDomain, etc.)
- **Status**: Existing framework code
- **Usage**: Provides foundational MAT abstractions

### 2. `routing.h` / `routing.cc`  
- **Purpose**: Base routing framework that directly returns datapath Channel pointers
- **Key Classes**:
  - `ComponentRouting`: Base class for component-specific routing
- **Core Function**: `lookup_channel(flow, kernel_retval) -> nicc::Channel*`
- **Architecture**: Follows datapath pattern with state/descriptor structs and virtual methods

### 3. `route_impl/` Directory
- **Purpose**: Component-specific routing implementations
- **Structure**: Each component has its own routing implementation files
- **Current Implementations**:
  - `soc_routing.h` / `soc_routing.cc`: SoC-specific routing logic

## Design Principles

1. **Simplicity**: MAT returns actual datapath Channel pointers, no extra abstraction layers
2. **Separation of Concerns**: 
   - Control path (MAT): Decides WHERE to send packets
   - Data path (Channel): Handles HOW to send packets
3. **Reuse**: Leverages existing datapath Channel infrastructure (RDMA, QP, etc.)

## Usage Flow

```cpp
// 1. In ComponentBlock: Create and configure routing
ComponentRouting_SoC* routing = new ComponentRouting_SoC();
routing->register_datapath_channel("to_dpa", soc_channel);
routing->register_datapath_channel("to_host", host_channel);

// 2. Configure routing rules  
routing->add_retval_mapping(NICC_SUCCESS, "to_host");
routing->add_retval_mapping(NICC_ERROR, "drop");

// 3. In SoCWrapper: Routing is automatically used in __launch()
// User kernel return value determines packet forwarding:
nicc_retval_t ret = msg_handler(packet, user_state);
// SoCWrapper::__forward_packet_with_routing() handles the rest
```

## Configuration

Routing rules are configured through:
- Return value mappings: `kernel_retval -> channel_name`
- Default channel for unmatched packets
- Future: Flow-based routing rules

## Integration Points

- **ComponentBlock**: Creates `ComponentRouting_SoC` and registers datapath channels
- **SoCWrapper**: 
  - Receives `ComponentRouting_SoC*` in context
  - Automatically uses routing in `__launch()` method via `__forward_packet_with_routing()`
- **User Kernels**: Return values influence routing decisions through `add_retval_mapping()`

## Architecture Changes

### SoCWrapper Integration
- Added `ComponentRouting_SoC* routing` to `SoCWrapperContext`
- Added `__forward_packet_with_routing()` method to `SoCWrapper`
- Modified `__launch()` to use routing-based forwarding when available

### Datapath-Style Design
- `ComponentRouting` follows datapath patterns with `RoutingState_t` and `RoutingDesp_t`
- Virtual methods for component-specific implementations
- Clean separation of base functionality and component-specific logic
- Organized structure: base classes in `ctrlpath/`, implementations in `ctrlpath/route_impl/`

## File Structure

```
axio-bf3-express/runtime/
├── include/ctrlpath/
│   ├── mat.h                    # Base MAT framework
│   ├── routing.h                # Base ComponentRouting class
│   ├── route_impl/
│   │   └── soc_routing.h        # ComponentRouting_SoC implementation
│   └── README.md                # This documentation
└── src/ctrlpath/
    ├── mat.cc
    ├── routing.cc               # Base ComponentRouting implementation
    └── route_impl/
        └── soc_routing.cc       # ComponentRouting_SoC implementation
```

## Future Extensions

This structure makes it easy to add new component routing implementations:

- `route_impl/dpa_routing.h/cc` for DPA components
- `route_impl/flow_engine_routing.h/cc` for FlowEngine components
- `route_impl/host_routing.h/cc` for host-side routing

Each implementation can have its own specific logic while inheriting common functionality from `ComponentRouting`.