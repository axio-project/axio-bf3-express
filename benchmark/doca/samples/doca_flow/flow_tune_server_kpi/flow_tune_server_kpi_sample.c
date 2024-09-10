/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <string.h>
#include <unistd.h>

#include <doca_log.h>
#include <doca_flow.h>
#include <doca_flow_tune_server.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_TUNE_SERVER_KPI);

/*
 * Create DOCA Flow pipe with match on src_ip version 4 and drop the packets
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_ipv4_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));

	/* 5 tuple match */
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;

	actions_arr[0] = &actions;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "IPv4_PIPE", DOCA_FLOW_PIPE_BASIC, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr, NULL, NULL, NB_ACTIONS_ARR);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg actions: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	/* drop all packets */
	fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(pipe_cfg, &fwd, NULL, pipe);
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow pipe entry to the ipv4 pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t add_ipv4_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	static unsigned char lsb = 0;
	doca_error_t result;

	doca_be32_t src_ip_addr = BE_IPV4_ADDR(1, 2, 3, lsb);
	lsb++;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.ip4.src_ip = src_ip_addr;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Logs app level KPI
 *
 * @cfg [in]: Tune Server configuration struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t log_app_kpi(struct doca_flow_tune_server_cfg *cfg)
{
	struct doca_flow_tune_server_kpi_res kpi_res;
	doca_error_t result;

	result = doca_flow_tune_server_get_kpi(cfg, TUNE_SERVER_KPI_TYPE_NR_QUEUES, &kpi_res);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Can't query number of queues KPI: %s", doca_error_get_descr(result));
		goto exit;
	}
	DOCA_LOG_INFO("Number of flow queues = %ld", kpi_res.kpi.val);
	result = doca_flow_tune_server_get_kpi(cfg, TUNE_SERVER_KPI_TYPE_QUEUE_DEPTH, &kpi_res);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Can't query queue depth KPI: %s", doca_error_get_descr(result));
		goto exit;
	}
	DOCA_LOG_INFO("Queue depth = %ld", kpi_res.kpi.val);
exit:
	return result;
}

/*
 * Logs port level KPI
 *
 * @port [in]: port id to query
 * @stage [in]: string to print along the kpi results
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t log_port_kpi(uint16_t port, char *stage)
{
	struct doca_flow_tune_server_kpi_res kpi_res;
	doca_error_t result;

	result = doca_flow_tune_server_get_port_kpi(port, TUNE_SERVER_KPI_TYPE_ENTRIES_OPS_ADD, &kpi_res);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Can't query number of entries that were added: %s", doca_error_get_descr(result));
		goto exit;
	}
	DOCA_LOG_INFO("Stage = %s, number of entries that were added = %ld", stage, kpi_res.kpi.val);

exit:
	return result;
}

/*
 * Run flow_tune_server_kpi sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t flow_tune_server_kpi(int nb_queues)
{
	const int nb_ports = 1;
	struct flow_resources resource = {0};
	uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_dev *dev_arr[nb_ports];
	struct doca_flow_pipe *pipe;
	struct entries_status status;
	struct doca_flow_tune_server_cfg *cfg;
	char *server_path = "/tmp/tune_server.sock";
	int num_of_entries = 0;
	int max_num_of_entries = 20;
	uint16_t port_id_arr[nb_ports];
	uint16_t returned_ports;
	doca_error_t result;

	result = init_doca_flow(nb_queues, "vnf,isolated,hws", &resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	memset(dev_arr, 0, sizeof(struct doca_dev *) * nb_ports);
	result = init_doca_flow_ports(nb_ports, ports, true, dev_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	result = doca_flow_tune_server_cfg_create(&cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create tune server cfg struct: %s", doca_error_get_descr(result));
		goto exit;
	}

	result = doca_flow_tune_server_cfg_set_bind_path(cfg, server_path, strlen(server_path));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set tune server path: %s", doca_error_get_descr(result));
		goto exit;
	}

	result = doca_flow_tune_server_init(cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init tune server: %s", doca_error_get_descr(result));
		goto exit;
	}

	/* query the ports ids in use */
	result = doca_flow_tune_server_get_port_ids(port_id_arr, nb_ports, &returned_ports);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Can't query ports ids: %s", doca_error_get_descr(result));
		goto exit;
	}
	DOCA_LOG_INFO("Number of ports found = %d", returned_ports);

	/* query app KPIs */
	result = log_app_kpi(cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed query app level KPIs: %s", doca_error_get_descr(result));
		goto exit;
	}

	/* query port KPIs before rule insertion */
	result = log_port_kpi(port_id_arr[0], "before insertion");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed query port level KPIs: %s", doca_error_get_descr(result));
		goto exit;
	}

	result = create_ipv4_pipe(ports[0], &pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_error_get_descr(result));
		goto exit;
	}

	/* adds entries to the table */
	for (num_of_entries = 0; num_of_entries < max_num_of_entries; num_of_entries++) {
		result = add_ipv4_pipe_entry(pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
			goto exit;
		}
	}
	result = doca_flow_entries_process(ports[0], 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
		goto exit;
	}

	/* query port KPIs after rule insertion */
	result = log_port_kpi(port_id_arr[0], "after insertion");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed query port level KPIs: %s", doca_error_get_descr(result));
		goto exit;
	}

	DOCA_LOG_INFO("Closing sample");
	sleep(2);
exit:
	doca_flow_tune_server_destroy();
	doca_flow_tune_server_cfg_destroy(cfg);
	result = stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return result;
}
