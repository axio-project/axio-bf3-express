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

#ifndef ETH_RXQ_COMMON_H_
#define ETH_RXQ_COMMON_H_

#include <unistd.h>

#include <doca_flow.h>
#include <doca_dev.h>
#include <doca_error.h>

struct eth_rxq_flow_resources {
	struct doca_flow_port *df_port;				/* DOCA flow port */
	struct doca_flow_pipe *root_pipe;			/* DOCA flow root pipe*/
	struct doca_flow_pipe_entry *root_entry;		/* DOCA flow root pipe entry*/
};

struct eth_rxq_flow_config {
	uint16_t dpdk_port_id;					/* Device DPDK port ID */
	uint16_t rxq_flow_queue_id;				/* DOCA ETH RXQ's flow queue ID */
};

/*
 * Allocate DOCA flow resources for ETH RXQ sample
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: DOCA flow resources for ETH RXQ sample to allocate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allocate_eth_rxq_flow_resources(struct eth_rxq_flow_config *cfg,
					     struct eth_rxq_flow_resources *resources);

/*
 * Destroy DOCA flow resources for ETH RXQ sample
 *
 * @resources [in]: DOCA flow resources for ETH RXQ sample to destroy
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_eth_rxq_flow_resources(struct eth_rxq_flow_resources *resources);

/*
 * Initalize DPDK port
 *
 * @device [in]: DOCA device to open its DPDK port
 * @dpdk_port_id [in/out]: DPDk port ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_dpdk_port(struct doca_dev *device, uint16_t *dpdk_port_id);

#endif /* ETH_RXQ_COMMON_H_ */
