#pragma once

#include "common.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
namespace nicc {

struct RuleTable {
  // map from workload type to the corresponding destination workspace id
  std::unordered_map<uint8_t, std::vector<uint8_t>> table;

  // budget for issuing infly message of each workload
  std::unordered_map<uint8_t, uint64_t> infly_budget;

  void add_route(uint8_t type, uint8_t ws_id) {
      table[type].push_back(ws_id);

      if(infly_budget.count(type) == 0){
        infly_budget[type] = kInflyMessageBudget;
      }
  }

  bool apply_infly_budget(uint8_t type, uint64_t apply_size){
    bool has_budget = infly_budget[type] >= apply_size;
    if(unlikely(has_budget)){
      infly_budget[type] -= apply_size;
    }
    return has_budget;
  }

  uint64_t get_infly_budget(uint8_t type){
    return infly_budget[type];
  }

  inline void return_infly_budget(uint8_t type, uint64_t return_size = 1){
    infly_budget[type] += return_size;
  }

  void remove_route(uint8_t type, uint8_t ws_id) {
      auto& ws_ids = table[type];
      auto it = std::find(ws_ids.begin(), ws_ids.end(), ws_id);
      if (it != ws_ids.end()) {
        ws_ids.erase(it);
      }
  }

  std::vector<uint8_t> get_ws_ids(uint8_t type) {
      return table[type];
  }

  uint8_t rr_select(uint8_t type) {
      auto& ws_ids = table[type];
      return ws_ids[select_idx++ % ws_ids.size()];
  }
  size_t select_idx = 0;
};

} // namespace nicc