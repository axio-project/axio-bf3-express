/*
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES.
 * Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FLEXIO_DEV_DEBUG_H_
#define _FLEXIO_DEV_DEBUG_H_

#include <stdint.h>
#include <dpaintrin.h>

#include <libflexio-dev/flexio_dev.h>

/* Inlined code section.
 * Skip to User API section below */
struct flexio_dev_perf_data {
	uint64_t cycles;
	uint64_t insts;
	int flag;
};

FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_perf_end(struct flexio_dev_perf_data *pdata)
{
	pdata->cycles = __dpa_thread_cycles() - pdata->cycles;
	pdata->insts = __dpa_thread_inst_ret() - pdata->insts;
}

FLEXIO_DEV_ALWAYS_INLINE void flexio_dev_perf_aggregate(struct flexio_dev_perf_data *collect,
							struct flexio_dev_perf_data *pdata)
{
	collect->cycles += pdata->cycles;
	collect->insts += pdata->insts;
}

FLEXIO_DEV_ALWAYS_INLINE struct flexio_dev_perf_data flexio_dev_perf_start(void)
{
	struct flexio_dev_perf_data tmp = {
		.cycles = __dpa_thread_cycles(),
		.insts = __dpa_thread_inst_ret(),
		.flag = 0,
	};

	return tmp;
}

/********************************************************************************************/
/* User API section                                                                         */
/********************************************************************************************/

/**
 * @brief Print a string to SimX log.
 *
 * This function prints a given string to SimX log according to user
 * apecified length.
 *
 * @param[in] str - A pointer to string buffer.
 * @param[in] len - The length of the string.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void print_sim_str(const char *str, int len);

/**
 * @brief Print a value in hexadecimal presentation to SimX log.
 *
 * This function prints a given unsigned 64 bit integer value to SimX log
 * in hexadecimal presentation up to a length specified by user.
 * Note that MSB part is dropped if exceeds length.
 *
 * @param[in] val - A value to be printed.
 * @param[in] len - The maximal hexadecimal string length to print.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void print_sim_hex(uint64_t val, int len);

/**
 * @brief Print a value in decimal presentation to SimX log.
 *
 * This function prints a signed given integer value to SimX log
 * in decimal presentation up to a length specified by user.
 * Note that MSB part is dropped if exceeds length.
 *
 * @param[in] val - A value to be printed.
 * @param[in] len - The maximal decimal string length to print.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void print_sim_int(int val, int len);

/**
 * @brief Print a a character to SimX log.
 *
 * This function prints a given integer value as a character
 * to SimX log.
 *
 * @param[in] val - A value to be printed.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void print_sim_putc(int val);

/**
 * @brief Trace a value to SimX log with a specific text.
 *
 * This function prints a given text and integer value with function name and line number
 * to SimX log.
 *
 * @param[in] text - Text to be added to trace.
 * @param[in] val - A value to be traced.
 * @param[in] func - Function name to include in the trace format.
 * @param[in] line - Line number to include in the trace format.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void sim_trace(const char *text, uint64_t val, const char *func, int line);

#define SIM_TRACE(text, val) sim_trace(text, (uint64_t)(val), __func__, __LINE__)
#define SIM_TRACEVAL(val) SIM_TRACE(#val, val)

/* @brief Console print of collected data in human readable form.
 *
 * Console print of collected data in human readable form. Use it after data collection by
 * macro FLEXIO_DEV_PERF_COLLECT()
 *
 * @param[in] pdata - pointer to structure with collected data.
 * @param[in] keyword - string to be added before print. Usable for future grepping
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_perf_print(struct flexio_dev_perf_data *pdata,
						   const char *keyword);

/**
 * @brief Measure and display instructions and clock cycles counters for pointed code block.
 *
 * Measure and display instructions and clock cycles counters for pointed code block.
 *      Usage example:
 *      FLEXIO_DEV_PERF_MEASURE(GREP1) {
 *              code block
 *      }
 *
 *      or
 *
 *      FLEXIO_DEV_PERF_MEASURE(GREP2) foo();
 *
 * Results of measuring will be printed in form:
 *  GREP1: instructions/cycles: <instructions_cnt>/<cpu_cycles_cnt>
 *  GREP2: instructions/cycles: <instructions_cnt>/<cpu_cycles_cnt>
 *
 * @return void.
 */
#define FLEXIO_DEV_PERF_MEASURE(keyword) \
	for (struct flexio_dev_perf_data pdata = flexio_dev_perf_start(); !pdata.flag; \
	     flexio_dev_perf_end(&pdata), flexio_dev_perf_print(&pdata, #keyword), pdata.flag = 1)

/**
 * @brief Collect instructions and CPU cycles counters for pointed code block.
 *
 * Collect instructions and CPU cycles counters for pointed code block.
 *      Usage example:
 *      struct flexio_dev_perf_data collector = { 0 };
 *
 *      for () {
 *              FLEXIO_DEV_PERF_COLLECT(&collector) foo();
 *
 *              some code not relevant for our measurement
 *
 *              FLEXIO_DEV_PERF_COLLECT(&collector) {
 *                      bar();
 *                      baz();
 *              }
 *      }
 *      flexio_dev_perf_print(&collector, "Collected");
 *
 *
 * @param[in] collector - A pointer to a flexio_dev_perf_data structure.
 *
 * @return void.
 */
#define FLEXIO_DEV_PERF_COLLECT(collector) \
	for (struct flexio_dev_perf_data pdata = flexio_dev_perf_start(); !pdata.flag; \
	     flexio_dev_perf_end(&pdata), flexio_dev_perf_aggregate(collector, &pdata), \
	     pdata.flag = 1)

/**
 * @brief Display performance measure tool overhead.
 *
 * In case of measuring small pieces of code, measure tool overhead can be significant.
 * If you want to estimate this overhead - use this function.
 * Similar result you can receive, using FLEXIO_DEV_PERF_MEASURE(XXX);
 * This data can be substituted from results given by FLEXIO_DEV_PERF_MEASURE() for more accurate
 * estimations.
 * No reason to do it in case of measuring of big blocks of code. Because perf tool overhead in
 * this case will be negligible.
 * Few calls of this function can give you slightly different results. Depending context and
 * current memory status.
 *
 * @return void.
 */
FLEXIO_DEV_EXPERIMENTAL void flexio_dev_perf_measure_calibrate(void);

#endif
