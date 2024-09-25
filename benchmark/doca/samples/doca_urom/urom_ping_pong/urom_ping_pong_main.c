/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdlib.h>

#include <mpi.h>

#include <doca_argp.h>
#include <doca_log.h>

#include "urom_common.h"

DOCA_LOG_REGISTER(UROM_PING_PONG::MAIN);

#define MAX_MSG_LEN 100 /* Maximum length of ping pong message*/

/* Sample's Logic */
doca_error_t urom_ping_pong(const char *message, const char *device, uint32_t rank, uint32_t size);

/**
 * Ping pong configuration file
 */
struct urom_pp_cfg {
	struct urom_common_cfg common; /* Common command line configuration arguments */
	char message[MAX_MSG_LEN];     /* Ping-Pong user-message */
};

/*
 * ARGP Callback - Handle ping pong message parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t message_callback(void *param, void *config)
{
	struct urom_pp_cfg *cfg = (struct urom_pp_cfg *)config;
	char *msg = (char *)param;
	int len;

	len = strnlen(msg, MAX_MSG_LEN);
	if (len == MAX_MSG_LEN) {
		DOCA_LOG_ERR("Entered message exceeding the maximum size of %d", MAX_MSG_LEN - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	strncpy(cfg->message, msg, len + 1);
	return DOCA_SUCCESS;
}

/*
 * Register ping pong argp params
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t register_urom_pp_params(void)
{
	doca_error_t result;
	struct doca_argp_param *message;

	result = register_urom_common_params();
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_argp_param_create(&message);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	doca_argp_param_set_short_name(message, "m");
	doca_argp_param_set_long_name(message, "message");
	doca_argp_param_set_description(message, "Specify message");
	doca_argp_param_set_callback(message, message_callback);
	doca_argp_param_set_type(message, DOCA_ARGP_TYPE_STRING);

	result = doca_argp_register_param(message);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
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
	struct urom_pp_cfg cfg;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;
	int rank, size;

	/* Set configuration default values */
	strcpy(cfg.message, "hello world");
	strcpy(cfg.common.device_name, "mlx5_0");

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_urom_ping_pong", &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	/* Register RegEx scan params */
	result = register_urom_pp_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register sample parameters: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	/* Start parsing sample arguments */
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	MPI_Init(NULL, NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	result = urom_ping_pong(cfg.message, cfg.common.device_name, rank, size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("urom_ping_pong() encountered an error: %s", doca_error_get_descr(result));
		goto mpi_barrier;
	}

	exit_status = EXIT_SUCCESS;

mpi_barrier:
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
