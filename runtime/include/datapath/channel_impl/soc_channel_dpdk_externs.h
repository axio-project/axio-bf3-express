/**
 * @file soc_channel_dpdk_externs.h
 *
 * @brief Externs for Channel_SoC DPDK implementation. These globals are shared
 * among different Channel_SoC objects
 */
 #pragma once
 
 #include <atomic>
 #include <set>
 #include "common.h"
#include "common/soc_queue.h"
 
 namespace nicc {
 /**
  * ----------------------Global parameters (shared by all threads)----------------------
  */
 extern std::mutex g_dpdk_lock;
 extern bool g_dpdk_initialized;
 // Use a fixed size instead of RTE_MAX_ETHPORTS to avoid DPDK header dependency
static constexpr size_t kMaxEthPorts = 32;  // Should be sufficient for most systems
extern bool g_port_initialized[kMaxEthPorts];
 extern DPDK_SoC_QP::ownership_memzone_t *g_memzone;
 }  // namespace nicc
 