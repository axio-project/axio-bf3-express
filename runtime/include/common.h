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

namespace nicc {

#define _unused(x) ((void)(x))  // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)
#define LOG2VALUE(l) (1UL << (l))

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
enum nicc_component_id_t : uint16_t {
    kComponent_Unknown = 0x00,
    kComponent_FlowEngine = 0x01,
    kComponent_DPA = 0x02,
    kComponent_ARM = 0x04,
    kComponent_Decompress = 0x08,
    kComponent_SHA = 0x10
};
#define NICC_ENABLE_EMPTY_MASK  0x0000
#define NICC_ENABLE_FULL_MASK   0xFFFF


/**
 * ----------------------Simple methods----------------------
 */ 
#if NICC_ENABLE_DEBUG_CHECK
  #define NICC_CHECK_POINTER(ptr)   assert((ptr) != nullptr);
  #define NICC_ASSERT(condition)    assert((condition));
#else // NICC_ENABLE_DEBUG_CHECK
    #define NICC_CHECK_POINTER(ptr)  (ptr);
    #define NICC_ASSERT(condition)   (condition);
#endif // NICC_ENABLE_DEBUG_CHECK

} // namespace nicc
