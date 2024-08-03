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
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_ORDERED_LIST);

/*
 * Create DOCA Flow pipe with changeable 5 tuple match as root
 *
 * @port [in]: port of the pipe
 * @next_pipe [in]: ordered list pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
create_root_pipe(struct doca_flow_port *port, struct doca_flow_pipe *next_pipe, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "ROOT_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
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

	fwd.type = DOCA_FLOW_FWD_ORDERED_LIST_PIPE;
	fwd.ordered_list_pipe.pipe = next_pipe;
	fwd.ordered_list_pipe.idx = 0xffffffff;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries to the root pipe that forwards the traffic to ordered list pipe entries
 *
 * @pipe [in]: pipe of the entries
 * @next_pipe [in]: ordered list pipe to forward the matched traffic
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
add_root_pipe_entries(struct doca_flow_pipe *pipe, struct doca_flow_pipe *next_pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_entry *entry1;
	struct doca_flow_pipe_entry *entry2;
	doca_error_t result;
	doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(1, 1, 1, 1);
	doca_be16_t dst_port = rte_cpu_to_be_16(80);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.ip4.dst_ip = dst_ip_addr;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	fwd.type = DOCA_FLOW_FWD_ORDERED_LIST_PIPE;
	fwd.ordered_list_pipe.pipe = next_pipe;
	fwd.ordered_list_pipe.idx = 0; // fwd the first entry matches to entry idx 0 in ordered list pipe

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, 0, status, &entry1);
	if (result != DOCA_SUCCESS)
		return result;

	src_ip_addr = BE_IPV4_ADDR(2, 2, 2, 2);
	match.outer.ip4.src_ip = src_ip_addr;
	fwd.ordered_list_pipe.idx = 1; // fwd the second entry matches to entry idx 1 in ordered list pipe

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, 0, status, &entry2);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Add DOCA Flow ordered list pipe with two lists
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
create_ordered_list_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_fwd fwd;
	const int nb_ordered_lists = 2;
	struct doca_flow_monitor meter;
	struct doca_flow_monitor counter;
	struct doca_flow_actions actions;
	struct doca_flow_actions actions_mask;
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_ordered_list ordered_list_0;
	struct doca_flow_ordered_list ordered_list_1;
	struct doca_flow_ordered_list *ordered_lists[nb_ordered_lists];

	memset(&fwd, 0, sizeof(fwd));
	memset(&meter, 0, sizeof(meter));
	memset(&counter, 0, sizeof(counter));
	memset(&actions, 0, sizeof(actions));
	memset(&actions_mask, 0, sizeof(actions_mask));
	memset(&ordered_list_0, 0, sizeof(ordered_list_0));
	memset(&ordered_list_1, 0, sizeof(ordered_list_1));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "ORDERED_LIST_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_ORDERED_LIST;
	pipe_cfg.ordered_lists = ordered_lists;
	pipe_cfg.attr.nb_ordered_lists = nb_ordered_lists;
	pipe_cfg.port = port;

	ordered_lists[0] = &ordered_list_0;
	ordered_lists[1] = &ordered_list_1;

	ordered_list_0.idx = 0;
	ordered_list_0.size = 4;
	ordered_list_0.types = (enum doca_flow_ordered_list_element_type[]){DOCA_FLOW_ORDERED_LIST_ELEMENT_ACTIONS, DOCA_FLOW_ORDERED_LIST_ELEMENT_ACTIONS_MASK,
									    DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR, DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR};
	ordered_list_0.elements = (const void *[]){&actions, &actions_mask, &meter, &counter};

	ordered_list_1.idx = 1;
	ordered_list_1.size = 2;
	ordered_list_1.types = (enum doca_flow_ordered_list_element_type[]){DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR, DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR};
	ordered_list_1.elements = (const void *[]){&counter, &meter};

	/* monitor with meter flag */
	meter.flags |= DOCA_FLOW_MONITOR_METER;

	/* monitor with changeable shared counter ID */
	counter.shared_counter_id = 0xffffffff;

	/* modify src ip */
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.src_ip = BE_IPV4_ADDR(192, 168, 0, 0);
	actions_mask.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions_mask.outer.ip4.src_ip = BE_IPV4_ADDR(255, 255, 0, 0);
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries to the ordered list pipe.
 *
 * @pipe [in]: pipe of the entries
 * @port_id [in]: port ID of the entries
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
add_ordered_list_pipe_entries(struct doca_flow_pipe *pipe, int port_id, struct entries_status *status)
{
	struct doca_flow_pipe_entry *entry1;
	struct doca_flow_pipe_entry *entry2;
	struct doca_flow_ordered_list ordered_list_0;
	struct doca_flow_ordered_list ordered_list_1;
	struct doca_flow_monitor counter;
	struct doca_flow_monitor meter;
	struct doca_flow_actions actions;
	struct doca_flow_actions actions_mask;
	doca_error_t result;

	memset(&counter, 0, sizeof(counter));
	memset(&meter, 0, sizeof(meter));
	memset(&actions, 0, sizeof(actions));
	memset(&actions_mask, 0, sizeof(actions_mask));
	memset(&ordered_list_0, 0, sizeof(ordered_list_0));
	memset(&ordered_list_1, 0, sizeof(ordered_list_1));

	ordered_list_0.idx = 0;
	ordered_list_0.size = 4;
	ordered_list_0.types = (enum doca_flow_ordered_list_element_type[]){DOCA_FLOW_ORDERED_LIST_ELEMENT_ACTIONS, DOCA_FLOW_ORDERED_LIST_ELEMENT_ACTIONS_MASK,
									    DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR, DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR};
	ordered_list_0.elements = (const void *[]){&actions, &actions_mask, &meter, &counter};

	meter.flags |= DOCA_FLOW_MONITOR_METER;
	meter.cir = 1024;
	meter.cbs = 1024;

	/* first list with counter ID = port ID */
	counter.shared_counter_id = port_id;

	result = doca_flow_pipe_ordered_list_add_entry(0, pipe, 0, &ordered_list_0, NULL, DOCA_FLOW_NO_WAIT, status, &entry1);
	if (result != DOCA_SUCCESS)
		return result;

	ordered_list_1.idx = 1;
	ordered_list_1.size = 2;
	ordered_list_1.types = (enum doca_flow_ordered_list_element_type[]){DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR, DOCA_FLOW_ORDERED_LIST_ELEMENT_MONITOR};
	ordered_list_1.elements = (const void *[]){&counter, &meter};

	/* second list with counter ID = port ID + 2*/
	counter.shared_counter_id = port_id + 2;
	result = doca_flow_pipe_ordered_list_add_entry(0, pipe, 1, &ordered_list_1, NULL, DOCA_FLOW_NO_WAIT, status, &entry2);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Run flow_ordered_list sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_ordered_list(int nb_queues)
{
	int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *root_pipe;
	struct doca_flow_pipe *ordered_list_pipe;
	uint32_t shared_counter_ids[] = {0, 1, 2, 3};
	struct doca_flow_shared_resource_result query_results_array[4];
	struct doca_flow_shared_resource_cfg cfg = {.domain = DOCA_FLOW_PIPE_DOMAIN_DEFAULT};
	int port_id;
	struct entries_status status;
	int num_of_entries = 4;
	doca_error_t result;

	nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_COUNT] = 4;
	resource.nb_meters = 2;

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

		result = create_ordered_list_pipe(ports[port_id], port_id, &ordered_list_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create ordered list pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_COUNT, shared_counter_ids[port_id], &cfg);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to configure shared counter to port %d", port_id);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_COUNT, &shared_counter_ids[port_id], 1, ports[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to bind shared counter to pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_COUNT, shared_counter_ids[port_id + 2], &cfg);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to configure shared counter to port %d", port_id);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_COUNT, &shared_counter_ids[port_id + 2], 1, ports[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to bind shared counter to pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_ordered_list_pipe_entries(ordered_list_pipe, port_id, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add ordered list pipe entries: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_root_pipe(ports[port_id], ordered_list_pipe, &root_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create root pipe: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
		result = add_root_pipe_entries(root_pipe, ordered_list_pipe, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add root pipe entries: %s", doca_get_error_string(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, 0);
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

	result = doca_flow_shared_resources_query(DOCA_FLOW_SHARED_RESOURCE_COUNT, shared_counter_ids, query_results_array, nb_ports * 2);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query shared counters: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	for (port_id = 0; port_id < 4; port_id++) {
		DOCA_LOG_INFO("Counter %d:", port_id);
		DOCA_LOG_INFO("Total bytes: %ld", query_results_array[port_id].counter.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_results_array[port_id].counter.total_pkts);
	}

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
