/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <rte_ethdev.h>

#include <doca_log.h>

#include "utils.h"
#include "dpdk_utils.h"
#include "flow_pipes_manager.h"
#include "switch_core.h"

DOCA_LOG_REGISTER(SWITCH::Core);

#define MAX_PORT_STR_LEN 128	   /* Maximal length of port name */
#define DEFAULT_TIMEOUT_US (10000) /* Timeout for processing pipe entries */

static struct flow_pipes_manager *pipes_manager;

/* user context struct that will be used in entries process callback */
struct entries_status {
	bool failure;	  /* will be set to true if some entry status will not be success */
	int nb_processed; /* will hold the number of entries that was already processed */
};

/*
 * Entry processing callback
 *
 * @entry [in]: DOCA Flow entry
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
static void check_for_valid_entry(struct doca_flow_pipe_entry *entry,
				  uint16_t pipe_queue,
				  enum doca_flow_entry_status status,
				  enum doca_flow_entry_op op,
				  void *user_ctx)
{
	(void)entry;
	(void)op;
	(void)pipe_queue;

	struct entries_status *entry_status = (struct entries_status *)user_ctx;

	if (entry_status == NULL || op != DOCA_FLOW_ENTRY_OP_ADD)
		return;
	if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Entry processing failed. entry_op=%d", op);
		entry_status->failure = true; /* Set is_failure to true if processing failed */
	}
	entry_status->nb_processed++;
}

/*
 * Create DOCA Flow pipe
 *
 * @cfg [in]: DOCA Flow pipe configuration
 * @port_id [in]: Not being used
 * @fwd [in]: DOCA Flow forward
 * @fw_pipe_id [in]: Pipe ID to forward
 * @fwd_miss [in]: DOCA Flow forward miss
 * @fw_miss_pipe_id [in]: Pipe ID to forward miss
 */
static void pipe_create(struct doca_flow_pipe_cfg *cfg,
			uint16_t port_id,
			struct doca_flow_fwd *fwd,
			uint64_t fw_pipe_id,
			struct doca_flow_fwd *fwd_miss,
			uint64_t fw_miss_pipe_id)
{
	(void)port_id;

	struct doca_flow_pipe *pipe;
	uint64_t pipe_id;
	uint16_t switch_mode_port_id = 0;
	doca_error_t result;

	DOCA_LOG_DBG("Create pipe is being called");

	result = doca_flow_pipe_cfg_set_enable_strict_matching(cfg, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg enable_strict_matching: %s",
			     doca_error_get_descr(result));
		return;
	}

	if (fwd != NULL && fwd->type == DOCA_FLOW_FWD_PIPE) {
		result = pipes_manager_get_pipe(pipes_manager, fw_pipe_id, &fwd->next_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to find relevant fwd pipe id=%" PRIu64, fw_pipe_id);
			return;
		}
	}

	if (fwd_miss != NULL && fwd_miss->type == DOCA_FLOW_FWD_PIPE) {
		result = pipes_manager_get_pipe(pipes_manager, fw_miss_pipe_id, &fwd_miss->next_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to find relevant fwd_miss pipe id=%" PRIu64, fw_miss_pipe_id);
			return;
		}
	}

	result = doca_flow_pipe_create(cfg, fwd, fwd_miss, &pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Pipe creation failed: %s", doca_error_get_descr(result));
		return;
	}

	if (pipes_manager_pipe_create(pipes_manager, pipe, switch_mode_port_id, &pipe_id) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Flow Pipes Manager failed to add pipe");
		doca_flow_pipe_destroy(pipe);
		return;
	}

	DOCA_LOG_INFO("Pipe created successfully with id: %" PRIu64, pipe_id);
}

/*
 * Add DOCA Flow entry
 *
 * @pipe_queue [in]: Queue identifier
 * @pipe_id [in]: Pipe ID
 * @match [in]: DOCA Flow match
 * @actions [in]: Pipe ID to actions
 * @monitor [in]: DOCA Flow monitor
 * @fwd [in]: DOCA Flow forward
 * @fw_pipe_id [in]: Pipe ID to forward
 * @flags [in]: Hardware steering flag, current implementation supports DOCA_FLOW_NO_WAIT only
 */
