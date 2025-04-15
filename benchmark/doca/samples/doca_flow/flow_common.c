/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <rte_byteorder.h>

#include <doca_log.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(flow_common);

/*
 * Entry processing callback
 *
 * @entry [in]: DOCA Flow entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
void check_for_valid_entry(struct doca_flow_pipe_entry *entry,
			   uint16_t pipe_queue,
			   enum doca_flow_entry_status status,
			   enum doca_flow_entry_op op,
			   void *user_ctx)
{
	(void)entry;
	(void)op;
	(void)pipe_queue;
	struct entries_status *entry_status = (struct entries_status *)user_ctx;

	if (entry_status == NULL)
		return;
	if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
		entry_status->failure = true; /* set failure to true if processing failed */
	entry_status->nb_processed++;
}

doca_error_t init_doca_flow(int nb_queues,
			    const char *mode,
			    struct flow_resources *resource,
			    uint32_t nr_shared_resources[])
{
	return init_doca_flow_cb(nb_queues, mode, resource, nr_shared_resources, check_for_valid_entry, NULL, NULL);
}

doca_error_t init_doca_flow_with_defs(int nb_queues,
				      const char *mode,
				      struct flow_resources *resource,
				      uint32_t nr_shared_resources[],
				      struct doca_flow_definitions *defs)
{
	return init_doca_flow_cb(nb_queues, mode, resource, nr_shared_resources, check_for_valid_entry, NULL, defs);
}

doca_error_t init_doca_flow_cb(int nb_queues,
			       const char *mode,
			       struct flow_resources *resource,
			       uint32_t nr_shared_resources[],
			       doca_flow_entry_process_cb cb,
			       doca_flow_pipe_process_cb pipe_process_cb,
			       struct doca_flow_definitions *defs)
{
	struct doca_flow_cfg *flow_cfg;
	uint16_t qidx, rss_queues[nb_queues];
	struct doca_flow_resource_rss_cfg rss = {0};
	doca_error_t result, tmp_result;

	result = doca_flow_cfg_create(&flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	rss.nr_queues = nb_queues;
	for (qidx = 0; qidx < nb_queues; qidx++)
		rss_queues[qidx] = qidx;
	rss.queues_array = rss_queues;
	result = doca_flow_cfg_set_default_rss(flow_cfg, &rss);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg rss: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_pipe_queues(flow_cfg, nb_queues);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_queues: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_mode_args(flow_cfg, mode);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg mode_args: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_nr_counters(flow_cfg, resource->nr_counters);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_counters: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_nr_meters(flow_cfg, resource->nr_meters);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_meters: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_cb_entry_process(flow_cfg, cb);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg doca_flow_entry_process_cb: %s",
			     doca_error_get_descr(result));
		goto destroy_cfg;
	}

	result = doca_flow_cfg_set_cb_pipe_process(flow_cfg, pipe_process_cb);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_process_cb: %s", doca_error_get_descr(result));
		goto destroy_cfg;
	}

	for (int i = 0; i < SHARED_RESOURCE_NUM_VALUES; i++) {
		result = doca_flow_cfg_set_nr_shared_resource(flow_cfg, nr_shared_resources[i], i);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_shared_resources: %s",
				     doca_error_get_descr(result));
			goto destroy_cfg;
		}
	}

	if (defs) {
		result = doca_flow_cfg_set_definitions(flow_cfg, defs);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set doca_flow_cfg defs: %s", doca_error_get_descr(result));
			goto destroy_cfg;
		}
	}

	/* creating doca flow with defs */
	result = doca_flow_init(flow_cfg);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to initialize DOCA Flow: %s", doca_error_get_descr(result));
