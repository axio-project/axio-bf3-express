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
#include <cassert>

#include <doca_error.h>

#include "debug.h"

namespace nicc {
    

/**
 * \brief  basic descriptor of the component block,
 *         using for allocating / deallocating component blocks
 *         from the resource pool
 * \note   [1] this structure should be inherited and 
 *         implenented by each component block
 *         [2] this structure could be used for describing
 *         either root component or component block
 */
typedef struct ComponentBaseDesp {
    // quota of the component
    uint64_t quota;
    // name of the component
    char block_name[64];
} ComponentBaseDesp_t;


/**
 * \brief  basic state of the component block,
 *         using for control plane, including rescheduling,
 *         inter-block communication channel, and MT
 * \note   [1] this structure should be inherited and
 *         implenented by each component block
 *         [2] this structure could be used for describing
 *         either root component or component block
 */
typedef struct ComponentBaseState {
    // quota of the component
    uint64_t quota;
} ComponentBaseState_t;


/**
 *  \brief  basic state of the function register 
 *          into the component block, 
 *          using for running the function on this component block
 *  \note   [1] this structure should be inherited and 
 *          implenented by each component block
 *          [2] func state can be modify by other component blocks
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
#define K(x) KB(x)
#define M(x) MM(x)
#define G(x) GB(x)
#define LOG2VALUE(l)    (1UL << (l))
#define LOG2(n)         (std::log2((double)(n)))

/**
 * ----------------------Server constants----------------------
 */ 
static constexpr size_t kMaxPhyPorts = 2;
static constexpr size_t kMaxNumaNodes = 2;
static constexpr size_t kMaxQueuesPerPort = 1;
static constexpr size_t kHugepageSize = (2 * 1024 * 1024);  ///< Hugepage size
static constexpr uint8_t kSoCWorkspaceMaxNum = 16;    // max number of workspaces in SoC, including dispatcher and worker
static constexpr uint16_t kDPAWorkspaceMaxNum = 128;    // max number of workspaces in DPA, including dispatcher and worker

// return values
enum nicc_retval_t {
    NICC_SUCCESS = 0,
    NICC_ERROR_NOT_IMPLEMENTED,
    NICC_ERROR_NOT_FOUND,
    NICC_ERROR_EXSAUSTED,
    NICC_ERROR_DUPLICATED,
    NICC_ERROR_HARDWARE_FAILURE,
    NICC_ERROR_MEMORY_FAILURE,
    NICC_ERROR
};


/*!
 *  \brief  mask for enabling dataplane component
 */
using component_typeid_t = uint16_t;
static constexpr component_typeid_t kComponent_Unknown = 0x00;
static constexpr component_typeid_t kComponent_FlowEngine = 0x01;
static constexpr component_typeid_t kComponent_DPA = 0x02;
static constexpr component_typeid_t kComponent_SoC = 0x04;
static constexpr component_typeid_t kComponent_Decompress = 0x08;
static constexpr component_typeid_t kComponent_SHA = 0x10;
static constexpr component_typeid_t kRemote_Host = 0x1000;
static constexpr component_typeid_t kLocal_Host = 0x2000;
static constexpr component_typeid_t NICC_ENABLE_EMPTY_MASK = static_cast<component_typeid_t>(0x0000);
static constexpr component_typeid_t NICC_ENABLE_FULL_MASK = static_cast<component_typeid_t>(0xFFFF);


/**
 * ----------------------Scope Exit----------------------
 */
template <typename T>
class ScopeExit
{
 public:
    ScopeExit(T t) : t(t) {}
    ~ScopeExit() { t(); }
    T t;
};

template <typename T>
ScopeExit<T> MoveScopeExit(T t) {
    return ScopeExit<T>(t);
};

#define __ANONYMOUS_VARIABLE_DIRECT(name, line) name##line
#define __ANONYMOUS_VARIABLE_INDIRECT(name, line) __ANONYMOUS_VARIABLE_DIRECT(name, line)
#define NICC_SCOPE_EXIT(func) const auto __ANONYMOUS_VARIABLE_INDIRECT(EXIT, __LINE__) = MoveScopeExit([=](){func;})

/**
 * ----------------------Simple methods----------------------
 */ 
#if NICC_RUNTIME_DEBUG_CHECK
  #define NICC_CHECK_POINTER(ptr)       assert((ptr) != nullptr);
  #define NICC_ASSERT(condition)        assert((condition));
  #define NICC_CHECK_BOUND(val, bound)  assert(val <= bound);
#else // NICC_RUNTIME_DEBUG_CHECK
    #define NICC_CHECK_POINTER(ptr)         _unused (ptr);
    #define NICC_ASSERT(condition)          _unused (condition);
    #define NICC_CHECK_BOUND(val, bound)    _unused (val);
#endif // NICC_RUNTIME_DEBUG_CHECK

#define NICC_STATIC_ASSERT(condition, report)   \
    static_assert((condition), report)


static inline void rt_assert(bool condition, std::string throw_str, char *s) {
  if (unlikely(!condition)) {
    fprintf(stderr, "%s %s\n", throw_str.c_str(), s);
    exit(-1);
  }
}

static inline void rt_assert(bool condition, const char *throw_str) {
  if (unlikely(!condition)) {
    fprintf(stderr, "%s\n", throw_str);
    exit(-1);
  }
}

static inline void rt_assert(bool condition, std::string throw_str) {
  if (unlikely(!condition)) {
    fprintf(stderr, "%s\n", throw_str.c_str());
    exit(-1);
  }
}

static inline void rt_assert(bool condition) {
  if (unlikely(!condition)) {
    fprintf(stderr, "Error\n");
    assert(false);
    exit(-1);
  }
}

/// Check a condition at runtime. If the condition is false, print error message
/// and exit.
static inline void exit_assert(bool condition, std::string error_msg) {
  if (unlikely(!condition)) {
    fprintf(stderr, "%s. Exiting.\n", error_msg.c_str());
    fflush(stderr);
    exit(-1);
  }
}

/**
 * ----------------------Print related----------------------
 */ 
const std::string RESET = "\033[0m";
const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";

} // namespace nicc
