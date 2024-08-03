/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <rte_byteorder.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_SWITCH);

#define NB_ENTRIES 2

struct doca_flow_pipe_entry *entries[2 * NB_ENTRIES];	/* array for storing created entries */

/*
 * Create DOCA Flow pipe with 5 tuple match on the switch port.
 * Matched traffic will be forwarded to the port defined per entry.
 * Unmatched traffic will be dropped.
 *
 * @sw_port [in]: switch port
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_switch_pipe(struct doca_flow_port *sw_port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SWITCH_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = sw_port;
	pipe_cfg.attr.nb_flows = NB_ENTRIES;

	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	/* Source, destination IP addresses and source, destination TCP ports are defined per entry */
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_PORT;

	/* Port ID to forward to is defined per entry */
	fwd.port_id = 0xffff;

	/* Unmatched packets will be dropped */
	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	monitor.flags |= DOCA_FLOW_MONITOR_COUNT;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, &fwd_miss, pipe);
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @switch_num [in]: switch number (1, 2 ...)
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_switch_pipe_entries(int switch_num, struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	enum doca_flow_flags_type flags = DOCA_FLOW_WAIT_FOR_BATCH;
	doca_error_t result;
	int entry_index = 0;
	int port_base;
	int entry_base = (switch_num - 1) * 2;

	port_base = (switch_num - 1) * 3;
	doca_be32_t dst_ip_addr;
	doca_be32_t src_ip_addr;
	doca_be16_t dst_port;
	doca_be16_t src_port;

	memset(&fwd, 0, sizeof(fwd));
	memset(&match, 0, sizeof(match));

	for (entry_index = 0; entry_index < NB_ENTRIES; entry_index++) {
		dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8 + entry_base + entry_index);
		src_ip_addr = BE_IPV4_ADDR(1, 2, 3, 4 + entry_base + entry_index);
		dst_port = rte_cpu_to_be_16(80);
		src_port = rte_cpu_to_be_16(1234);

		match.outer.ip4.dst_ip = dst_ip_addr;
		match.outer.ip4.src_ip = src_ip_addr;
		match.outer.tcp.l4_port.dst_port = dst_port;
		match.outer.tcp.l4_port.src_port = src_port;

		fwd.type = DOCA_FLOW_FWD_PORT;
		fwd.port_id = port_base + 1 + entry_index;	/* The port to forward to is defined based on the entry index */

		/* last entry should be inserted with DOCA_FLOW_NO_WAIT flag */
		if (entry_index == NB_ENTRIES - 1)
			flags = DOCA_FLOW_NO_WAIT;

		result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, flags, status,
										  &entries[entry_base + entry_index]);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_get_error_string(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}
/*
 * Run flow_switch sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @nb_ports [in]: number of ports the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_switch(int nb_queues, int nb_ports)
{
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *pipe1;
	struct doca_flow_pipe *pipe2;
	struct doca_flow_query query_stats;
	struct entries_status status;
	doca_error_t result;
	int entry_idx;

	memset(&status, 0, sizeof(status));
	resource.nb_counters = 2 * NB_ENTRIES;	/* counter per entry */

	result = init_doca_flow(nb_queues, "switch,hws", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_get_error_string(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, false /* is_hairpin */);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_get_error_string(result));
		doca_flow_destroy();
		return result;
	}

	result = create_switch_pipe(doca_flow_port_switch_get(ports[0]), &pipe1);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_switch_pipe_entries(1, pipe1, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to the pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(ports[0]), 0, DEFAULT_TIMEOUT_US, NB_ENTRIES);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entries: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = create_switch_pipe(doca_flow_port_switch_get(ports[3]), &pipe2);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_switch_pipe_entries(2, pipe2, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to the pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(ports[3]), 0, DEFAULT_TIMEOUT_US, NB_ENTRIES);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entries: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	if (status.nb_processed != 2 * NB_ENTRIES || status.failure) {
		DOCA_LOG_ERR("Failed to process entries");
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return DOCA_ERROR_BAD_STATE;
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(15);

	/* dump entries counters */
	for (entry_idx = 0; entry_idx < 2 * NB_ENTRIES; entry_idx++) {

		result = doca_flow_query_entry(entries[entry_idx], &query_stats);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entry: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
		DOCA_LOG_INFO("Entry in index: %d", entry_idx);
		DOCA_LOG_INFO("Total bytes: %ld", query_stats.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_stats.total_pkts);
	}

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
