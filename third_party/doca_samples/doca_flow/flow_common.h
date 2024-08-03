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

#ifndef FLOW_COMMON_H_
#define FLOW_COMMON_H_

#include <rte_byteorder.h>

#include <doca_flow.h>

#define BE_IPV4_ADDR(a, b, c, d) (RTE_BE32(((uint32_t)a << 24) + (b << 16) + (c << 8) + d))	/* create IPV4 address */
#define SET_MAC_ADDR(addr, a, b, c, d, e, f)\
do {\
	addr[0] = a & 0xff;\
	addr[1] = b & 0xff;\
	addr[2] = c & 0xff;\
	addr[3] = d & 0xff;\
	addr[4] = e & 0xff;\
	addr[5] = f & 0xff;\
} while (0)									/* create source mac address */
#define BUILD_VNI(uint24_vni) (RTE_BE32((uint32_t)uint24_vni << 8))		/* create VNI */
#define DEFAULT_TIMEOUT_US (10000)						/* default timeout for processing entries */
#define NB_ACTIONS_ARR (1)							/* default length for action array */

/* user context struct that will be used in entries process callback */
struct entries_status {
	bool failure;		/* will be set to true if some entry status will not be success */
	int nb_processed;	/* will hold the number of entries that was already processed */
};

/*
 * Initialize DOCA Flow library
 *
 * @nb_queues [in]: number of queues the sample will use
 * @mode [in]: doca flow architecture mode
 * @resource [in]: number of meters and counters to configure
 * @nr_shared_resources [in]: total shared resource per type
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t init_doca_flow(int nb_queues, const char *mode, struct doca_flow_resources resource,
			    uint32_t nr_shared_resources[]);

/*
 * Initialize DOCA Flow library with callback
 *
 * @nb_queues [in]: number of queues the sample will use
 * @mode [in]: doca flow architecture mode
 * @resource [in]: number of meters and counters to configure
 * @nr_shared_resources [in]: total shared resource per type
 * @cb [in]: process callback pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t init_doca_flow_cb(int nb_queues, const char *mode, struct doca_flow_resources resource,
			    uint32_t nr_shared_resources[], doca_flow_entry_process_cb cb);

/*
 * Initialize DOCA Flow ports
 *
 * @nb_ports [in]: number of ports to create
 * @ports [in]: array of ports to create
 * @is_hairpin [in]: port pair should run if is_hairpin = true
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t init_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[], bool is_hairpin);

/*
 * Stop DOCA Flow ports
 *
 * @nb_ports [in]: number of ports to stop
 * @ports [in]: array of ports to stop
 */
void stop_doca_flow_ports(int nb_ports, struct doca_flow_port *ports[]);

#endif /* FLOW_COMMON_H_ */