static void pipe_add_entry(uint16_t pipe_queue,
			   uint64_t pipe_id,
			   struct doca_flow_match *match,
			   struct doca_flow_actions *actions,
			   struct doca_flow_monitor *monitor,
			   struct doca_flow_fwd *fwd,
			   uint64_t fw_pipe_id,
			   uint32_t flags)
{
	(void)pipe_queue;

	struct doca_flow_pipe *pipe;
	struct doca_flow_pipe_entry *entry;
	uint64_t entry_id;
	doca_error_t result;
	struct entries_status status = {0};
	int num_of_entries = 1;
	uint32_t hws_flag = flags;

	DOCA_LOG_DBG("Add entry is being called");

	if (fwd != NULL && fwd->type == DOCA_FLOW_FWD_PIPE) {
		result = pipes_manager_get_pipe(pipes_manager, fw_pipe_id, &fwd->next_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to find relevant fwd pipe with id %" PRIu64, fw_pipe_id);
			return;
		}
	}

	result = pipes_manager_get_pipe(pipes_manager, pipe_id, &pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find pipe with id %" PRIu64 " to add entry into", pipe_id);
		return;
	}

	if (hws_flag != DOCA_FLOW_NO_WAIT) {
		DOCA_LOG_DBG("Batch insertion of pipe entries is not supported");
		hws_flag = DOCA_FLOW_NO_WAIT;
	}

	result = doca_flow_pipe_add_entry(0, pipe, match, actions, monitor, fwd, hws_flag, &status, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry creation failed: %s", doca_error_get_descr(result));
		return;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(NULL), 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry creation FAILED: %s", doca_error_get_descr(result));
		return;
	}

	if (status.nb_processed != num_of_entries || status.failure) {
		DOCA_LOG_ERR("Entry creation failed");
		return;
	}

	if (pipes_manager_pipe_add_entry(pipes_manager, entry, pipe_id, &entry_id) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Flow Pipes Manager failed to add entry");
		doca_flow_pipe_remove_entry(0, DOCA_FLOW_NO_WAIT, entry);
		return;
	}

	DOCA_LOG_INFO("Entry created successfully with id: %" PRIu64, entry_id);
}

/*
 * Add DOCA Flow control pipe entry
 *
 * @pipe_queue [in]: Queue identifier
 * @priority [in]: Entry priority
 * @pipe_id [in]: Pipe ID
 * @match [in]: DOCA Flow match
 * @match_mask [in]: DOCA Flow match mask
 * @fwd [in]: DOCA Flow forward
 * @fw_pipe_id [in]: Pipe ID to forward
 */
static void pipe_control_add_entry(uint16_t pipe_queue,
				   uint8_t priority,
				   uint64_t pipe_id,
				   struct doca_flow_match *match,
				   struct doca_flow_match *match_mask,
				   struct doca_flow_fwd *fwd,
				   uint64_t fw_pipe_id)
{
	(void)pipe_queue;

	struct doca_flow_pipe *pipe;
	struct doca_flow_pipe_entry *entry;
	uint64_t entry_id;
	doca_error_t result;

	DOCA_LOG_DBG("Add control pipe entry is being called");

	if (fwd != NULL && fwd->type == DOCA_FLOW_FWD_PIPE) {
		result = pipes_manager_get_pipe(pipes_manager, fw_pipe_id, &fwd->next_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to find relevant fwd pipe id=%" PRIu64, fw_pipe_id);
			return;
		}
	}

	result = pipes_manager_get_pipe(pipes_manager, pipe_id, &pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find relevant pipe id=%" PRIu64 " to add entry into", pipe_id);
		return;
	}

	result = doca_flow_pipe_control_add_entry(0,
						  priority,
						  pipe,
						  match,
						  match_mask,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  fwd,
						  NULL,
						  &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry creation for control pipe failed: %s", doca_error_get_descr(result));
		return;
	}

	if (pipes_manager_pipe_add_entry(pipes_manager, entry, pipe_id, &entry_id) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Flow Pipes Manager failed to add control pipe entry");
		doca_flow_pipe_remove_entry(0, DOCA_FLOW_NO_WAIT, entry);
		return;
	}

	DOCA_LOG_INFO("Control pipe entry created successfully with id: %" PRIu64, entry_id);
}

