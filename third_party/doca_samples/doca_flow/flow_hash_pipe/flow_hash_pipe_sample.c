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

DOCA_LOG_REGISTER(FLOW_HASH_PIPE);

#define NB_ENTRIES 2	/* number of entries in the created hash pipe */

struct doca_flow_pipe_entry *entries[NB_ENTRIES];	/* array for storing created entries */

/*
 * Create DOCA Flow hash pipe on the switch port.
 * The hash pipe calculates the entry index based on IPv4 destination address;
 * the indicated fields in match_mask variable.
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_hash_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match_mask;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match_mask, 0, sizeof(match_mask));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "HASH_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_HASH;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;
	pipe_cfg.attr.nb_flows = NB_ENTRIES;

	/* match mask defines which header fields to use in order to calculate the entry index */
	match_mask.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match_mask.outer.ip4.dst_ip = 0xffffffff;

	monitor.flags |= DOCA_FLOW_MONITOR_COUNT;

	/* FWD component is defined per entry */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = 0xffff;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries to the hash pipe.
 * First entry forwards "matched" packets to the first port representor, and the second entry forwards "matched" packets to the second one.
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_hash_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_fwd fwd;
	enum doca_flow_flags_type flags = DOCA_FLOW_WAIT_FOR_BATCH;
	doca_error_t result;
	int entry_index = 0;

	memset(&fwd, 0, sizeof(fwd));

	for (entry_index = 0; entry_index < NB_ENTRIES; entry_index++) {
		/* entry index is calculated as follows: hash_func( destination IPv4 address ) mod nb_flows; the hash output is the entry index to be used */
		fwd.type = DOCA_FLOW_FWD_PORT;
		fwd.port_id = entry_index + 1;	/* The port to forward to is defined based on the entry index */

		/* last entry should be inserted with DOCA_FLOW_NO_WAIT flag */
		if (entry_index == NB_ENTRIES - 1)
			flags = DOCA_FLOW_NO_WAIT;

		result = doca_flow_pipe_hash_add_entry(0, pipe, entry_index, NULL, NULL, &fwd, flags,
					      status, &entries[entry_index]);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add hash pipe entry: %s", doca_get_error_string(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Run flow_hash_pipe sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @nb_ports [in]: number of ports the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_hash_pipe(int nb_queues, int nb_ports)
{
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *hash_pipe;
	struct doca_flow_query query_stats;
	struct entries_status status;
	int num_of_entries = NB_ENTRIES;
	doca_error_t result;
	int entry_idx;

	memset(&status, 0, sizeof(status));
	resource.nb_counters = NB_ENTRIES;	/* counter per entry */

	result = init_doca_flow(nb_queues, "switch,hws", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_get_error_string(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_get_error_string(result));
		doca_flow_destroy();
		return result;
	}

	result = create_hash_pipe(doca_flow_port_switch_get(NULL), &hash_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = add_hash_pipe_entries(hash_pipe, &status);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entries to hash pipe: %s", doca_get_error_string(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(NULL), 0, DEFAULT_TIMEOUT_US, num_of_entries);
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

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(15);

	/* dump entries counters */
	for (entry_idx = 0; entry_idx < NB_ENTRIES; entry_idx++) {

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
