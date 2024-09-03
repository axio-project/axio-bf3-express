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
#include <doca_dev.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_SWITCH_TO_WIRE);

#define NB_EGRESS_ENTRIES 2

#define NB_INGRESS_ENTRIES 2

#define NB_TOTAL_ENTRIES (NB_EGRESS_ENTRIES + NB_INGRESS_ENTRIES + 1)

static struct doca_flow_pipe *pipe_egress;
static struct doca_flow_pipe *pipe_ingress;
static struct doca_flow_pipe *pipe_rss;

/* array for storing created egress entries */
static struct doca_flow_pipe_entry *egress_entries[NB_EGRESS_ENTRIES];

/* array for storing created ingress entries */
static struct doca_flow_pipe_entry *ingress_entries[NB_INGRESS_ENTRIES];

static struct doca_flow_pipe_entry *rss_entry;

/*
 * Create DOCA Flow pipe with 5 tuple match, changeable set meta action, and forward RSS
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_rss_meta_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	uint16_t rss_queues[1];

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&monitor, 0, sizeof(monitor));

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	/* set mask value */
	actions.meta.pkt_meta = UINT32_MAX;

	pipe_cfg.attr.name = "RSS_META_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	/* 5 tuple match */
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	/* RSS queue - send matched traffic to queue 0  */
	rss_queues[0] = 0;
	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = rss_queues;
	fwd.rss_inner_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_TCP;
	fwd.num_of_queues = 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry with example 5 tuple to match and set meta data value
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_rss_meta_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	doca_error_t result;

	/* example 5-tuple to drop */
	doca_be16_t dst_port = rte_cpu_to_be_16(80);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	/* set meta value */
	actions.meta.pkt_meta = 10;
	actions.action_idx = 0;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &rss_entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

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
create_switch_egress_pipe(struct doca_flow_port *sw_port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SWITCH_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_EGRESS;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = sw_port;
	pipe_cfg.attr.nb_flows = NB_EGRESS_ENTRIES;

	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	/* Source, destination IP addresses and source, destination TCP ports are defined per entry */
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_PORT;

	/* Port ID to forward to is defined per entry */
	fwd.port_id = 0xffff;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Create DOCA Flow pipe with 5 tuple match on the switch port.
 * Matched traffic will be forwarded to the port defined per entry.
 * Unmatched traffic will be dropped.
 *
 * @sw_port [in]: switch port
 * @mirror_id [in]: mirror ID
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_switch_ingress_pipe(struct doca_flow_port *sw_port, uint32_t mirror_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SWITCH_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = sw_port;
	pipe_cfg.attr.nb_flows = NB_INGRESS_ENTRIES;

	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	/* Source, destination IP addresses and source, destination TCP ports are defined per entry */
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	monitor.shared_mirror_id = mirror_id;

	/* Port ID to forward to is defined per entry */
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = NULL;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_switch_egress_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	enum doca_flow_flags_type flags = DOCA_FLOW_WAIT_FOR_BATCH;
	doca_error_t result;
	int entry_index = 0;

	doca_be32_t dst_ip_addr;
	doca_be16_t dst_port;
	doca_be16_t src_port;

	memset(&fwd, 0, sizeof(fwd));
	memset(&match, 0, sizeof(match));

	for (entry_index = 0; entry_index < NB_EGRESS_ENTRIES; entry_index++) {
		dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8 + entry_index);
		dst_port = rte_cpu_to_be_16(80);
		src_port = rte_cpu_to_be_16(1234);

		match.outer.ip4.dst_ip = dst_ip_addr;
		match.outer.tcp.l4_port.dst_port = dst_port;
		match.outer.tcp.l4_port.src_port = src_port;

		fwd.type = DOCA_FLOW_FWD_PORT;
		/* First port as wire to wire, second wire to VF */
		fwd.port_id = entry_index;

		/* last entry should be inserted with DOCA_FLOW_NO_WAIT flag */
		if (entry_index == NB_EGRESS_ENTRIES - 1)
			flags = DOCA_FLOW_NO_WAIT;

		result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, flags, status,
				&egress_entries[entry_index]);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_switch_ingress_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	enum doca_flow_flags_type flags = DOCA_FLOW_WAIT_FOR_BATCH;
	doca_error_t result;
	int entry_index = 0;

	doca_be32_t src_ip_addr;
	doca_be16_t dst_port;
	doca_be16_t src_port;

	memset(&fwd, 0, sizeof(fwd));
	memset(&match, 0, sizeof(match));

	for (entry_index = 0; entry_index < NB_INGRESS_ENTRIES; entry_index++) {
		src_ip_addr = BE_IPV4_ADDR(1, 2, 3, 4 + entry_index);
		dst_port = rte_cpu_to_be_16(80);
		src_port = rte_cpu_to_be_16(1234);

		match.outer.ip4.src_ip = src_ip_addr;
		match.outer.tcp.l4_port.dst_port = dst_port;
		match.outer.tcp.l4_port.src_port = src_port;

		fwd.type = DOCA_FLOW_FWD_PIPE;
		/* First port as wire to wire, second wire to VF */
		fwd.next_pipe = entry_index ? pipe_rss : pipe_egress;

		result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, flags, status,
				&ingress_entries[entry_index]);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @doca_dev [in]: the doca device array for each port
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
sample_create_doca_flow_port(int port_id, struct doca_dev *doca_dev, struct doca_flow_port **port)
{
	int max_port_str_len = 128;
	struct doca_flow_port_cfg port_cfg;
	char port_id_str[max_port_str_len];

	memset(&port_cfg, 0, sizeof(port_cfg));

	port_cfg.port_id = port_id;
	port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
	port_cfg.dev = doca_dev;
	snprintf(port_id_str, max_port_str_len, "%d", port_cfg.port_id);
	port_cfg.devargs = port_id_str;
	return doca_flow_port_start(&port_cfg, port);
}

/*
 * Initialize DOCA Flow ports
 *
 * @nb_ports [in]: number of ports to create
 * @ports [in]: array of ports to create
 * @is_hairpin [in]: port pair should run if is_hairpin = true
 * @doca_dev [in]: the doca device array for each port
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
sample_init_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[], bool is_hairpin, struct doca_dev **doca_dev)
{
	int portid;
	doca_error_t result;

	for (portid = 0; portid < nb_ports; portid++) {
		/* Create doca flow port */
		result = sample_create_doca_flow_port(portid, doca_dev[portid], &ports[portid]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to start port: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(portid + 1, ports);
			return result;
		}
		/* Pair ports should be done in the following order: port0 with port1, port2 with port3 etc */
		if (!is_hairpin || !portid || !(portid % 2))
			continue;
		/* pair odd port with previous port */
		result = doca_flow_port_pair(ports[portid], ports[portid ^ 1]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to pair ports %u - %u", portid, portid ^ 1);
			stop_doca_flow_ports(portid + 1, ports);
			return result;
		}
	}
	return DOCA_SUCCESS;
}

