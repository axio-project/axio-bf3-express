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

DOCA_LOG_REGISTER(FLOW_FWD_TARGET);

/*
 * Create DOCA Flow pipe with 5 tuple match that forwards the matched traffic to the other port
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @target [in]: target for fwd miss traffic
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_ipv4_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_target *target,
		 struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "IPv4_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

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

	/* forwarding miss traffic to kernel */
	fwd_miss.type = DOCA_FLOW_FWD_TARGET;
	fwd_miss.target = target;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, &fwd_miss, pipe);
}

/*
 * Add DOCA Flow pipe entry to the ipv4 pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_ipv4_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	/* example 5-tuple to forward */
	doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(1, 2, 3, 4);
	doca_be16_t dst_port = rte_cpu_to_be_16(80);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.outer.ip4.dst_ip = dst_ip_addr;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
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
create_main_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
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
 * Add DOCA Flow pipe entry to the control pipe that forwards ipv4 traffic to next pipe
 *
 * @pipe [in]: pipe of the entry
 * @next_pipe [in]: next pipe to forward the matched traffic
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_main_pipe_entry(struct doca_flow_pipe *pipe, struct doca_flow_pipe *next_pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = next_pipe;

	return doca_flow_pipe_control_add_entry(0, 0, pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, status, NULL);
}

/*
 * Run flow_fwd_target sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_fwd_target(int nb_queues)
{
	const int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *main_pipe;
	struct doca_flow_pipe *ipv4_pipe;
	struct doca_flow_target *kernel_target;
	struct entries_status status;
	int num_of_entries = 2;
	doca_error_t result;
	int port_id;

	result = init_doca_flow(nb_queues, "vnf,isolated,hws", resource, nr_shared_resources);
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

	result = doca_flow_get_target(DOCA_FLOW_TARGET_KERNEL, &kernel_target);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get kernel target: %s", doca_get_error_string(result));
		doca_flow_destroy();
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		memset(&status, 0, sizeof(status));

		result = create_ipv4_pipe(ports[port_id], port_id, kernel_target, &ipv4_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create ipv4 pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_ipv4_pipe_entry(ipv4_pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entry: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_main_pipe(ports[port_id], &main_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create main pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_main_pipe_entry(main_pipe, ipv4_pipe, &status);
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
