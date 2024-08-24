#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>

#include <doca_error.h>

#include "debug.h"

namespace nicc {

/**
 * \brief  basic descriptor of the component
 * \note   [1] this structure should be inherited and 
 *         implenented by each component
 *         [2] this structure could be used for describing
 *         either root component or component block
 */
typedef struct ComponentBaseDesp {
    // quota of the component
    uint64_t quota;
} ComponentBaseDesp_t;

/**
 * \brief  basic state of the component
 * \note   [1] this structure should be inherited and
 *         implenented by each component
 *         [2] this structure could be used for describing
 *         either root component or component block
 */
typedef struct ComponentBaseState {
    // quota of the component
    uint64_t quota;
} ComponentBaseState_t;

/*!
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncBaseState {
    // basic state
    uint8_t mock_state;
} ComponentFuncBaseState_t;

#define _unused(x) ((void)(x))  // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)
#define LOG2VALUE(l)    (1UL << (l))
#define LOG2(n)         (std::log2((double)(n)))

// return values
enum nicc_retval_t {
    NICC_SUCCESS = 0,
    NICC_ERROR_NOT_IMPLEMENTED,
    NICC_ERROR_NOT_FOUND,
    NICC_ERROR_EXSAUSTED,
    NICC_ERROR_DUPLICATED,
    NICC_ERROR_HARDWARE_FAILURE,
    NICC_ERROR
};


/*!
 *  \brief  mask for enabling dataplane component
 */
using component_typeid_t = uint16_t;
static constexpr component_typeid_t kComponent_Unknown = 0x00;
static constexpr component_typeid_t kComponent_FlowEngine = 0x01;
static constexpr component_typeid_t kComponent_DPA = 0x02;
static constexpr component_typeid_t kComponent_ARM = 0x04;
static constexpr component_typeid_t kComponent_Decompress = 0x08;
static constexpr component_typeid_t kComponent_SHA = 0x10;
static constexpr component_typeid_t NICC_ENABLE_EMPTY_MASK = static_cast<component_typeid_t>(0x0000);
static constexpr component_typeid_t NICC_ENABLE_FULL_MASK = static_cast<component_typeid_t>(0xFFFF);

/**
 * ----------------------Simple methods----------------------
 */ 
#if NICC_RUNTIME_DEBUG_CHECK
  #define NICC_CHECK_POINTER(ptr)   assert((ptr) != nullptr);
  #define NICC_ASSERT(condition)    assert((condition));
#else // NICC_RUNTIME_DEBUG_CHECK
    #define NICC_CHECK_POINTER(ptr)  _unused (ptr);
    #define NICC_ASSERT(condition)   _unused (condition);
#endif // NICC_RUNTIME_DEBUG_CHECK

} // namespace nicc