/*
 * Destroy DOCA Flow pipe
 *
 * @pipe_id [in]: Pipe ID to destroy
 */
static void pipe_destroy(uint64_t pipe_id)
{
	struct doca_flow_pipe *pipe;

	DOCA_LOG_DBG("Destroy pipe is being called");

	if (pipes_manager_get_pipe(pipes_manager, pipe_id, &pipe) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find pipe id %" PRIu64 " to destroy", pipe_id);
		return;
	}

	if (pipes_manager_pipe_destroy(pipes_manager, pipe_id) == DOCA_SUCCESS)
		doca_flow_pipe_destroy(pipe);
}

/*
 * Remove DOCA Flow entry
 *
 * @pipe_queue [in]: Queue identifier
 * @entry_id [in]: Entry ID to remove
 * @flags [in]: Hardware steering flag, current implementation supports DOCA_FLOW_NO_WAIT only
 */
static void pipe_rm_entry(uint16_t pipe_queue, uint64_t entry_id, uint32_t flags)
{
	(void)pipe_queue;

	struct doca_flow_pipe_entry *entry;
	doca_error_t result;
	uint32_t hws_flag = flags;

	DOCA_LOG_DBG("Remove entry is being called");

	if (pipes_manager_get_entry(pipes_manager, entry_id, &entry) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find entry id %" PRIu64 " to remove", entry_id);
		return;
	}

	if (hws_flag != DOCA_FLOW_NO_WAIT) {
		DOCA_LOG_DBG("Batch insertion of pipe entries is not supported");
		hws_flag = DOCA_FLOW_NO_WAIT;
	}

	if (pipes_manager_pipe_rm_entry(pipes_manager, entry_id) == DOCA_SUCCESS) {
		result = doca_flow_pipe_remove_entry(0, hws_flag, entry);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to remove entry");
	}
}

/*
 * DOCA Flow port pipes flush
 *
 * @port_id [in]: Port ID to flush
 */
static void port_pipes_flush(uint16_t port_id)
{
	uint16_t switch_mode_port_id = 0;

	DOCA_LOG_DBG("Pipes flush is being called");

	if (port_id != switch_mode_port_id) {
		DOCA_LOG_ERR("Switch mode port id is 0 only");
		return;
	}

	if (pipes_manager_pipes_flush(pipes_manager, switch_mode_port_id) == DOCA_SUCCESS)
		doca_flow_port_pipes_flush(doca_flow_port_switch_get(NULL));
}

/*
 * DOCA Flow query
 *
 * @entry_id [in]: Entry to query
 * @stats [in]: Query statistics
 */
static void flow_query(uint64_t entry_id, struct doca_flow_resource_query *stats)
{
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	DOCA_LOG_DBG("Query is being called");

	if (pipes_manager_get_entry(pipes_manager, entry_id, &entry) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find entry id %" PRIu64 " to query on", entry_id);
		return;
	}

	result = doca_flow_resource_query_entry(entry, stats);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Query on entry failed");
}

/*
 * DOCA Flow port pipes dump
 *
 * @port_id [in]: Port ID to dump
 * @fd [in]: File to dump information into
 */
static void port_pipes_dump(uint16_t port_id, FILE *fd)
{
	uint16_t switch_mode_port_id = 0;

	DOCA_LOG_DBG("Pipes dump is being called");

	if (port_id != switch_mode_port_id) {
		DOCA_LOG_ERR("Switch mode port id is 0 only");
		return;
	}

	doca_flow_port_pipes_dump(doca_flow_port_switch_get(NULL), fd);
}

/*
 * Register all application's relevant function to flow parser module
 */
static void register_actions_on_flow_parser(void)
{
	set_pipe_create(pipe_create);
	set_pipe_add_entry(pipe_add_entry);
	set_pipe_control_add_entry(pipe_control_add_entry);
	set_pipe_destroy(pipe_destroy);
	set_pipe_rm_entry(pipe_rm_entry);
	set_port_pipes_flush(port_pipes_flush);
	set_query(flow_query);
	set_port_pipes_dump(port_pipes_dump);
}

/*
 * Stop application's ports
 *
 * @nb_ports [in]: Number of ports to stop
 * @ports [in]: Port array
 */
