# Axio BF3 Express

> **Note**: This project is under active development. Documentation is being continuously updated.

## Overview

Axio BF3 Express is a high-performance execution platform for NVIDIA BlueField 3 DPUs. It enables users to run application DAGs with pipeline parallelism to achieve optimal throughput (up to 400Gbps in ideal conditions). The platform provides comprehensive programmability across the entire stack, including:

- Reconfigurable match-action engines
- Data path accelerators (DPAs)
- On-board SoC
- ASIC-based accelerators

## Programming Model

Axio BF3 Express uses a kernel and wrapper programming model, similar to GPU programming. The model consists of two main components:

1. **Kernels**: Function binaries that correspond to application DAG nodes
2. **Specification File**: Defines:
   - Target DPU component for each kernel
   - Inter-function dependencies (representing DAG edges)

The platform encapsulates each DAG node into a wrapper, which manages the corresponding kernel binary for parallel execution.

## Test Environment

### Hardware
- Intel® Xeon® Gold 6530 Processor
- Two-port 400GbE BlueField 3, PCIe 5.0 x 16
- Host Memory: 512GB DDR5 3200MT/s
- DPU Memory: 32GB DDR5 5200MT/s

### Software
- Ubuntu 22.04
- Linux kernel 5.15.x
- DPDK 22.11.x
- NVIDIA DOCA 2.9.2

## Installation

### Prerequisites

Axio BF3 Express depends on NVIDIA DOCA, open vswitch, and rdma-core. Please make sure DOCA has been installed, while others can be installed by running the following commands:
```bash
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/axio-project/axio-bf3-express.git

# Install required packages
sudo apt-get install binutils-dev meson nlohmann-json3-dev libyara-dev
```

### Build Process

1. Build the Axio BF3 Express library (includes kernel wrapper compilation):
```bash
# Clean build
bash build.sh -t lib -e rdma_simple -c

# Build library
bash build.sh -t lib -e rdma_simple
```

2. Build the runtime environment:
```bash
# Clean build
bash build.sh -t runtime -c

# Build runtime
bash build.sh -t runtime
```

## Running with Axio-Emulator

Axio BF3 Express handles RDMA traffic (RC mode) and is compatible with any RDMA application. The following example uses Axio-emulator to generate RDMA traffic. For more details about Axio-emulator, please refer to the [Axio-emulator documentation](https://github.com/axio-project/axio-emulator).

### Traffic Flow
```
Remote Host (Axio-emulator) -> Local BF3 (Axio BF3 Express) -> Local Host (Axio-emulator)
```

### Execution Steps

1. On Local Host:
```bash
sudo ./build/axio > tmp/temp.log
```

2. On Local BF3 SoC:
```bash
sudo ./runtime/build/runtime/nicc_runtime
```

3. On Remote Host:
```bash
sudo ./build/axio > tmp/temp.log
```

### RDMA Simple Example

The offloaded DAG in the RDMA simple example follows this workflow:
1. DPA core receives RDMA requests and stores them in DPA memory
2. Requests are sent to a SoC core for processing
3. SoC core redirects requests to local host through local RDMA
4. Local Axio-emulator echoes back requests through: local BF3 SoC -> local BF3 DPA -> remote host

