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

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_GENEVE_ENCAP);

/*
 * Create DOCA Flow pipe with 5 tuple match and set pkt meta value
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_match_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "MATCH_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	/* 5 tuple match */
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	/* set meta data to match on the egress domain */
	actions.meta.pkt_meta = UINT32_MAX;

	/* forwarding traffic to other port */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Create DOCA Flow pipe on EGRESS domain with match on the packet meta and encap action with changeable values
 *
 * @port [in]: port of the pipe
 * @port_id [in]: pipe port ID
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_geneve_encap_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions1, actions2, actions3, actions4, *actions_arr[4];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions1, 0, sizeof(actions1));
	memset(&actions2, 0, sizeof(actions2));
	memset(&actions3, 0, sizeof(actions3));
	memset(&actions4, 0, sizeof(actions4));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "GENEVE_ENCAP_PIPE";
	pipe_cfg.attr.is_root = true;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_EGRESS;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	actions_arr[0] = &actions1;
	actions_arr[1] = &actions2;
	actions_arr[2] = &actions3;
	actions_arr[3] = &actions4;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = 4;
	pipe_cfg.port = port;

	/* match on pkt meta */
	match_mask.meta.pkt_meta = UINT32_MAX;

	/* build basic outer GENEVE L3 encap data */
	actions1.has_encap = true;
	SET_MAC_ADDR(actions1.encap.outer.eth.src_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	SET_MAC_ADDR(actions1.encap.outer.eth.dst_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	actions1.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions1.encap.outer.ip4.src_ip = 0xffffffff;
	actions1.encap.outer.ip4.dst_ip = 0xffffffff;
	actions1.encap.outer.ip4.ttl = 0xff;
	actions1.encap.tun.type = DOCA_FLOW_TUN_GENEVE;
	actions1.encap.tun.geneve.vni = 0xffffffff;
	actions1.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_IPV4);

	/* build basic outer GENEVE + options L3 encap data */
	actions2.has_encap = true;
	SET_MAC_ADDR(actions2.encap.outer.eth.src_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	SET_MAC_ADDR(actions2.encap.outer.eth.dst_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	actions2.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions2.encap.outer.ip4.src_ip = 0xffffffff;
	actions2.encap.outer.ip4.dst_ip = 0xffffffff;
	actions2.encap.outer.ip4.ttl = 0xff;
	actions2.encap.tun.type = DOCA_FLOW_TUN_GENEVE;
	actions2.encap.tun.geneve.vni = 0xffffffff;
	actions2.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_IPV4);
	actions2.encap.tun.geneve.ver_opt_len = 5;

	/* build basic outer GENEVE L2 encap data */
	actions3.has_encap = true;
	SET_MAC_ADDR(actions2.encap.outer.eth.src_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	SET_MAC_ADDR(actions2.encap.outer.eth.dst_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	actions3.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions3.encap.outer.ip4.src_ip = 0xffffffff;
	actions3.encap.outer.ip4.dst_ip = 0xffffffff;
	actions3.encap.outer.ip4.ttl = 0xff;
	actions3.encap.tun.type = DOCA_FLOW_TUN_GENEVE;
	actions3.encap.tun.geneve.vni = 0xffffffff;
	actions3.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_TEB);

	/* build basic outer GENEVE + options L2 encap data */
	actions4.has_encap = true;
	SET_MAC_ADDR(actions4.encap.outer.eth.src_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	SET_MAC_ADDR(actions4.encap.outer.eth.dst_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	actions4.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions4.encap.outer.ip4.src_ip = 0xffffffff;
	actions4.encap.outer.ip4.dst_ip = 0xffffffff;
	actions4.encap.outer.ip4.ttl = 0xff;
	actions4.encap.tun.type = DOCA_FLOW_TUN_GENEVE;
	actions4.encap.tun.geneve.vni = 0xffffffff;
	actions4.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_TEB);
	actions4.encap.tun.geneve.ver_opt_len = 5;

	/* forwarding traffic to the wire */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry with example 5 tuple match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_match_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.ip4.dst_ip = BE_IPV4_ADDR(8, 8, 8, 8);
	match.outer.ip4.src_ip = BE_IPV4_ADDR(1, 2, 3, 4);
	match.outer.tcp.l4_port.dst_port = rte_cpu_to_be_16(80);
	match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(1234);

	actions.meta.pkt_meta = 1;
	actions.action_idx = 0;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(2345);
	actions.meta.pkt_meta = 2;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(3456);
	actions.meta.pkt_meta = 3;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(4567);
	actions.meta.pkt_meta = 4;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Add DOCA Flow pipe entry with example encap values
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_geneve_encap_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	doca_be32_t encap_dst_ip_addr = BE_IPV4_ADDR(81, 81, 81, 81);
	doca_be32_t encap_src_ip_addr = BE_IPV4_ADDR(11, 21, 31, 41);
	uint8_t encap_ttl = 17;
	uint8_t src_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	uint8_t dst_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	actions.has_encap = true;
	SET_MAC_ADDR(actions.encap.outer.eth.src_mac, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
	SET_MAC_ADDR(actions.encap.outer.eth.dst_mac, dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5]);
	actions.encap.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.encap.outer.ip4.src_ip = encap_src_ip_addr;
	actions.encap.outer.ip4.dst_ip = encap_dst_ip_addr;
	actions.encap.outer.ip4.ttl = encap_ttl;
	actions.encap.tun.type = DOCA_FLOW_TUN_GENEVE;

	/* L3 encap - GENEVE header only */
	actions.encap.tun.geneve.vni = BUILD_VNI(0xadadad);
	actions.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_IPV4);
	actions.action_idx = 0;
	match.meta.pkt_meta = 1;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	/* L3 encap - GENEVE header */
	actions.encap.tun.geneve.vni = BUILD_VNI(0xcdcdcd);
	actions.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_IPV4);
	actions.encap.tun.geneve.ver_opt_len = 5;
	/* First option */
	actions.encap.tun.geneve_options[0].class_id = rte_cpu_to_be_16(0x0107);
	actions.encap.tun.geneve_options[0].type = 1;
	actions.encap.tun.geneve_options[0].length = 2;
	actions.encap.tun.geneve_options[1].data = rte_cpu_to_be_32(0x01234567);
	actions.encap.tun.geneve_options[2].data = rte_cpu_to_be_32(0x89abcdef);
	/* Second option */
	actions.encap.tun.geneve_options[3].class_id = rte_cpu_to_be_16(0x0107);
	actions.encap.tun.geneve_options[3].type = 2;
	actions.encap.tun.geneve_options[3].length = 1;
	actions.encap.tun.geneve_options[4].data = rte_cpu_to_be_32(0xabbadeba);
	actions.action_idx = 1;
	match.meta.pkt_meta = 2;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	/* L2 encap - GENEVE header only */
	actions.encap.tun.geneve.vni = BUILD_VNI(0xefefef);
	actions.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_TEB);
	actions.encap.tun.geneve.ver_opt_len = 0;
	actions.action_idx = 2;
	match.meta.pkt_meta = 3;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	/* L2 encap - GENEVE header */
	actions.encap.tun.geneve.vni = BUILD_VNI(0x123456);
	actions.encap.tun.geneve.next_proto = rte_cpu_to_be_16(DOCA_ETHER_TYPE_TEB);
	actions.encap.tun.geneve.ver_opt_len = 5;
	/* Option header */
	actions.encap.tun.geneve_options[0].class_id = rte_cpu_to_be_16(0x0107);
	actions.encap.tun.geneve_options[0].type = 3;
	actions.encap.tun.geneve_options[0].length = 4;
	/* Option data */
	actions.encap.tun.geneve_options[1].data = rte_cpu_to_be_32(0x11223344);
	actions.encap.tun.geneve_options[2].data = rte_cpu_to_be_32(0x55667788);
	actions.encap.tun.geneve_options[3].data = rte_cpu_to_be_32(0x99aabbcc);
	actions.encap.tun.geneve_options[4].data = rte_cpu_to_be_32(0xddeeff00);
	actions.action_idx = 3;
	match.meta.pkt_meta = 4;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Run flow_geneve_encap sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_geneve_encap(int nb_queues)
{
	int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *pipe;
	struct entries_status status_ingress;
	int num_of_entries_ingress = 4;
	struct entries_status status_egress;
	int num_of_entries_egress = 4;
	doca_error_t result;
	int port_id;

	result = init_doca_flow(nb_queues, "vnf,hws", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_get_error_string(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_get_error_string(result));
		doca_flow_destroy();
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		memset(&status_ingress, 0, sizeof(status_ingress));
		memset(&status_egress, 0, sizeof(status_egress));

		result = create_match_pipe(ports[port_id], port_id, &pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create match pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_match_pipe_entries(pipe, &status_ingress);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entries to match pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_geneve_encap_pipe(ports[port_id ^ 1], port_id ^ 1, &pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create geneve encap pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_geneve_encap_pipe_entries(pipe, &status_egress);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entries to geneve encap pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, num_of_entries_ingress);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (status_ingress.nb_processed != num_of_entries_ingress || status_ingress.failure) {
			DOCA_LOG_ERR("Failed to process entries");
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return DOCA_ERROR_BAD_STATE;
		}

		result = doca_flow_entries_process(ports[port_id ^ 1], 0, DEFAULT_TIMEOUT_US, num_of_entries_egress);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (status_egress.nb_processed != num_of_entries_egress || status_egress.failure) {
			DOCA_LOG_ERR("Failed to process entries");
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return DOCA_ERROR_BAD_STATE;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(5);

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