static void ports_stop(int nb_ports, struct doca_flow_port **ports)
{
	int portid;
	struct doca_flow_port *port;

	for (portid = 0; portid < nb_ports; portid++) {
		port = ports[portid];
		if (port != NULL)
			doca_flow_port_stop(port);
	}
}

/*
 * Create application port
 *
 * @portid [in]: Port ID
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t port_create(uint8_t portid, struct doca_flow_port **port)
{
	struct doca_flow_port_cfg *port_cfg;
	char port_id_str[MAX_PORT_STR_LEN];
	doca_error_t result, tmp_result;

	result = doca_flow_port_cfg_create(&port_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_port_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	snprintf(port_id_str, MAX_PORT_STR_LEN, "%d", portid);
	result = doca_flow_port_cfg_set_devargs(port_cfg, port_id_str);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg devargs: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_start(port_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start doca_flow port: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

destroy_port_cfg:
	tmp_result = doca_flow_port_cfg_destroy(port_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy doca_flow port: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

/*
 * Initialize application's ports
 *
 * @nb_ports [in]: Number of ports to init
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t init_ports(int nb_ports)
{
	int portid;
	struct doca_flow_port *ports[nb_ports];
	doca_error_t result;

	for (portid = 0; portid < nb_ports; portid++) {
		result = port_create(portid, &ports[portid]);
		if (result != DOCA_SUCCESS) {
			ports_stop(portid, ports);
			return result;
		}
	}

	return DOCA_SUCCESS;
}

void switch_ports_count(struct application_dpdk_config *app_dpdk_config)
{
	int nb_ports;

	nb_ports = rte_eth_dev_count_avail();
	app_dpdk_config->port_config.nb_ports = nb_ports;
	DOCA_LOG_DBG("Initialize Switch with %d ports", nb_ports);
}

doca_error_t switch_init(struct application_dpdk_config *app_dpdk_config)
{
	struct doca_flow_cfg *flow_cfg;
	uint16_t rss_queues[app_dpdk_config->port_config.nb_queues];
	struct doca_flow_resource_rss_cfg rss = {0};
	doca_error_t result;

	/* Initialize doca flow framework */
	/* Initialize doca flow framework */
	result = doca_flow_cfg_create(&flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_cfg_set_pipe_queues(flow_cfg, app_dpdk_config->port_config.nb_queues);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_queues: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(flow_cfg);
		return result;
	}

	result = doca_flow_cfg_set_mode_args(flow_cfg, "switch,hws");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg mode_args: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(flow_cfg);
		return result;
	}

	result = doca_flow_cfg_set_cb_entry_process(flow_cfg, check_for_valid_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg doca_flow_entry_process_cb: %s",
			     doca_error_get_descr(result));
		doca_flow_cfg_destroy(flow_cfg);
		return result;
	}

	rss.nr_queues = app_dpdk_config->port_config.nb_queues;
	linear_array_init_u16(rss_queues, rss.nr_queues);
	rss.queues_array = rss_queues;
	result = doca_flow_cfg_set_default_rss(flow_cfg, &rss);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg rss: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(flow_cfg);
		return result;
	}

	result = doca_flow_init(flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca: %s", doca_error_get_descr(result));
		return DOCA_ERROR_INITIALIZATION;
	}

	result = doca_flow_cfg_destroy(flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy doca_flow_cfg: %s", doca_error_get_descr(result));
		return DOCA_ERROR_INITIALIZATION;
	}

	result = init_ports(app_dpdk_config->port_config.nb_ports);
	if (result != DOCA_SUCCESS) {
		doca_flow_destroy();
		DOCA_LOG_ERR("Failed to init ports: %s", doca_error_get_descr(result));
		return DOCA_ERROR_INITIALIZATION;
	}

	result = create_pipes_manager(&pipes_manager);
	if (result != DOCA_SUCCESS) {
		doca_flow_destroy();
		DOCA_LOG_ERR("Failed to create pipes manager: %s", doca_error_get_descr(result));
		return DOCA_ERROR_INITIALIZATION;
	}

	register_actions_on_flow_parser();
	return DOCA_SUCCESS;
}

void switch_destroy(void)
{
	destroy_pipes_manager(pipes_manager);
}
