/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <doca_apsh.h>
#include <doca_argp.h>
#include <doca_log.h>
#include <doca_telemetry_exporter.h>

#include "app_shield_agent_core.h"

DOCA_LOG_REGISTER(APSH_APP);

/*
 * APSH agent application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
	struct doca_apsh_process **processes;
	struct doca_apsh_process *process = NULL;
	struct doca_apsh_attestation **attestation;
	int att_failure = 0, att_count;
	struct apsh_resources resources;
	struct apsh_config apsh_conf;
	int runtime_file_ind;
	struct event_indexes indexes;
	struct attestation_event attest_event;
	struct doca_telemetry_exporter_schema *telemetry_schema;
	struct doca_telemetry_exporter_source *telemetry_source;
	const char *process_path;
	bool telemetry_enabled;
	doca_telemetry_exporter_timestamp_t timestamp;
	struct doca_log_backend *sdk_log;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_app_shield_agent", &apsh_conf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}
	result = register_apsh_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Init the app shield agent app */
	result = app_shield_agent_init(&apsh_conf, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init application: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Get process with 'pid' */
	result = get_process_by_pid(&resources, &apsh_conf, &processes, &process);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Apsh init was successful but failed to read process %d information: %s",
			     apsh_conf.pid,
			     doca_error_get_descr(result));
		exit_status = EXIT_FAILURE;
		goto apsh_cleanup;
	}

	/* Creating telemetry schema */
	telemetry_enabled = (telemetry_start(&telemetry_schema, &telemetry_source, &indexes) == DOCA_SUCCESS);

	/* Set const values of the telemetry data */
	attest_event.pid = apsh_conf.pid;
	attest_event.scan_count = 0;
	process_path = doca_apsh_process_info_get(process, DOCA_APSH_PROCESS_COMM);
	assert(process_path != NULL); /* Should never happen, but will catch this error here instead of in strncpy */
	/* Copy string & pad with '\0' until MAX_PATH_LEN bytes were written. this clean the telemetry message */
	strncpy(attest_event.path, process_path, MAX_PATH_LEN);
	attest_event.path[MAX_PATH_LEN] = '\0';

	/* Get attestation */
	result = doca_apsh_attestation_get(process, apsh_conf.exec_hash_map_path, &attestation, &att_count);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Attestation init failed: %s", doca_error_get_descr(result));
		exit_status = EXIT_FAILURE;
		goto telemetry_cleanup;
	}

	/* Start attestation on loop with time_interval */
	DOCA_LOG_INFO("Start attestation on pid=%d", apsh_conf.pid);
	do {
		/* Refresh attestation */
		result = doca_apsh_attst_refresh(&attestation, &att_count);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create a new attestation, error code: %d", result);
			att_failure = true;
		}

		/* Check attestation */
		for (runtime_file_ind = 0; runtime_file_ind < att_count && !att_failure; runtime_file_ind++) {
			att_failure = doca_apsh_attst_info_get(attestation[runtime_file_ind],
							       DOCA_APSH_ATTESTATION_PAGES_PRESENT) !=
				      doca_apsh_attst_info_get(attestation[runtime_file_ind],
							       DOCA_APSH_ATTESTATION_MATCHING_HASHES);
		}

		/* Send telemetry data */
		if (telemetry_enabled) {
			result = doca_telemetry_exporter_get_timestamp(&timestamp);
			if (result != DOCA_SUCCESS)
				DOCA_LOG_ERR("Failed to get timestamp, error code: %d", result);
			attest_event.timestamp = timestamp;
			attest_event.result = att_failure;
			if (doca_telemetry_exporter_source_report(telemetry_source,
								  indexes.attest_index,
								  &attest_event,
								  1) != DOCA_SUCCESS)
				DOCA_LOG_ERR("Cannot report to telemetry");
			++attest_event.scan_count;
		}

		/* Check attestation attempt status */
		if (att_failure) {
			DOCA_LOG_INFO("Attestation failed");
			exit_status = EXIT_FAILURE;
			break;
		}
		DOCA_LOG_INFO("Attestation pass");
		sleep(apsh_conf.time_interval);
	} while (true);

	/* Destroy */
	doca_apsh_attestation_free(attestation);
telemetry_cleanup:
	if (telemetry_enabled)
		telemetry_destroy(telemetry_schema, telemetry_source);
	doca_apsh_processes_free(processes);
apsh_cleanup:
	app_shield_agent_cleanup(&resources);
	doca_argp_destroy();

	return exit_status;
}
