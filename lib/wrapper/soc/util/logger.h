#pragma once

/***************************************************************************
 *   Copyright (C) 2008 by H-Store Project                                 *
 *   Brown University                                                      *
 *   Massachusetts Institute of Technology                                 *
 *   Yale University                                                       *
 *                                                                         *
 *   This software may be modified and distributed under the terms         *
 *   of the MIT license.  See the LICENSE file for details.                *
 *                                                                         *
 ***************************************************************************/

/**
 * @file logger.h
 * @brief Logging macros that can be optimized out
 * @author Hideaki and Anuj, modified by Xinyang
 */

#include <chrono>
#include <string>

namespace nicc {

// Log levels: higher means more verbose
#define DPERF_LOG_LEVEL_OFF 0
#define DPERF_LOG_LEVEL_ERROR 1  // Only fatal conditions
#define DPERF_LOG_LEVEL_WARN 2  // Conditions from which it's possible to recover
#define DPERF_LOG_LEVEL_INFO 3  // Reasonable to log (e.g., management packets)
#define DPERF_LOG_LEVEL_REORDER 4  // Too frequent to log (e.g., reordered pkts)
#define DPERF_LOG_LEVEL_TRACE 5  // Extremely frequent (e.g., all datapath pkts)
#define DPERF_LOG_LEVEL_CC 6     // Even congestion control decisions!

#define DPERF_LOG_DEFAULT_STREAM stdout

// Log messages with "reorder" or higher verbosity get written to
// dperf_trace_file_or_default_stream. This can be stdout for basic debugging, or
// DPerf's trace file for more involved debugging.

#define dperf_trace_file_or_default_stream trace_file_
//#define dperf_trace_file_or_default_stream DPERF_LOG_DEFAULT_STREAM

// If DPERF_LOG_LEVEL is not defined, default to the highest level so that
// YouCompleteMe does not report compilation errors
#ifndef DPERF_LOG_LEVEL
#define DPERF_LOG_LEVEL DPERF_LOG_LEVEL_CC
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_ERROR
#define DPERF_ERROR(...)                                 \
  nicc::output_log_header(stderr, DPERF_LOG_LEVEL_ERROR); \
  fprintf(DPERF_LOG_DEFAULT_STREAM, __VA_ARGS__);        \
  fflush(DPERF_LOG_DEFAULT_STREAM)
#else
#define DPERF_ERROR(...) ((void)0)
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_WARN
#define DPERF_WARN(...)                                                  \
  nicc::output_log_header(DPERF_LOG_DEFAULT_STREAM, DPERF_LOG_LEVEL_WARN); \
  fprintf(DPERF_LOG_DEFAULT_STREAM, __VA_ARGS__);                        \
  fflush(DPERF_LOG_DEFAULT_STREAM)
#else
#define DPERF_WARN(...) ((void)0)
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_INFO
#define DPERF_INFO(...)                                                  \
  nicc::output_log_header(DPERF_LOG_DEFAULT_STREAM, DPERF_LOG_LEVEL_INFO); \
  fprintf(DPERF_LOG_DEFAULT_STREAM, __VA_ARGS__);                        \
  fflush(DPERF_LOG_DEFAULT_STREAM)
#else
#define DPERF_INFO(...) ((void)0)
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_REORDER
#define DPERF_REORDER(...)                                   \
  nicc::output_log_header(dperf_trace_file_or_default_stream, \
                         DPERF_LOG_LEVEL_REORDER);           \
  fprintf(dperf_trace_file_or_default_stream, __VA_ARGS__);  \
  fflush(dperf_trace_file_or_default_stream)
#else
#define DPERF_REORDER(...) ((void)0)
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_TRACE
#define DPERF_TRACE(...)                                     \
  nicc::output_log_header(dperf_trace_file_or_default_stream, \
                         DPERF_LOG_LEVEL_TRACE);             \
  fprintf(dperf_trace_file_or_default_stream, __VA_ARGS__);  \
  fflush(dperf_trace_file_or_default_stream)
#else
#define DPERF_TRACE(...) ((void)0)
#endif

#if DPERF_LOG_LEVEL >= DPERF_LOG_LEVEL_CC
#define DPERF_CC(...)                                        \
  nicc::output_log_header(dperf_trace_file_or_default_stream, \
                         DPERF_LOG_LEVEL_CC);                \
  fprintf(dperf_trace_file_or_default_stream, __VA_ARGS__);  \
  fflush(dperf_trace_file_or_default_stream)
#else
#define DPERF_CC(...) ((void)0)
#endif

/// Return decent-precision time formatted as seconds:microseconds
static std::string get_formatted_time() {
  const auto now = std::chrono::high_resolution_clock::now();

  const size_t sec = static_cast<size_t>(
      std::chrono::time_point_cast<std::chrono::seconds>(now)
          .time_since_epoch()
          .count());

  const size_t usec = static_cast<size_t>(
      std::chrono::time_point_cast<std::chrono::microseconds>(now)
          .time_since_epoch()
          .count());

  // Roll-over seconds every 100 seconds
  char buf[20];
  sprintf(buf, "%zu:%06zu", sec % 100,
          (usec - (sec * 1000000)) /* spare microseconds */);
  return std::string(buf);
}

// Output log message header
static void output_log_header(FILE *stream, int level) {
  std::string formatted_time = get_formatted_time();

  const char *type;
  switch (level) {
    case DPERF_LOG_LEVEL_ERROR: type = "ERROR"; break;
    case DPERF_LOG_LEVEL_WARN: type = "WARNG"; break;
    case DPERF_LOG_LEVEL_INFO: type = "INFOR"; break;
    case DPERF_LOG_LEVEL_REORDER: type = "REORD"; break;
    case DPERF_LOG_LEVEL_TRACE: type = "TRACE"; break;
    case DPERF_LOG_LEVEL_CC: type = "CONGC"; break;
    default: type = "UNKWN";
  }

  fprintf(stream, "%s %s: ", formatted_time.c_str(), type);
}

/// Return true iff REORDER/TRACE/CC mode logging is disabled. These modes can
/// print an unreasonable number of log messages.
static bool is_log_level_reasonable() {
  return DPERF_LOG_LEVEL <= DPERF_LOG_LEVEL_INFO;
}

}  // namespace nicc
