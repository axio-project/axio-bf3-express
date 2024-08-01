/**
 * @file common.h
 * @brief Common header file with convenience definitions
 */
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

/**
 * ----------------------Server constants----------------------
 */ 
static constexpr size_t kMaxPhyPorts = 2;
static constexpr size_t kMaxNumaNodes = 2;
static constexpr size_t kMaxQueuesPerPort = 8;
static constexpr size_t kHugepageSize = (2 * 1024 * 1024);  ///< Hugepage size

/**
 * ----------------------Perf Test constants----------------------
 */ 
#define PERF_TEST 1       // 1: enable perf test, 0: disable perf test
#define PERF_TEST_LAT 1
#define PERF_TEST_THR 1
#define PERF_TEST_LAT_MIN_MAX 1
#define PERT_TEST_MBUF_RANGE 1

/**
 * ----------------------Node Type----------------------
 */
#define CLIENT 0
#define SERVER 1

#define NODE_TYPE CLIENT

/**
 * ----------------------App behaviour control----------------------
 */

/*!
 *  \note     T-APP behavior:
 *            [1] recv a huge packet;
 *            [2] scan the huge packet;
 *            [3] free huge packet and apply a new mbuf  
 *            [3] generate a small response and return
 *  \example  distributed file system, e.g., GFS
 */
#define T_APP 0

/*!
  *  \note     L-APP behavior:
  *            [1] recv a small packet;
  *            [2] scan the small packet;
  *            [3] return a small response
  *  \example  RPC server, e.g., eRPC
  */
#define L_APP 1

  /*!
   *  \note     M-APP behavior:
   *            [1] recv a small packet;
   *            [2] scan an external memory area;
   *            [3] return a small response
   *  \example  in-memory key-value database, e.g., Redis
   */
#define M_APP 2

  /*!
   *  \note     FILE_DECOMPRESS behavior:
   *            [1] recv a large message;
   *            [2] perform deflate decompression;
   *            [3] return a small response
   *  \example  file system, e.g., Redis
   */
#define FILE_DECOMPRESS 3

#define APP_BEHAVIOR FILE_DECOMPRESS

#define EnableInflyMessageLimit true
static constexpr uint64_t kInflyMessageBudget = 8192;

#define CEIL_2(x)    std::pow(2, std::ceil(std::log(x)/std::log(2)))
static constexpr size_t kMemoryAccessRangePerPkt    = KB(1);
static constexpr size_t kStatefulMemorySizePerCore  = MB(4);

#define ApplyNewMbuf false


/**
 * ----------------------OneStage modes----------------------
 */
/// Available types: kTxNICType, kTxDispatcherType, kTxApplicationType, kRxNICType, kRxDispatcherType, kRxApplicationType
// #define OneStage kRxApplicationType
/*!
 *  \note [xinyang] for app and dispatcher stage, max value is kWsQueueSize - 1, for nic stage, max 
                    value is kNumTxRingEntries
 *  \note [zhuobin] if this number exceed the size of mempool cache size, then the apply_bulk will
 *                  leak to the normal mempool mbufs, which might cause high cache miss rate
 */
#define FlowSize 256

/**
 * ----------------------Dispatcher modes----------------------
 */ 
// #define RoceMode 0
#define DpdkMode 1

#ifdef RoceMode
  #define DISPATCHER_TYPE RoceDispatcher
  #define MEM_REG_TYPE Buffer
#elif DpdkMode
  #define DISPATCHER_TYPE DpdkDispatcher
  #define MEM_REG_TYPE rte_mbuf
#endif

enum dispatcher_handler_type_t : uint8_t {
  kRxDispatcherHandler_Empty = 0,
  kRxDispatcherHandler_Echo
};
#define kRxDispatcherHandler  kRxDispatcherHandler_Empty

/**
 * ----------------------General constants----------------------
 */ 

#define DPERF_LOG_LEVEL 3

static constexpr uint8_t kWorkspaceTypeNum = 3;
static constexpr uint8_t kInvaildWorkspaceType = std::pow(2, kWorkspaceTypeNum);
static constexpr uint8_t kWorkspaceMaxNum = 16;
static constexpr uint8_t kInvalidWsId = kWorkspaceMaxNum + 1;
static constexpr size_t  kWsQueueSize = 4096;    // Queue size must be power of two

/// Parameters for datapath pipeline
static constexpr uint8_t kMaxWorkloadNum = kWorkspaceMaxNum;
static constexpr uint8_t kInvalidWorkloadType = kMaxWorkloadNum + 1;

static constexpr uint8_t kTxNICType = 0;
static constexpr uint8_t kTxDispatcherType = 1;
static constexpr uint8_t kTxApplicationType = 2;
static constexpr uint8_t kRxNICType = 3;
static constexpr uint8_t kRxDispatcherType = 4;
static constexpr uint8_t kRxApplicationType = 5;

/**
 * ----------------------Simple methods----------------------
 */ 
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
}