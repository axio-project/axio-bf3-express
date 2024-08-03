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

#include <rte_byteorder.h>

#include <doca_flow.h>
#include <doca_log.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_CONTROL_PIPE);

/*
 * Create DOCA Flow pipe that match VXLAN traffic with changeable VXLAN tunnel ID and decap action
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_vxlan_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "VXLAN_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.tun.type = DOCA_FLOW_TUN_VXLAN;
	match.tun.vxlan_tun_id = 0xffffffff;

	actions.decap = true;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry with example VXLAN tunnel ID to match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_vxlan_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;

	doca_be32_t vxlan_tun_id = BUILD_VNI(0xcdab12);
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.tun.vxlan_tun_id = vxlan_tun_id;
	actions.action_idx = 0;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow pipe that match MPLS traffic with changeable MPLS tunnel ID and decap action
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_mpls_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	uint8_t src_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	uint8_t dst_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "MPLS_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.tun.type = DOCA_FLOW_TUN_MPLS_O_UDP;
	match.tun.mpls[2].label = 0xffffffff;

	result = doca_flow_mpls_label_encode(0xfffff, 0, 0, true, &match_mask.tun.mpls[2]);
	if (result != DOCA_SUCCESS)
		return result;

	actions.decap = true;
	/* append eth header after decap MPLS tunnel */
	SET_MAC_ADDR(actions.outer.eth.src_mac,
		     src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
	SET_MAC_ADDR(actions.outer.eth.dst_mac,
		     dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5]);
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry with example MPLS tunnel ID to match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_mpls_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	actions.action_idx = 0;

	result = doca_flow_mpls_label_encode(0xababa, 0, 0, true, &match.tun.mpls[2]);
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow pipe that match GRE traffic with changeable GRE key and decap action
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_gre_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	uint8_t src_mac[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	uint8_t dst_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "GRE_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.tun.type = DOCA_FLOW_TUN_GRE;
	match.tun.key_present = 1;
	match.tun.gre_key = 0xffffffff;

	actions.decap = true;
	/* append eth header after decap GRE tunnel */
	SET_MAC_ADDR(actions.outer.eth.src_mac,
		     src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
	SET_MAC_ADDR(actions.outer.eth.dst_mac,
		     dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5]);
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry with example GRE key to match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_gre_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_be32_t gre_key = RTE_BE32(900);
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.tun.gre_key = gre_key;
	actions.action_idx = 0;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, DOCA_FLOW_NO_WAIT, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow control pipe
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_control_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "CONTROL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	return doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries to the control pipe:
 * - entry with VXLAN match that forward the matched packet to vxlan_pipe
 * - entry with MPLS match that forward the matched packet to mpls_pipe
 * - entry with GRE match that forward the matched packet to gre_pipe
 *
 * @control_pipe [in]: pipe of the entry
 * @vxlan_pipe [in]: pipe to forward VXLAN traffic
 * @mpls_pipe [in]: pipe to forward MPLS traffic
 * @gre_pipe [in]: pipe to forward GRE traffic
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_control_pipe_entries(struct doca_flow_pipe *control_pipe, struct doca_flow_pipe *vxlan_pipe,
			 struct doca_flow_pipe *mpls_pipe, struct doca_flow_pipe *gre_pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	uint8_t priority = 0;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(DOCA_VXLAN_DEFAULT_PORT);

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = vxlan_pipe;

	result = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						  NULL, NULL, NULL, NULL, NULL, &fwd, status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entry: %s", doca_get_error_string(result));
		return result;
	}

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(DOCA_FLOW_MPLS_DEFAULT_PORT);

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = mpls_pipe;

	result = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						  NULL, NULL, NULL, NULL, NULL, &fwd, status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entry: %s", doca_get_error_string(result));
		return result;
	}

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.tun.type = DOCA_FLOW_TUN_GRE;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = gre_pipe;

	result = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						  NULL, NULL, NULL, NULL, NULL, &fwd, status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entry: %s", doca_get_error_string(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Run flow_control_pipe sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_control_pipe(int nb_queues)
{
	int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *vxlan_pipe;
	struct doca_flow_pipe *mpls_pipe;
	struct doca_flow_pipe *gre_pipe;
	struct doca_flow_pipe *control_pipe;
	struct entries_status status;
	int num_of_entries = 6;
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
		memset(&status, 0, sizeof(status));

		result = create_vxlan_pipe(ports[port_id], port_id, &vxlan_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add vxlan pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_vxlan_pipe_entry(vxlan_pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add vxlan pipe entry: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_mpls_pipe(ports[port_id], port_id, &mpls_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add mpls pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_mpls_pipe_entry(mpls_pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add mpls pipe entry: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_gre_pipe(ports[port_id], port_id, &gre_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add gre pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_gre_pipe_entry(gre_pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add gre pipe entry: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_control_pipe(ports[port_id], &control_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create control pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_control_pipe_entries(control_pipe, vxlan_pipe, mpls_pipe, gre_pipe, &status);
		if (result != DOCA_SUCCESS) {
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, num_of_entries);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (status.nb_processed != num_of_entries || status.failure) {
			DOCA_LOG_ERR("Failed to process entries");
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return DOCA_ERROR_BAD_STATE;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(10);

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
