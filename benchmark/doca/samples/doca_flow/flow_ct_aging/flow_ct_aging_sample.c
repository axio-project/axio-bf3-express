/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdlib.h>
#include <unistd.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_flow.h>
#include <doca_flow_ct.h>

#include "flow_ct_common.h"
#include "flow_common.h"

#define PACKET_BURST 128

DOCA_LOG_REGISTER(FLOW_CT_AGING);

/* user context struct for aging entry */
struct aging_user_data {
	struct entries_status *status; /* status pointer */
	int entry_num;		       /* entry number */
	int port_id;		       /* port ID of the entry */
};

/*
 * Handle all aged flow in a port
 *
 * @port [in]: port to remove the aged flow from
 * @ct_queue [in]: Pipe of the entries
 * @status [in]: user context for adding entry
 * @total_counter [in/out]: counter for all aged flows in both ports
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t handle_aged_flow(struct doca_flow_port *port,
				     uint16_t ct_queue,
				     struct entries_status *status,
				     int *total_counter)
{
	int num_of_aged_entries;
	doca_error_t result;

	num_of_aged_entries = doca_flow_aging_handle(port, ct_queue, 0, 0);
	while (num_of_aged_entries > 0) {
		*total_counter += num_of_aged_entries;
		DOCA_LOG_INFO("Num of aged CT entries: %d, total: %d", num_of_aged_entries, *total_counter);

		result = doca_flow_entries_process(port, ct_queue, DEFAULT_TIMEOUT_US, 0);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process CT entries: %s", doca_error_get_descr(result));
			return result;
		}
		if (status->failure) {
			DOCA_LOG_ERR("Failed to process CT entries, status is not success");
			return DOCA_ERROR_BAD_STATE;
		}

		status->nb_processed = 0;
		num_of_aged_entries = doca_flow_aging_handle(port, 0, 0, 0);
	}

	return DOCA_SUCCESS;
}

/*
 * Entry processing callback
 *
 * @entry [in]: DOCA Flow entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
static void check_for_valid_entry_aging(struct doca_flow_pipe_entry *entry,
					uint16_t pipe_queue,
					enum doca_flow_entry_status status,
					enum doca_flow_entry_op op,
					void *user_ctx)
{
	(void)entry;
	(void)op;
	(void)pipe_queue;
	struct aging_user_data *user_data = (struct aging_user_data *)user_ctx;

	if (user_data == NULL)
		return;

	if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
		user_data->status->failure = true; /* set failure to true if processing failed */
	if (op == DOCA_FLOW_ENTRY_OP_AGED) {
		doca_flow_ct_rm_entry(pipe_queue, NULL, DOCA_FLOW_NO_WAIT, entry);
		DOCA_LOG_INFO("CT Entry number %d aged out and removed", user_data->entry_num);
	} else
		user_data->status->nb_processed++;
}

