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

DOCA_LOG_REGISTER(FLOW_PIPE_RESIZE);

#define PIPE_SIZE	10
#define PERCENTAGE	80
#define MAX_ENTRIES	80
static bool resize_cb_received;
static bool congestion_notified;

static int congestion_reached_flag;

static struct doca_flow_port *ports[3];
static struct doca_flow_pipe_entry *entry[MAX_ENTRIES];
struct entry_ctx {
	struct entries_status entry_status;	/* Entry status */
	uint32_t index;				/* Entry index */
};
static struct entry_ctx entry_ctx[MAX_ENTRIES];

/*
 * pipe number of entries changed callback
 *
 * @pipe_user_ctx [in]: DOCA Flow pipe user context pointer
 * @nr_entries [in]: DOCA Flow new number of entries
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
pipe_resize_sample_nr_entries_changed_cb(void *pipe_user_ctx, uint32_t nr_entries)
{
	DOCA_LOG_INFO("Pipe user context %p: entries increased to %u",
		      pipe_user_ctx, nr_entries);

	return DOCA_SUCCESS;
}

/*
 * pipe entry relocated callback
 *
 * @pipe_user_ctx [in]: DOCA Flow pipe user context pointer
 * @pipe_queue [in]: DOCA Flow pipe queue id
 * @entry_user_ctx [in]: DOCA Flow entry user context pointer
 * @new_entry_user_ctx [out]: DOCA Flow updated entry user context pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
pipe_resize_sample_entry_relocate_cb(void *pipe_user_ctx,
				     uint16_t pipe_queue,
				     void *entry_user_ctx,
				     void **new_entry_user_ctx)
{
	struct entry_ctx *entry_ctx = (struct entry_ctx *)entry_user_ctx;

	DOCA_LOG_INFO("Pipe %p entry context %p (%u) relocated on queue %u\n",
		pipe_user_ctx, entry_ctx, entry_ctx->index, pipe_queue);

	*new_entry_user_ctx = entry_ctx;

	return DOCA_SUCCESS;
}

static doca_flow_pipe_resize_nr_entries_changed_cb nr_entries_changed_cb =
	pipe_resize_sample_nr_entries_changed_cb;
static doca_flow_pipe_resize_entry_relocate_cb entry_relocation_cb =
	pipe_resize_sample_entry_relocate_cb;

/*
 * Create DOCA Flow control pipe
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_resizable_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "RESIZABLE_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;

	pipe_cfg.attr.is_resizable = true;
	pipe_cfg.attr.nb_flows = PIPE_SIZE;
	pipe_cfg.attr.congestion_level_threshold = PERCENTAGE;
	pipe_cfg.attr.is_resizable = true;
	pipe_cfg.attr.user_ctx = &congestion_reached_flag;

	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	return doca_flow_pipe_create(&pipe_cfg, NULL /* fwd */, NULL /* fwd_miss */, pipe);
}

