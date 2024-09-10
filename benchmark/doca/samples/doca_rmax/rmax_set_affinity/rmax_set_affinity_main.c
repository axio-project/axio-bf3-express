/*
 * Copyright (c) 2022-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdbool.h>
#include <stdlib.h>

#include <doca_log.h>

#include "rmax_common.h"

DOCA_LOG_REGISTER(RMAX_SET_AFFINITY::MAIN);

/* Sample's logic */
doca_error_t set_affinity_sample(unsigned core);

/**
 * Sample configuration options
 */
struct app_config {
	unsigned cpu_core; /* CPU core to set internal thread affinity to */
};

/**
 * ARGP Callback - Handle CPU core number parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t cpu_core_callback(void *param, void *config)
{
	struct app_config *cfg = (struct app_config *)config;
	int cpu = *(int *)param;

	if (cpu < 0) {
		DOCA_LOG_ERR("Invalid CPU core number: %d", cpu);
		return DOCA_ERROR_INVALID_VALUE;
	}
	cfg->cpu_core = cpu;
	return DOCA_SUCCESS;
}

/**
 * Register parameters for command-line arguments parser
 *
 * @return: DOCA_SUCCESS on success and DOCA error otherwise
 */
doca_error_t register_argp_params(void)
{
	doca_error_t result;
	struct doca_argp_param *cpu_param;

	/* --cpu parameter */
	result = doca_argp_param_create(&cpu_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_name(result));
		return result;
	}
	doca_argp_param_set_short_name(cpu_param, "c");
	doca_argp_param_set_long_name(cpu_param, "cpu");
	doca_argp_param_set_description(cpu_param, "CPU core to set affinity to");
	doca_argp_param_set_callback(cpu_param, cpu_core_callback);
	doca_argp_param_set_type(cpu_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(cpu_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_name(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	doca_error_t result;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;
	struct app_config config;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");
	config.cpu_core = 0;

	result = doca_argp_init("doca_rmax_set_affinity", &config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_name(result));
		goto sample_exit;
	}
	if (register_argp_params() != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application command line: %s", doca_error_get_name(result));
		goto sample_exit;
	}

	result = set_affinity_sample(config.cpu_core);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("set_affinity() encountered an error: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	exit_status = EXIT_SUCCESS;

sample_exit:
	doca_argp_destroy();
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