/*
 * Run flow_switch_to_wire sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @nb_ports [in]: number of ports the sample will use
 * @doca_dev [in]: the doca device for proxy port
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_switch_to_wire(int nb_queues, int nb_ports, struct doca_dev *doca_dev)
{
	struct doca_flow_resources resource = {0};
	struct doca_dev *doca_dev_arr[nb_ports];
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_query query_stats;
	struct entries_status status;
	doca_error_t result;
	int entry_idx;
	uint32_t shared_mirror_ids = 1;
	struct doca_flow_mirror_target target = {0};
	struct doca_flow_shared_resource_cfg cfg = {0};
	struct doca_flow_resource_mirror_cfg mirror_cfg = {0};

	memset(&status, 0, sizeof(status));
	nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MIRROR] = 4;
	resource.nb_counters = 2 * NB_TOTAL_ENTRIES;	/* counter per entry */

	result = init_doca_flow(nb_queues, "switch,hws,hairpinq_num=4", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	/* Doca_dev is opened for proxy_port only */
	memset(doca_dev_arr, 0, sizeof(doca_dev_arr));
	doca_dev_arr[0] = doca_dev;
	result = sample_init_doca_flow_ports(nb_ports, ports, false /* is_hairpin */, doca_dev_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	/* Create rss pipe and entry */
	result = create_rss_meta_pipe(doca_flow_port_switch_get(ports[0]), &pipe_rss);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create rss pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_rss_meta_pipe_entry(pipe_rss, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	/* Create egress pipe and entries */
	result = create_switch_egress_pipe(doca_flow_port_switch_get(ports[0]), &pipe_egress);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create egress pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_switch_egress_pipe_entries(pipe_egress, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add egress_entries to the pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	mirror_cfg.nr_targets = 1;
	mirror_cfg.target = &target;
	target.fwd.type = DOCA_FLOW_FWD_PIPE;
	target.fwd.next_pipe = pipe_egress;
	cfg.mirror_cfg = mirror_cfg;
	/* config shared mirror with dest */
	result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_MIRROR,
			shared_mirror_ids, &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to cfg shared mirror");
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}
	/* bind shared mirror to port */
	result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_MIRROR,
			&shared_mirror_ids, 1, ports[0]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to bind shared mirror to port");
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	/* Create ingress pipe and entries */
	result = create_switch_ingress_pipe(doca_flow_port_switch_get(ports[0]), shared_mirror_ids, &pipe_ingress);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ingress pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_switch_ingress_pipe_entries(pipe_ingress, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add ingress_entries to the pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}


	result = doca_flow_entries_process(doca_flow_port_switch_get(ports[0]), 0, DEFAULT_TIMEOUT_US, NB_TOTAL_ENTRIES);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process egress_entries: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	if (status.nb_processed != NB_TOTAL_ENTRIES || status.failure) {
		DOCA_LOG_ERR("Failed to process all entries");
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return DOCA_ERROR_BAD_STATE;
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(15);

	/* dump egress entries counters */
	for (entry_idx = 0; entry_idx < NB_EGRESS_ENTRIES; entry_idx++) {

		result = doca_flow_query_entry(egress_entries[entry_idx], &query_stats);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entry: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
		DOCA_LOG_INFO("Egress Entry in index: %d", entry_idx);
		DOCA_LOG_INFO("Total bytes: %ld", query_stats.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_stats.total_pkts);
	}

	for (entry_idx = 0; entry_idx < NB_INGRESS_ENTRIES; entry_idx++) {

		result = doca_flow_query_entry(ingress_entries[entry_idx], &query_stats);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entry: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
		DOCA_LOG_INFO("Ingress Entry in index: %d", entry_idx);
		DOCA_LOG_INFO("Total bytes: %ld", query_stats.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_stats.total_pkts);
	}

	result = doca_flow_query_entry(rss_entry, &query_stats);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query entry: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}
	DOCA_LOG_INFO("RSS Entry in index: %d", entry_idx);
	DOCA_LOG_INFO("Total bytes: %ld", query_stats.total_bytes);
	DOCA_LOG_INFO("Total packets: %ld", query_stats.total_pkts);

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