/*
 * Remove DOCA Flow pipe entry from the resizeable control pipe.
 *
 * @pipe [in]: entry's pipe
 * @port_id [in]: port id of incoming packets
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static void
remove_resizable_pipe_entries(void)
{
	int i;
	int rc;

	for (i = 0; i < MAX_ENTRIES; i++) {
		rc = doca_flow_pipe_rm_entry(0, DOCA_FLOW_NO_WAIT, entry[i]);
		if (rc)
			DOCA_LOG_WARN("Failed removing entry %d %p", i, entry[i]);
	}
}

/*
 * Add DOCA Flow pipe entry to the resizeable control pipe that matches
 * multiple ipv4 traffic and forward to peer port.
 *
 * @pipe [in]: entry's pipe
 * @port_id [in]: port id of incoming packets
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_resizable_pipe_entries(struct doca_flow_pipe *pipe, int port_id, uint16_t nb_queues)
{
	int i;
	struct doca_flow_fwd fwd;
	int rc;
	uint16_t queue_id;

	struct doca_flow_match match;
	struct doca_flow_actions actions;
	doca_error_t result;

	/* example 5-tuple to forward */
	doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(1, 2, 3, 4);
	doca_be16_t dst_port = rte_cpu_to_be_16(80);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));

	/* 5 tuple match */
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.dst_ip = dst_ip_addr;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	/* forwarding traffic to other port */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	memset(entry_ctx, 0, sizeof(entry_ctx));
	for (i = 0; i < MAX_ENTRIES; i++)
		entry_ctx[i].index = i;

	for (i = 0; i < MAX_ENTRIES; i++) {
		match.outer.ip4.dst_ip++;
		result = doca_flow_pipe_control_add_entry(0 /* pipe_queue */, 1 /*priority */,
							  pipe, &match, NULL /* match_mask */,
							  &actions, NULL /* actions_mask */,
							  NULL /* action_descs */, NULL /* monitor */,
							  &fwd, &entry_ctx[i], &entry[i]);
		if (result != DOCA_SUCCESS)
			return result;

		if (congestion_notified == true) {
			DOCA_LOG_INFO("Calling resize on pipe %p", pipe);
			rc = doca_flow_pipe_resize(pipe, 50 /* New congestion in percentage */,
				nr_entries_changed_cb, entry_relocation_cb);

			if (rc == 0)
				DOCA_LOG_INFO("Pipe %p successfully called resize operation", pipe);
			else
				DOCA_LOG_WARN("Pipe %p call to resize failed. rc=%d", pipe, rc);

			do {
				for (queue_id = 0; queue_id < nb_queues; queue_id++) {
					result = doca_flow_entries_process(ports[0], queue_id, DEFAULT_TIMEOUT_US, 10);
					if (result != DOCA_SUCCESS) {
						DOCA_LOG_ERR("Failed to process entries on queue id %u: %s", queue_id, doca_error_get_descr(result));
						stop_doca_flow_ports(3, ports);
						doca_flow_destroy();
						return result;
					}
				}
			} while (resize_cb_received == false);
			resize_cb_received = false;
			congestion_notified = false;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * pipe process callback
 *
 * @pipe [in]: DOCA Flow pipe pointer
 * @status [in]: DOCA Flow pipe status
 * @op [in]: DOCA Flow pipe operation
 * @user_ctx [out]: user context
 */
static void
pipe_process_cb(struct doca_flow_pipe *pipe,
		enum doca_flow_pipe_status status,
		enum doca_flow_pipe_op op, void *user_ctx)
{
	const char *op_str;
	bool is_err = false;

	(void)user_ctx;
	if (status != DOCA_FLOW_PIPE_STATUS_SUCCESS)
		is_err = true;

	switch (op) {
	case DOCA_FLOW_PIPE_OP_CONGESTION_REACHED:
		op_str = "CONGESTION_REACHED";
		congestion_notified = true;
		break;
	case DOCA_FLOW_PIPE_OP_RESIZED:
		op_str = "RESIZED";
		resize_cb_received = true;
		break;
	default:
		op_str = "UNKNOWN";
		break;
	}

	if (is_err)
		DOCA_LOG_INFO("Pipe %p received a %s operation callback. Errors encountered", pipe, op_str);
	else
		DOCA_LOG_INFO("Pipe %p successfully received a %s operation callback", pipe, op_str);
}

/*
 * Run flow_pipe_resize sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_pipe_resize(uint16_t nb_queues)
{
	const int nb_ports = 3;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_pipe *resizable_pipe;
	doca_error_t result;
	int port_id;

	/* DOCA flow mode: switch, hardware steering, control pipe dynamic size */
	result = init_doca_flow_cb(nb_queues, "switch,hws,cpds", resource, nr_shared_resources, check_for_valid_entry, pipe_process_cb);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	port_id = 0;
	result = create_resizable_pipe(ports[port_id], &resizable_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create resizable pipe: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	resize_cb_received = false;
	congestion_notified = false;

	result = add_resizable_pipe_entries(resizable_pipe, port_id, nb_queues);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
		stop_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return result;
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(5);

	remove_resizable_pipe_entries();

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
