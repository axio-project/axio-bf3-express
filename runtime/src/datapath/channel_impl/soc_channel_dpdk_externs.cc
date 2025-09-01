#include "datapath/channel_impl/soc_channel_dpdk_externs.h"

namespace nicc {

std::mutex g_dpdk_lock;
bool g_dpdk_initialized = false;
bool g_port_initialized[kMaxEthPorts] = {false};
DPDK_SoC_QP::ownership_memzone_t *g_memzone = nullptr;

}  // namespace nicc