/*
 * Create CT pipe
 *
 * @port [in]: Pipe port
 * @fwd_pipe [in]: Forward pipe pointer
 * @pipe [out]: Created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_ct_pipe(struct doca_flow_port *port,
				   struct doca_flow_pipe *fwd_pipe,
				   struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match mask;
	struct doca_flow_pipe_cfg *cfg;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&mask, 0, sizeof(mask));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd));

	result = doca_flow_pipe_cfg_create(&cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(cfg, "CT_PIPE", DOCA_FLOW_PIPE_CT, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(cfg, &match, &mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = fwd_pipe;

	fwd_miss.type = DOCA_FLOW_FWD_PIPE;
	fwd_miss.next_pipe = fwd_pipe;

	result = doca_flow_pipe_create(cfg, &fwd, &fwd_miss, pipe);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to add CT pipe: %s", doca_error_get_descr(result));
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(cfg);
	return result;
}

/*
 * Create pipe to count packets based on 5 tuple match
 *
 * @port [in]: Pipe port
 * @pipe [out]: Created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_count_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));

	/* 5 tuple match */
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.udp.l4_port.src_port = 0xffff;
	match.outer.udp.l4_port.dst_port = 0xffff;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "COUNT_PIPE", DOCA_FLOW_PIPE_BASIC, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
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

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = 1;

	result = doca_flow_pipe_create(pipe_cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create count pipe: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	doca_flow_pipe_cfg_destroy(pipe_cfg);

	memset(&match, 0, sizeof(match));
	match.outer.ip4.dst_ip = BE_IPV4_ADDR(8, 8, 8, 8);
	match.outer.ip4.src_ip = BE_IPV4_ADDR(1, 2, 3, 4);
	match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(80);
	match.outer.udp.l4_port.src_port = rte_cpu_to_be_16(1234);

	result = doca_flow_pipe_add_entry(0, *pipe, &match, NULL, NULL, NULL, 0, NULL, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add count pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, 0);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to process count entry: %s", doca_error_get_descr(result));

	return result;

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow CT pipe entry to be aged
 *
 * @port [in]: Pipe port
 * @ct_queue [in]: Pipe of the entries
 * @nb_aging_entries [in]: Number of entries to add
 * @status [in]: User context for adding entry
 * @user_data [out]: User data for each entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t add_age_ct_entries(struct doca_flow_port *port,
				       uint16_t ct_queue,
				       const int nb_aging_entries,
				       struct entries_status *status,
				       struct aging_user_data *user_data[nb_aging_entries])
{
	struct doca_flow_ct_match match_o;
	struct doca_flow_ct_match match_r;
	struct doca_flow_pipe_entry *entry;
	doca_be32_t src_ip_addr;
	uint32_t aging_sec, flags = DOCA_FLOW_CT_ENTRY_FLAGS_NO_WAIT | DOCA_FLOW_CT_ENTRY_FLAGS_DIR_ORIGIN;
	int i;
	doca_error_t result;

	for (i = 0; i < nb_aging_entries; i++) {
		user_data[i] = (struct aging_user_data *)malloc(sizeof(struct aging_user_data));
		if (user_data[i] == NULL) {
			DOCA_LOG_ERR("Failed to allocate user data");
			return DOCA_ERROR_NO_MEMORY;
		}

		aging_sec = (uint32_t)((rte_rand() % 10) + 3);

		memset(&match_o, 0, sizeof(match_o));
		memset(&match_r, 0, sizeof(match_r));

		src_ip_addr = BE_IPV4_ADDR(i, 2, 3, 4);

		match_o.ipv4.src_ip = src_ip_addr;
		match_o.ipv4.dst_ip = BE_IPV4_ADDR(8, 8, 8, 8);
		match_r.ipv4.src_ip = match_o.ipv4.dst_ip;
		match_r.ipv4.dst_ip = match_o.ipv4.src_ip;

		match_o.ipv4.l4_port.src_port = rte_cpu_to_be_16(1234);
		match_o.ipv4.l4_port.dst_port = rte_cpu_to_be_16(80);
		match_r.ipv4.l4_port.src_port = match_o.ipv4.l4_port.dst_port;
		match_r.ipv4.l4_port.dst_port = match_o.ipv4.l4_port.src_port;

		match_o.ipv4.next_proto = DOCA_FLOW_PROTO_UDP;
		match_r.ipv4.next_proto = DOCA_FLOW_PROTO_UDP;

		user_data[i]->entry_num = i;
		user_data[i]->port_id = 0;
		user_data[i]->status = status;

		/* Allocate CT entry */
		result = doca_flow_ct_entry_prepare(ct_queue,
						    NULL,
						    DOCA_FLOW_CT_ENTRY_FLAGS_ALLOC_ON_MISS,
						    &match_o,
						    flow_ct_hash_6tuple(&match_o, false, false),
						    &match_r,
						    flow_ct_hash_6tuple(&match_r, false, true),
						    &entry,
						    NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to prepare CT entry\n");
			return result;
		}
		result = doca_flow_ct_add_entry(ct_queue,
						NULL,
						flags,
						&match_o,
						&match_r,
						NULL,
						NULL,
						0,
						0,
						aging_sec,
						user_data[i],
						entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add CT aged entry: %s", doca_error_get_descr(result));
			return result;
		}
	}

	do {
		result = doca_flow_entries_process(port, ct_queue, DEFAULT_TIMEOUT_US, 0);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process CT aged entries: %s", doca_error_get_descr(result));
			return result;
		}
		if (status->failure) {
			DOCA_LOG_ERR("Failed to process entries, status is not success");
			return DOCA_ERROR_BAD_STATE;
		}
	} while (status->nb_processed < nb_aging_entries);

	return DOCA_SUCCESS;
}

