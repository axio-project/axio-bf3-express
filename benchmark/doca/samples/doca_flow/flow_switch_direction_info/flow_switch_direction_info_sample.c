/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <rte_byteorder.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"
#include "flow_switch_common.h"

DOCA_LOG_REGISTER(FLOW_SWITCH_DIRECTION_INFO);

#define NB_ENTRIES 6

static struct doca_flow_pipe_entry *entries[NB_ENTRIES]; /* array for storing created entries */
static uint8_t entry_idx = 0;

/*
 * Create DOCA Flow pipe with match on ipv4 and next_proto
 * Matched traffic will be forwarded to the port defined per entry.
 * Unmatched traffic will be dropped.
 *
 * @sw_port [in]: switch port
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_network_to_host_pipe(struct doca_flow_port *sw_port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.next_proto = UINT8_MAX;

	match_mask.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match_mask.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match_mask.outer.ip4.next_proto = UINT8_MAX;

	fwd.type = DOCA_FLOW_FWD_PORT;

	/* Port ID to forward to is defined per entry */
	fwd.port_id = UINT16_MAX;

	/* Unmatched packets will be dropped */
	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, sw_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "N2H_PIPE", DOCA_FLOW_PIPE_BASIC, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 2);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg nr_entries: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_dir_info(pipe_cfg, DOCA_FLOW_DIRECTION_NETWORK_TO_HOST);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg dir_info: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match_mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, pipe);
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t add_network_to_host_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&fwd, 0, sizeof(fwd));
	memset(&match, 0, sizeof(match));

	fwd.type = DOCA_FLOW_FWD_PORT;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	/* tcp traffic from the network forwards to port 1 */
	fwd.port_id = 1;
	match.outer.ip4.next_proto = DOCA_FLOW_PROTO_TCP;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_WAIT_FOR_BATCH,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	/* udp traffic from the network forwards to port 2 */
	fwd.port_id = 2;
	match.outer.ip4.next_proto = DOCA_FLOW_PROTO_UDP;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_NO_WAIT,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow pipe with match packets with dst_max = aa:aa:aa:aa:aa:aa
 * Matched traffic will be forwarded to the port defined per entry.
 * Unmatched traffic will be dropped.
 *
 * @sw_port [in]: switch port
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_host_to_network_pipe(struct doca_flow_port *sw_port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd_miss, 0, sizeof(fwd_miss));

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = UINT16_MAX;

	memset(&match.outer.eth.src_mac, 0xaa, DOCA_FLOW_ETHER_ADDR_LEN);

	/* Unmatched packets will be dropped */
	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, sw_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "H2N_PIPE", DOCA_FLOW_PIPE_BASIC, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 1);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg nr_entries: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_dir_info(pipe_cfg, DOCA_FLOW_DIRECTION_HOST_TO_NETWORK);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg dir_info: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, pipe);
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t add_host_to_network_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = 0;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_NO_WAIT,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow pipe with match on port_meta
 * Matched traffic will be forwarded to the pipe defined per entry.
 * Unmatched traffic will be dropped.
 *
 * @sw_port [in]: switch port
 * @nb_ports [in]: number of ports
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_switch_pipe(struct doca_flow_port *sw_port, int nb_ports, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg *pipe_cfg;
	struct doca_flow_fwd fwd = {.type = DOCA_FLOW_FWD_CHANGEABLE};
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd_miss, 0, sizeof(fwd_miss));

	match.parser_meta.port_meta = UINT32_MAX;
	match_mask.parser_meta.port_meta = UINT32_MAX;

	/* Unmatched packets will be dropped */
	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, sw_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "SWITCH_PIPE", DOCA_FLOW_PIPE_BASIC, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, nb_ports);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg nr_entries: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match_mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, pipe);
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow pipe entry to the pipe
 *
 * @pipe [in]: pipe of the entry
 * @n2h_pipe [in]: next pipe for network to host traffic
 * @h2n_pipe [in]: next pipe for host to network traffic
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t add_switch_pipe_entries(struct doca_flow_pipe *pipe,
					    struct doca_flow_pipe *n2h_pipe,
					    struct doca_flow_pipe *h2n_pipe,
					    struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&fwd, 0, sizeof(fwd));
	memset(&match, 0, sizeof(match));

	match.parser_meta.port_meta = 0;
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = n2h_pipe;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_WAIT_FOR_BATCH,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.port_meta = 1;
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = h2n_pipe;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_WAIT_FOR_BATCH,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.port_meta = 2;
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = h2n_pipe;

	result = doca_flow_pipe_add_entry(0,
					  pipe,
					  &match,
					  NULL,
					  NULL,
					  &fwd,
					  DOCA_FLOW_NO_WAIT,
					  status,
					  &entries[entry_idx++]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}
/*
 * Run flow_switch_direction_info sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @nb_ports [in]: number of ports the sample will use
 * @ctx [in]: flow switch context the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t flow_switch_direction_info(int nb_queues, int nb_ports, struct flow_switch_ctx *ctx)
{
	struct flow_resources resource = {0};
	uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_dev *dev_arr[nb_ports];
	uint32_t actions_mem_size[nb_ports];
	struct doca_flow_pipe *root_pipe;
	struct doca_flow_pipe *n2h_pipe;
	struct doca_flow_pipe *h2n_pipe;
	struct doca_flow_resource_query query_stats;
	struct entries_status status;
	doca_error_t result;
	int entry_idx;

	memset(&status, 0, sizeof(status));
	resource.nr_counters = NB_ENTRIES; /* counter per entry */

	result = init_doca_flow(nb_queues, "switch,hws", &resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	memset(dev_arr, 0, sizeof(struct doca_dev *) * nb_ports);
	dev_arr[0] = ctx->doca_dev[0];
	ARRAY_INIT(actions_mem_size, ACTIONS_MEM_SIZE(nb_queues, NB_ENTRIES));
	result = init_doca_flow_ports(nb_ports, ports, false /* is_hairpin */, dev_arr, actions_mem_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	result = create_network_to_host_pipe(doca_flow_port_switch_get(NULL), &n2h_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create network to host pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_network_to_host_pipe_entries(n2h_pipe, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to network to host pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = create_host_to_network_pipe(doca_flow_port_switch_get(NULL), &h2n_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create host to network pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_host_to_network_pipe_entries(h2n_pipe, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to host to network pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = create_switch_pipe(doca_flow_port_switch_get(NULL), nb_ports, &root_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create switch pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_switch_pipe_entries(root_pipe, n2h_pipe, h2n_pipe, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to switch pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(NULL), 0, DEFAULT_TIMEOUT_US, NB_ENTRIES);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	if (status.nb_processed != NB_ENTRIES || status.failure) {
		DOCA_LOG_ERR("Failed to process entries");
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return DOCA_ERROR_BAD_STATE;
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(15);

	/* dump entries counters */
	for (entry_idx = 0; entry_idx < NB_ENTRIES; entry_idx++) {
		result = doca_flow_resource_query_entry(entries[entry_idx], &query_stats);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entry: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
		DOCA_LOG_INFO("Entry in index: %d", entry_idx);
		DOCA_LOG_INFO("Total bytes: %ld", query_stats.counter.total_bytes);
		DOCA_LOG_INFO("Total packets: %ld", query_stats.counter.total_pkts);
	}

	result = stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return result;
}
