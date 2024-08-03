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

DOCA_LOG_REGISTER(FLOW_ACL);

/* for egress use domain = DOCA_FLOW_PIPE_DOMAIN_EGRESS*/
enum doca_flow_pipe_domain domain = DOCA_FLOW_PIPE_DOMAIN_DEFAULT;

/*
 * Create DOCA Flow control pipe
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
create_rx_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	doca_error_t result;

	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	pipe_cfg.attr.name = "CONTROL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS)
		return result;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	/* forwarding traffic to other port */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_control_add_entry(0, 0, *pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, NULL);
}

/*
 * Add DOCA Flow ACL pipe that matched IPV4 addresses
 *
 * @port [in]: port of the pipe
 * @is_root [in]: pipeline is root or not.
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
create_acl_pipe(struct doca_flow_port *port, bool is_root, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_fwd fwd_miss;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&fwd_miss, 0, sizeof(fwd_miss));

	pipe_cfg.attr.name = "ACL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_ACL;
	pipe_cfg.attr.is_root = is_root;
	pipe_cfg.attr.nb_flows = 10;
	pipe_cfg.attr.domain = domain;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;

	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	return doca_flow_pipe_create(&pipe_cfg, NULL, &fwd_miss, pipe);
}

/*
 * Add DOCA Flow pipe entry to the ACL pipe.
 *
 * @pipe [in]: pipe of the entry
 * @port_id [in]: port ID of the entry
 * @status [in]: the entries status struct that monitors the entries in this specific port
 * @src_ip_addr [in]: src ip address
 * @dst_ip_addr [in]: dst ip address
 * @src_port [in]: src port
 * @dst_port [in]: dst port
 * @l4_type [in]: l4 protocol
 * @src_ip_addr_mask [in]: src ip mask
 * @dst_ip_addr_mask [in]: dst ip mask
 * @src_port_mask [in]: src port mask.
 *	if src_port_mask is equal to src_port, ACL adds rule with exact src port : src_port with mask 0xffff
 *	if src_port_mask is 0, ACL adds rule with any src port : src_port with mask 0x0
 *	if src_port_mask > src_port, ACL adds rule with port range : src_port_from = src_port, src_port_to = src_port_mask  with mask 0xffff
 *	if src_port_mask < src_port, ACL will return with the error
 * @dst_port_mask [in]: dst port mask
 *	if dst_port_mask is equal to dst_port, ACL adds rule with exact dst port : dst_port with mask 0xffff
 *	if dst_port_mask is 0, ACL adds rule with any dst port : dst_port with mask 0x0
 *	if dst_port_mask > dst_port, ACL adds rule with port range : dst_port_from = dst_port, dst_port_to = dst_port_mask  with mask 0xffff
 *	if dst_port_mask < dst_port, ACL will return with the error
 * @priority [in]: priority of the entry. 0 <= priority <= 1024. the lowest parameter value is used as the highest priority
 * @is_allow [in]: allow or deny the entry
 * @flag [in]: Flow entry will be pushed to hw immediately or not. enum doca_flow_flags_type.
 *	flag DOCA_FLOW_WAIT_FOR_BATCH is using for collecting entries by ACL module
 *	flag DOCA_FLOW_NO_WAIT is using for adding the entry and starting building and offloading
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
add_acl_specific_entry(struct doca_flow_pipe *pipe, int port_id, struct entries_status *status,
		       doca_be32_t src_ip_addr, doca_be32_t dst_ip_addr,
		       doca_be16_t src_port, doca_be16_t dst_port, uint8_t l4_type,
		       doca_be32_t src_ip_addr_mask, doca_be32_t dst_ip_addr_mask,
		       doca_be16_t src_port_mask, doca_be16_t dst_port_mask,
		       uint16_t priority,
		       bool is_allow, enum doca_flow_flags_type flag)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&fwd, 0, sizeof(fwd));

	match_mask.outer.ip4.src_ip = src_ip_addr_mask;
	match_mask.outer.ip4.dst_ip = dst_ip_addr_mask;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.ip4.dst_ip = dst_ip_addr;

	if (l4_type == DOCA_FLOW_L4_TYPE_EXT_TCP) {
		match.outer.tcp.l4_port.src_port = src_port;
		match.outer.tcp.l4_port.dst_port = dst_port;
		match_mask.outer.tcp.l4_port.src_port = src_port_mask;
		match_mask.outer.tcp.l4_port.dst_port = dst_port_mask;
	} else {
		match.outer.udp.l4_port.src_port = src_port;
		match.outer.udp.l4_port.dst_port = dst_port;
		match_mask.outer.udp.l4_port.src_port = src_port_mask;
		match_mask.outer.udp.l4_port.dst_port = dst_port_mask;
	}
	match.outer.l4_type_ext = l4_type;

	if (is_allow) {
		if (domain == DOCA_FLOW_PIPE_DOMAIN_DEFAULT) {
			fwd.type = DOCA_FLOW_FWD_PORT;
			fwd.port_id = port_id ^ 1;
		} else { //domain == DOCA_FLOW_PIPE_DOMAIN_EGRESS
			fwd.type = DOCA_FLOW_FWD_PORT;
			fwd.port_id = port_id;
		}
	} else
		fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_acl_add_entry(0, pipe, &match, &match_mask,
			priority, &fwd, flag, status, NULL);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add acl pipe entry: %s",
				doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Add DOCA Flow pipe entries to the ACL pipe.
 *
 * @pipe [in]: pipe of the entry
 * @port_id [in]: port ID of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
add_acl_pipe_entries(struct doca_flow_pipe *pipe, int port_id, struct entries_status *status)
{
	doca_error_t result;

	result = add_acl_specific_entry(pipe, port_id, status,
			BE_IPV4_ADDR(1, 2, 3, 4), BE_IPV4_ADDR(8, 8, 8, 8),
			RTE_BE16(1234), RTE_BE16(80), DOCA_FLOW_L4_TYPE_EXT_TCP,
			RTE_BE32(0xffffffff), RTE_BE32(0xffffffff),
			RTE_BE16(0x00), RTE_BE16(0x0), 10, false,
			DOCA_FLOW_WAIT_FOR_BATCH);
	if (result != DOCA_SUCCESS)
		return result;

	result = add_acl_specific_entry(pipe, port_id, status,
			BE_IPV4_ADDR(172, 20, 1, 4), BE_IPV4_ADDR(192, 168, 3, 4),
			RTE_BE16(1234), RTE_BE16(80), DOCA_FLOW_L4_TYPE_EXT_UDP,
			RTE_BE32(0xffffffff), RTE_BE32(0xffffffff),
			RTE_BE16(0x0), RTE_BE16(3000), 50, true,
			DOCA_FLOW_WAIT_FOR_BATCH);

	if (result != DOCA_SUCCESS)
		return result;

	result = add_acl_specific_entry(pipe, port_id, status,
			BE_IPV4_ADDR(172, 20, 1, 4), BE_IPV4_ADDR(192, 168, 3, 4),
			RTE_BE16(1234), RTE_BE16(80), DOCA_FLOW_L4_TYPE_EXT_TCP,
			RTE_BE32(0xffffffff), RTE_BE32(0xffffffff),
			RTE_BE16(1234), RTE_BE16(0x0), 40, true,
			DOCA_FLOW_WAIT_FOR_BATCH);

	if (result != DOCA_SUCCESS)
		return result;

	result = add_acl_specific_entry(pipe, port_id, status,
			BE_IPV4_ADDR(1, 2, 3, 5), BE_IPV4_ADDR(8, 8, 8, 6),
			RTE_BE16(1234), RTE_BE16(80), DOCA_FLOW_L4_TYPE_EXT_TCP,
			RTE_BE32(0xffffff00), RTE_BE32(0xffffff00),
			RTE_BE16(0xffff), RTE_BE16(80), 20, true,
			DOCA_FLOW_NO_WAIT);

	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Run flow_acl sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_acl(int nb_queues)
{
	const int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *acl_pipe;
	struct doca_flow_pipe *rx_pipe;
	struct entries_status status;
	int num_of_entries = 4;
	doca_error_t result;
	int port_id, port_acl;

	result = init_doca_flow(nb_queues, "vnf,hws", resource,
			nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s",
				doca_get_error_string(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s",
				doca_get_error_string(result));
		doca_flow_destroy();
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		memset(&status, 0, sizeof(status));

		if (domain == DOCA_FLOW_PIPE_DOMAIN_DEFAULT)
			port_acl = port_id;
		else // domain == DOCA_FLOW_PIPE_DOMAIN_EGRESS
			port_acl = port_id ^ 1;

		result = create_acl_pipe(ports[port_acl], true, &acl_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create acl pipe: %s",
					doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_acl_pipe_entries(acl_pipe, port_acl, &status);
		if (result != DOCA_SUCCESS) {
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (domain == DOCA_FLOW_PIPE_DOMAIN_EGRESS) {
			result = create_rx_pipe(ports[port_id], port_id, &rx_pipe);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to create main pipe: %s", doca_get_error_string(result));
				stop_doca_flow_ports(nb_ports, ports);
				doca_flow_destroy();
				return result;
			}
		}

		result = doca_flow_entries_process(ports[port_acl], 0, DEFAULT_TIMEOUT_US, num_of_entries);
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
	sleep(50);

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
