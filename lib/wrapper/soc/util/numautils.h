#pragma once
#include <vector>
#include <stdlib.h>
#include <thread>

namespace nicc {

/// Return the number of logical cores per NUMA node
size_t num_lcores_per_numa_node();

/// Return a list of logical cores in \p numa_node
std::vector<size_t> get_lcores_for_numa_node(size_t numa_node);

/// Bind this thread to the core with index numa_local_index on the socket =
/// numa_node
size_t bind_to_core(std::thread &thread, size_t numa_node,
                         size_t numa_local_index);

/// Reset this process's core mask to be all cores
void clear_affinity_for_process();

size_t get_global_index(size_t numa_node, size_t numa_local_index);
double get_cpu_freq_ghz(size_t core_idx);
bool is_cpu_freq_max(size_t core_idx);
void set_cpu_freq_max(size_t core_idx);
void set_cpu_freq_normal(size_t core_idx);

}  // namespace nicc
