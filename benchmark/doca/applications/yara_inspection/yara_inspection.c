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

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <doca_apsh.h>
#include <doca_argp.h>
#include <doca_log.h>
#include <doca_telemetry_exporter.h>

#include "utils.h"

#include "yara_inspection_core.h"

DOCA_LOG_REGISTER(YARA_APP);

static bool running = true; /* True/False - should we continue YARA scanning or not */

/*
 * YARA agent application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	doca_error_t result;
	struct doca_log_backend *sdk_log;
	struct yara_config yara_conf;
	struct yara_resources resources;
	struct doca_apsh_process **processes;
	struct doca_apsh_yara **yara_matches;
	doca_telemetry_exporter_type_index_t yara_index;
	int num_processes, i, yara_matches_size;
	enum doca_apsh_yara_rule yara_rules_arr[] = {DOCA_APSH_YARA_RULE_MIMIKATZ, DOCA_APSH_YARA_RULE_HELLO_WORLD};
	uint32_t yara_rules_arr_size = 2;
	struct doca_telemetry_exporter_schema *telemetry_schema;
	struct doca_telemetry_exporter_source *telemetry_source;
	doca_telemetry_exporter_timestamp_t timestamp;
	struct yara_event yara_match_event;
	bool telemetry_enabled;
	const char *str;
	uint32_t pid;

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
	result = doca_argp_init("doca_yara_inspection", &yara_conf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}
	result = register_yara_params();
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

	/* Init the yara inspection app */
	result = yara_inspection_init(&yara_conf, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init application: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Creating telemetry schema */
	telemetry_enabled = (telemetry_start(&telemetry_schema, &telemetry_source, &yara_index) == DOCA_SUCCESS);

	do {
		result = doca_apsh_processes_get(resources.sys, &processes, &num_processes);
		if (result != DOCA_SUCCESS)
			return result;
		for (i = 0; i < num_processes; i++) {
			result = doca_apsh_yara_get(processes[i],
						    yara_rules_arr,
						    yara_rules_arr_size,
						    DOCA_APSH_YARA_SCAN_HEAP,
						    &yara_matches,
						    &yara_matches_size);

			if (yara_matches_size != 0) {
				for (i = 0; i < yara_matches_size; i++) {
					pid = doca_apsh_yara_info_get(yara_matches[i], DOCA_APSH_YARA_PID);
					str = doca_apsh_yara_info_get(yara_matches[i], DOCA_APSH_YARA_RULE);
					DOCA_LOG_INFO("Got match for Yara rule %s in process id %d", str, pid);

					if (!telemetry_enabled)
						continue;

					result = doca_telemetry_exporter_get_timestamp(&timestamp);
					if (result != DOCA_SUCCESS)
						DOCA_LOG_ERR("Failed to get timestamp, error code: %d", result);
					yara_match_event.timestamp = timestamp;
					yara_match_event.pid =
						doca_apsh_yara_info_get(yara_matches[i], DOCA_APSH_YARA_PID);
					yara_match_event.vad =
						doca_apsh_yara_info_get(yara_matches[i],
									DOCA_APSH_YARA_MATCH_WINDOW_ADDR);
					str = doca_apsh_yara_info_get(yara_matches[i], DOCA_APSH_YARA_COMM);
					if (strlcpy(yara_match_event.process_name, str, MAX_PROCESS_NAME_LEN) >=
					    MAX_PROCESS_NAME_LEN)
						yara_match_event.process_name[MAX_PROCESS_NAME_LEN - 2] = '+';
					str = doca_apsh_yara_info_get(yara_matches[i], DOCA_APSH_YARA_RULE);
					if (strlcpy(yara_match_event.yara_rule_name, str, MAX_PATH_LEN) >= MAX_PATH_LEN)
						yara_match_event.yara_rule_name[MAX_PATH_LEN - 2] = '+';

					/* Send telemetry data */
					if (doca_telemetry_exporter_source_report(telemetry_source,
										  yara_index,
										  &yara_match_event,
										  1) != DOCA_SUCCESS)
						DOCA_LOG_ERR("Cannot report to telemetry");
				}
				doca_apsh_yara_free(yara_matches);
				running = false;
				break;
			}
		}
		if (running) {
			DOCA_LOG_INFO("No match for any Yara rule");
			sleep(yara_conf.time_interval);
		}
		doca_apsh_processes_free(processes);
	} while (running);

	/* Destroy */
	if (telemetry_enabled)
		telemetry_destroy(telemetry_schema, telemetry_source);

	yara_inspection_cleanup(&resources);

	doca_argp_destroy();

	return DOCA_SUCCESS;
}