/*
 * Run flow_ct_aging sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @ct_dev [in]: Flow CT device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t flow_ct_aging(uint16_t nb_queues, struct doca_dev *ct_dev)
{
	const int nb_ports = 2, nb_aged_entries = 32;
	int entry_idx, aged_entry_counter = 0;
	struct flow_resources resource;
	uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
	struct doca_flow_pipe *count_pipe, *ct_pipe = NULL, *udp_pipe;
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_meta o_zone_mask, o_modify_mask, r_zone_mask, r_modify_mask;
	struct aging_user_data *user_data[nb_aged_entries];
	struct doca_dev *dev_arr[nb_ports];
	struct entries_status ct_status;
	uint32_t ct_flags = 0, nb_arm_queues = 1, nb_ctrl_queues = 1, nb_user_actions = 0, nb_ipv4_sessions = 1024,
		 nb_ipv6_sessions = 0; /* On BF2 should always be 0 */
	uint16_t ct_queue = nb_queues;
	doca_error_t result;

	memset(&ct_status, 0, sizeof(ct_status));
	memset(&resource, 0, sizeof(resource));
	memset(user_data, 0, sizeof(struct aging_user_data *) * nb_aged_entries);

	resource.nr_counters = 1;

	result = init_doca_flow_cb(nb_queues,
				   "switch,hws",
				   &resource,
				   nr_shared_resources,
				   check_for_valid_entry_aging,
				   NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	/* Dont use zone masking */
	memset(&o_zone_mask, 0, sizeof(o_zone_mask));
	memset(&o_modify_mask, 0, sizeof(o_modify_mask));
	memset(&r_zone_mask, 0, sizeof(r_zone_mask));
	memset(&r_modify_mask, 0, sizeof(r_modify_mask));

	result = init_doca_flow_ct(ct_flags,
				   nb_arm_queues,
				   nb_ctrl_queues,
				   nb_user_actions,
				   NULL,
				   nb_ipv4_sessions,
				   nb_ipv6_sessions,
				   0,
				   false,
				   &o_zone_mask,
				   &o_modify_mask,
				   false,
				   &r_zone_mask,
				   &r_modify_mask);
	if (result != DOCA_SUCCESS) {
		doca_flow_destroy();
		return result;
	}

	memset(dev_arr, 0, sizeof(struct doca_dev *) * nb_ports);
	dev_arr[0] = ct_dev;
	result = init_doca_flow_ports(nb_ports, ports, false, dev_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_ct_destroy();
		doca_flow_destroy();
		return result;
	}

	result = create_count_pipe(ports[0], &count_pipe);
	if (result != DOCA_SUCCESS)
		goto cleanup;

	result = create_ct_pipe(ports[0], count_pipe, &ct_pipe);
	if (result != DOCA_SUCCESS)
		goto cleanup;

	result = add_age_ct_entries(ports[0], ct_queue, nb_aged_entries, &ct_status, user_data);
	if (result != DOCA_SUCCESS)
		goto cleanup;

	result = create_ct_root_pipe(ports[0], true, false, DOCA_FLOW_L4_META_UDP, ct_pipe, NULL, &udp_pipe);
	if (result != DOCA_SUCCESS)
		goto entries_cleanup;

	/* handle aging in loop until all entries aged out */
	while (aged_entry_counter < nb_aged_entries) {
		result = doca_flow_entries_process(ports[0], ct_queue, DEFAULT_TIMEOUT_US, 0);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process CT entries: %s", doca_error_get_descr(result));
			break;
		}
		sleep(0);
		result = handle_aged_flow(ports[0], ct_queue, &ct_status, &aged_entry_counter);
		if (result != DOCA_SUCCESS)
			break;
	}

entries_cleanup:
	for (entry_idx = 0; entry_idx < nb_aged_entries; entry_idx++) {
		if (user_data[entry_idx] != NULL)
			free(user_data[entry_idx]);
	}

cleanup:
	cleanup_procedure(ct_pipe, nb_ports, ports);
	return result;
}
