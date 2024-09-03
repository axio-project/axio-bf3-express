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

#include <stdio.h>
#include <stdlib.h>

#include <rte_ethdev.h>

#include <doca_dpdk.h>
#include <doca_log.h>

#include "eth_rxq_common.h"

DOCA_LOG_REGISTER(ETH::RXQ::COMMON);

#define COUNTERS_NUM (1 << 19)

/*
 * Initalize DOCA Flow with the flags: VNF/Hardware Steering/Isolated
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_doca_flow(void)
{
	doca_error_t status;
	struct doca_flow_cfg rxq_flow_cfg = {
		.queues = 1,
		.mode_args = "vnf,hws,isolated",
		.resource.nb_counters = COUNTERS_NUM
	};

	status = doca_flow_init(&rxq_flow_cfg);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca flow, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Start DOCA Flow with desired port ID
 *
 * @dpdk_port_id [in]: DPDK port ID
 * @df_port [out]: DOCA Flow port to start
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
start_doca_flow_port(uint16_t dpdk_port_id, struct doca_flow_port **df_port)
{
	doca_error_t status;
	struct doca_flow_port_cfg port_cfg = {
		.port_id = dpdk_port_id,
		.type = DOCA_FLOW_PORT_DPDK_BY_ID
	};
	char port_id_str[128] = {};

	snprintf(port_id_str, 128, "%d", port_cfg.port_id);
	port_cfg.devargs = port_id_str;

	status = doca_flow_port_start(&port_cfg, df_port);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start doca flow, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Create root pipe and add an entry into desired RXQ queue
 *
 * @df_port [in]: DOCA Flow port to create root pipe in
 * @rxq_flow_queue_id [in]: Pointer to RXQ queue ID
 * @root_pipe [out]: DOCA Flow pipe to create
 * @root_entry [out]: DOCA Flow port entry to create
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_root_pipe(struct doca_flow_port *df_port, uint16_t *rxq_flow_queue_id,
		 struct doca_flow_pipe **root_pipe, struct doca_flow_pipe_entry **root_entry)
{
	doca_error_t status;
	struct doca_flow_match match_mask;
	struct doca_flow_monitor monitor;

	struct doca_flow_pipe_cfg pipe_cfg = {
		.attr.name = "ROOT_PIPE",
		.attr.type = DOCA_FLOW_PIPE_CONTROL,
		.attr.is_root = true,
		.port = df_port
	};

	memset(&monitor, 0, sizeof(monitor));
	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;
	memset(&match_mask, 0, sizeof(match_mask));
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.monitor = &monitor;

	status = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, root_pipe);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca flow pipe, err: %s", doca_error_get_name(status));
		return status;
	}

	struct doca_flow_match all_match;
	struct doca_flow_fwd all_fwd = {
		.type = DOCA_FLOW_FWD_RSS,
		.rss_queues = rxq_flow_queue_id,
		.num_of_queues = 1
	};

	memset(&all_match, 0, sizeof(all_match));

	status = doca_flow_pipe_control_add_entry(0, 0, *root_pipe, &all_match, NULL, NULL,
						  NULL, NULL, NULL, &all_fwd, NULL, root_entry);
	if (status != DOCA_SUCCESS) {
		doca_flow_pipe_destroy(*root_pipe);
		DOCA_LOG_ERR("Failed to add doca flow entry, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_flow_entries_process(df_port, 0, 10000, 4);
	if (status != DOCA_SUCCESS) {
		doca_flow_pipe_destroy(*root_pipe);
		DOCA_LOG_ERR("Failed to process doca flow entry, err: %s", doca_error_get_name(status));
		return status;
	}

	DOCA_LOG_INFO("Created Pipe %s", pipe_cfg.attr.name);

	return DOCA_SUCCESS;
}

doca_error_t
allocate_eth_rxq_flow_resources(struct eth_rxq_flow_config *cfg, struct eth_rxq_flow_resources *resources)
{
	doca_error_t status, clean_status;

	if (cfg == NULL || resources == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	status = init_doca_flow();
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate eth_rxq_flow_resources: failed to init DOCA flow, err: %s",
			doca_error_get_name(status));
		return status;
	}

	status = start_doca_flow_port(cfg->dpdk_port_id, &(resources->df_port));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate eth_rxq_flow_resources: failed to init DOCA flow port, err: %s",
			doca_error_get_name(status));
		goto destroy_doca_flow;
	}

	status = create_root_pipe(resources->df_port, &(cfg->rxq_flow_queue_id),
				  &(resources->root_pipe), &(resources->root_entry));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate eth_rxq_flow_resources: failed to create root pipe, err: %s",
			doca_error_get_name(status));
		goto destroy_doca_flow_port;
	}

	return DOCA_SUCCESS;

destroy_doca_flow_port:
	clean_status = doca_flow_port_stop(resources->df_port);
	if (clean_status != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to stop DOCA flow port, err: %s", doca_error_get_name(clean_status));
destroy_doca_flow:
	doca_flow_destroy();

	return status;
}

doca_error_t
destroy_eth_rxq_flow_resources(struct eth_rxq_flow_resources *resources)
{
	doca_error_t status;

	if (resources->root_pipe != NULL)
		doca_flow_pipe_destroy(resources->root_pipe);

	if (resources->df_port != NULL) {
		status = doca_flow_port_stop(resources->df_port);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to stop DOCA flow port, err: %s", doca_error_get_name(status));
			return status;
		}

		doca_flow_destroy();
	}

	return DOCA_SUCCESS;
}

doca_error_t
init_dpdk_port(struct doca_dev *device, uint16_t *dpdk_port_id)
{
	doca_error_t status;
	int res;

	status = doca_dpdk_port_probe(device, "dv_flow_en=2");
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to probe dpdk port, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_dpdk_get_first_port_id(device, dpdk_port_id);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get dpdk port ID, err: %s", doca_error_get_name(status));
		return status;
	}

	struct rte_flow_error error;
	struct rte_eth_conf eth_conf;
	struct rte_eth_dev_info dev_info;

	res = rte_eth_dev_info_get(*dpdk_port_id, &dev_info);
	if (res) {
		DOCA_LOG_ERR("Failed to get rte device info: %s", rte_strerror(-res));
		return DOCA_ERROR_DRIVER;
	}

	memset(&eth_conf, 0, sizeof(eth_conf));
	res = rte_eth_dev_configure(*dpdk_port_id, 0, 0, &eth_conf);
	if (res) {
		DOCA_LOG_ERR("Failed to configure dpdk port: %s", rte_strerror(-res));
		return DOCA_ERROR_DRIVER;
	}

	res = rte_flow_isolate(*dpdk_port_id, 1, &error);
	if (res) {
		DOCA_LOG_ERR("Failed to configure dpdk port: %s", error.message);
		return DOCA_ERROR_DRIVER;
	}

	res = rte_eth_dev_start(*dpdk_port_id);
	if (res) {
		DOCA_LOG_ERR("Failed to start dpdk port: %s", rte_strerror(-res));
		return DOCA_ERROR_DRIVER;
	}

	return DOCA_SUCCESS;
}
