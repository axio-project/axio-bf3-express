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
void
check_for_valid_entry(struct doca_flow_pipe_entry *entry, uint16_t pipe_queue,
		      enum doca_flow_entry_status status, enum doca_flow_entry_op op, void *user_ctx)
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

doca_error_t
init_doca_flow(int nb_queues, const char *mode, struct doca_flow_resources resource, uint32_t nr_shared_resources[])
{
	return init_doca_flow_cb(nb_queues, mode, resource, nr_shared_resources, check_for_valid_entry, NULL);
}

doca_error_t
init_doca_flow_cb(int nb_queues, const char *mode, struct doca_flow_resources resource,
		  uint32_t nr_shared_resources[], doca_flow_entry_process_cb cb,
		  doca_flow_pipe_process_cb pipe_process_cb)
{
	struct doca_flow_cfg flow_cfg;
	int shared_resource_idx;

	memset(&flow_cfg, 0, sizeof(flow_cfg));

	flow_cfg.queues = nb_queues;
	flow_cfg.mode_args = mode;
	flow_cfg.resource = resource;
	flow_cfg.cb = cb;
	flow_cfg.pipe_process_cb = pipe_process_cb;
	for (shared_resource_idx = 0; shared_resource_idx < DOCA_FLOW_SHARED_RESOURCE_MAX; shared_resource_idx++)
		flow_cfg.nr_shared_resources[shared_resource_idx] = nr_shared_resources[shared_resource_idx];
	return doca_flow_init(&flow_cfg);
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_doca_flow_port(int port_id, struct doca_flow_port **port)
{
	int max_port_str_len = 128;
	struct doca_flow_port_cfg port_cfg;
	char port_id_str[max_port_str_len];

	memset(&port_cfg, 0, sizeof(port_cfg));

	port_cfg.port_id = port_id;
	port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
	snprintf(port_id_str, max_port_str_len, "%d", port_cfg.port_id);
	port_cfg.devargs = port_id_str;
	return doca_flow_port_start(&port_cfg, port);
}

void
stop_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[])
{
	int portid;

	/*
	 * Stop the ports in reverse order, since in switch mode port 0
	 * is proxy port, and proxy port should stop as last.
	 */
	for (portid = nb_ports - 1; portid >= 0; portid--) {
		if (ports[portid] != NULL)
			doca_flow_port_stop(ports[portid]);
	}
}

doca_error_t
init_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[], bool is_hairpin)
{
	int portid;
	doca_error_t result;

	for (portid = 0; portid < nb_ports; portid++) {
		/* Create doca flow port */
		result = create_doca_flow_port(portid, &ports[portid]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to start port: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(portid + 1, ports);
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
