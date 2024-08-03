/**
 * @file db.h
 * @brief The dataplane flow entry database on each components
 */

#pragma once

#include <iostream>
#include <stdint.h>

#include "common.h"

namespace nicc {

/*!
 *  \brief  flow entry for runtime dispatching
 */
typedef struct nicc_flow_entry {
    uint8_t *match_fields;
} nicc_flow_entries_t;

/*!
 *  \brief  set of flow entries for runtime dispatching
 */
class FlowDB {
 public:
    FlowDB(){}

    ~FlowDB(){}

 private:
    
}

} // namespace nicc
