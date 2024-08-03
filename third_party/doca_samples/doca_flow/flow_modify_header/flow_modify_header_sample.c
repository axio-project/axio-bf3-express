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

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

#define NB_ACTION_DESC (1)

DOCA_LOG_REGISTER(FLOW_MODIFY_HEADER);

/*
 * Create DOCA Flow pipe that match changeable destination IP, modify the destination mac address and decrease TTL value by one
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_modify_header_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_action_descs descs;
	struct doca_flow_action_descs *descs_arr[NB_ACTIONS_ARR];
	struct doca_flow_action_desc desc_array[NB_ACTION_DESC] = {0};
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&descs, 0, sizeof(descs));

	pipe_cfg.attr.name = "MODIFY_HEADER_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	descs_arr[0] = &descs;
	descs.nb_action_desc = NB_ACTION_DESC;
	descs.desc_array = desc_array;
	pipe_cfg.action_descs = descs_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.dst_ip = 0xffffffff;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	desc_array[0].type = DOCA_FLOW_ACTION_ADD;
	desc_array[0].add.dst.field_string = "outer.ipv4.ttl";
	desc_array[0].add.dst.bit_offset = 0;
	desc_array[0].add.width = 8;
	/* set ttl=-1 in doca_flow_actions for decrease TTL value by one */
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.ttl = UINT8_MAX;
	/* set changeble modify destination mac address */
	SET_MAC_ADDR(actions.outer.eth.dst_mac, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry to the pipe with example values.
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_modify_header_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;
	doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.ip4.dst_ip = dst_ip_addr;
	actions.action_idx = 0;

	/* modify destination mac address */
	SET_MAC_ADDR(actions.outer.eth.dst_mac, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff);

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Run flow_modify_header sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_modify_header(int nb_queues)
{
	int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *pipe;
	struct entries_status status;
	int num_of_entries = 1;
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

		result = create_modify_header_pipe(ports[port_id], port_id, &pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_modify_header_pipe_entry(pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entry: %s", doca_get_error_string(result));
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
	sleep(5);

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
