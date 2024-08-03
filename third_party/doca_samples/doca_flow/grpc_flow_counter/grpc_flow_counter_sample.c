/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include <string.h>
#include <unistd.h>

#include <doca_log.h>
#include <doca_flow_grpc_client.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(GRPC_FLOW_COUNTER);

/*
 * Stop DOCA Flow ports
 *
 * @nb_ports [in]: number of ports to stop
 */
static void
stop_doca_flow_grpc_ports(int nb_ports)
{
	int portid;

	for (portid = 0; portid < nb_ports; portid++)
		doca_flow_grpc_port_stop(portid);
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_doca_flow_grpc_port(int port_id)
{
	int max_port_str_len = 128;
	struct doca_flow_port_cfg port_cfg;
	char port_id_str[max_port_str_len];
	uint16_t res_port_id;

	memset(&port_cfg, 0, sizeof(port_cfg));

	port_cfg.port_id = port_id;
	port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
	snprintf(port_id_str, max_port_str_len, "%d", port_id);
	port_cfg.devargs = port_id_str;
	port_cfg.priv_data_size = 0;
	return doca_flow_grpc_port_start(&port_cfg, &res_port_id);
}

/*
 * Initialize DOCA Flow ports
 *
 * @nb_ports [in]: number of ports to create
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
init_doca_flow_grpc_ports(int nb_ports)
{
	int port_id;
	doca_error_t result;

	for (port_id = 0; port_id < nb_ports; port_id++) {
		/* create doca flow port */
		result = create_doca_flow_grpc_port(port_id);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to build doca port: %s", doca_get_error_string(result));
			stop_doca_flow_grpc_ports(port_id + 1);
			return result;
		}
		/* Pair ports should be done in the following order: port0 with port1, port2 with port3 etc */
		if (!port_id || !(port_id % 2))
			continue;
		/* pair odd port with previous port */
		result = doca_flow_grpc_port_pair(port_id, port_id ^ 1);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to pair doca ports: %s", doca_get_error_string(result));
			stop_doca_flow_grpc_ports(port_id + 1);
			return result;
		}
	}
	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow pipe with 5 tuple match and monitor with counter flag
 *
 * @port_id [in]: port ID of the pipe
 * @pipe_id [out]: created pipe ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_grpc_counter_pipe(int port_id, uint64_t *pipe_id)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_grpc_fwd client_fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_grpc_pipe_cfg client_pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "HAIRPIN_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.attr.is_root = true;
	/* use doca_flow_grpc_pipe_cfg to sent port ID of the pipe */
	client_pipe_cfg.cfg = &pipe_cfg;
	client_pipe_cfg.port_id = port_id;

	/* 5 tuple match */
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	/* forwarding traffic to other port */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;
	client_fwd.fwd = &fwd;

	monitor.flags |= DOCA_FLOW_MONITOR_COUNT;

	return doca_flow_grpc_pipe_create(&client_pipe_cfg, &client_fwd, NULL, pipe_id);
}

/*
 * Add DOCA Flow pipe entry to the drop pipe with example 5 tuple to match
 *
 * @pipe_id [in]: pipe ID of the entry
 * @port_id [in]: port ID of the entry
 * @entry_id [out]: pointer to the created entry ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_grpc_counter_pipe_entry(uint64_t pipe_id, uint16_t port_id, uint64_t *entry_id)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	doca_error_t result;
	enum doca_flow_entry_status status;
	int processed, num_of_entries = 1;

	/* example 5-tuple to forward */
	doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(1, 2, 3, 4);
	doca_be16_t dst_port = rte_cpu_to_be_16(80);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.dst_ip = dst_ip_addr;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	result = doca_flow_grpc_pipe_add_entry(0, pipe_id, &match, &actions, NULL, NULL, DOCA_FLOW_NO_WAIT, entry_id);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry");
		return result;
	}

	result = doca_flow_grpc_entries_process(port_id, 0, DEFAULT_TIMEOUT_US, num_of_entries, &processed);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry process function failed with error");
		return result;
	}

	result = doca_flow_grpc_pipe_entry_get_status(*entry_id, &status);
	if (result != DOCA_SUCCESS || processed != num_of_entries || status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entry");
		return DOCA_ERROR_BAD_STATE;
	}
	return DOCA_SUCCESS;
}

/*
 * Run grpc_flow_counter sample
 *
 * @grpc_address [in]: IP address to create the grpc address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
grpc_flow_counter(const char *grpc_address)
{
	int nb_ports = 2;
	int nb_queues = 8;
	int nb_counters = 2;
	int port_id;
	uint64_t pipe_id;
	uint64_t entries_id[nb_ports];
	struct doca_flow_cfg cfg = {0};
	struct doca_flow_query query_stats;
	doca_error_t result;

	cfg.queues = nb_queues;
	cfg.resource.nb_counters = nb_counters;
	cfg.mode_args = "vnf,hws";

	/* create grpc channel with a given address */
	doca_flow_grpc_client_create(grpc_address);

	/* RPC call for doca_flow_init() */
	result = doca_flow_grpc_init(&cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_get_error_string(result));
		return result;
	}

	result = init_doca_flow_grpc_ports(nb_ports);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_get_error_string(result));
		doca_flow_grpc_destroy();
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		result = create_grpc_counter_pipe(port_id, &pipe_id);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create pipe: %s", doca_get_error_string(result));
			stop_doca_flow_grpc_ports(nb_ports);
			doca_flow_grpc_destroy();
			return result;
		}

		result = add_grpc_counter_pipe_entry(pipe_id, port_id, &entries_id[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entry: %s", doca_get_error_string(result));
			stop_doca_flow_grpc_ports(nb_ports);
			doca_flow_grpc_destroy();
			return result;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(5);

	for (port_id = 0; port_id < nb_ports; port_id++) {
		result = doca_flow_grpc_query_entry(entries_id[port_id], &query_stats);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entry: %s", doca_get_error_string(result));
			stop_doca_flow_grpc_ports(nb_ports);
			doca_flow_grpc_destroy();
			return result;
		}
		DOCA_LOG_INFO("Port %d:", port_id);
		DOCA_LOG_INFO("Total bytes: %ld", query_stats.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_stats.total_pkts);
	}

	stop_doca_flow_grpc_ports(nb_ports);
	doca_flow_grpc_destroy();
	return 0;
}