destroy_cfg:
	tmp_result = doca_flow_cfg_destroy(flow_cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy doca_flow_cfg: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @dev [in]: doca device to attach
 * @state [in]: port operation initial state
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_doca_flow_port(int port_id,
					  struct doca_dev *dev,
					  enum doca_flow_port_operation_state state,
					  struct doca_flow_port **port,
					  uint32_t actions_mem_size)
{
	int max_port_str_len = 128;
	struct doca_flow_port_cfg *port_cfg;
	char port_id_str[max_port_str_len];
	doca_error_t result, tmp_result;

	result = doca_flow_port_cfg_create(&port_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_port_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_port_cfg_set_dev(port_cfg, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg dev: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	snprintf(port_id_str, max_port_str_len, "%d", port_id);
	result = doca_flow_port_cfg_set_devargs(port_cfg, port_id_str);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg devargs: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_cfg_set_operation_state(port_cfg, state);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg operation state: %s", doca_error_get_descr(result));
		goto destroy_port_cfg;
	}

	result = doca_flow_port_cfg_set_actions_mem_size(port_cfg, actions_mem_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set actions memory size: %s", doca_error_get_descr(result));
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

doca_error_t stop_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[])
{
	int portid;
	doca_error_t ret, doca_error = DOCA_SUCCESS;

	/*
	 * Stop the ports in reverse order, since in switch mode port 0
	 * is proxy port, and proxy port should stop as last.
	 */
	for (portid = nb_ports - 1; portid >= 0; portid--) {
		if (ports[portid] != NULL) {
			ret = doca_flow_port_stop(ports[portid]);
			/* record first error */
			if (ret != DOCA_SUCCESS && doca_error == DOCA_SUCCESS)
				doca_error = ret;
		}
	}
	return doca_error;
}

doca_error_t init_doca_flow_ports_with_op_state(int nb_ports,
						struct doca_flow_port *ports[],
						bool is_hairpin,
						struct doca_dev *dev_arr[],
						enum doca_flow_port_operation_state *states,
						uint32_t actions_mem_size[])
{
	int portid;
	doca_error_t result;
	enum doca_flow_port_operation_state state;

	for (portid = 0; portid < nb_ports; portid++) {
		state = states ? states[portid] : DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE;
		/* Create doca flow port */
		result =
			create_doca_flow_port(portid, dev_arr[portid], state, &ports[portid], actions_mem_size[portid]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to start port: %s", doca_error_get_descr(result));
			if (portid != 0)
				stop_doca_flow_ports(portid, ports);
			return result;
		}
		/* Pair ports should be done in the following order: port0 with port1, port2 with port3 etc */
		if (!is_hairpin || !portid || !(portid % 2))
			continue;
		/* pair odd port with previous port */
		result = doca_flow_port_pair(ports[portid], ports[portid ^ 1]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to pair ports %u - %u", portid, portid ^ 1);
			stop_doca_flow_ports(portid + 1, ports);
			return result;
		}
	}
	return DOCA_SUCCESS;
}

doca_error_t init_doca_flow_ports(int nb_ports,
				  struct doca_flow_port *ports[],
				  bool is_hairpin,
				  struct doca_dev *dev_arr[],
				  uint32_t actions_mem_size[])
{
	return init_doca_flow_ports_with_op_state(nb_ports, ports, is_hairpin, dev_arr, NULL, actions_mem_size);
}

doca_error_t set_flow_pipe_cfg(struct doca_flow_pipe_cfg *cfg,
			       const char *name,
			       enum doca_flow_pipe_type type,
			       bool is_root)
{
	doca_error_t result;

	if (cfg == NULL) {
		DOCA_LOG_ERR("Failed to set DOCA Flow pipe configurations, cfg=NULL");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = doca_flow_pipe_cfg_set_name(cfg, name);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg name: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_pipe_cfg_set_type(cfg, type);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg type: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_pipe_cfg_set_is_root(cfg, is_root);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg is_root: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

doca_error_t flow_process_entries(struct doca_flow_port *port, struct entries_status *status, uint32_t nr_entries)
{
	doca_error_t result;

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, nr_entries);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process %u entries: %s", nr_entries, doca_error_get_descr(result));
		return result;
	}

	if (status->nb_processed != (int)nr_entries) {
		DOCA_LOG_ERR("Failed to process %u entries, nb_processed=%d", nr_entries, status->nb_processed);
		return DOCA_ERROR_BAD_STATE;
	}

	if (status->failure) {
		DOCA_LOG_ERR("Failed to process %u entries, status is failure", nr_entries);
		return DOCA_ERROR_BAD_STATE;
	}

	return DOCA_SUCCESS;
}
