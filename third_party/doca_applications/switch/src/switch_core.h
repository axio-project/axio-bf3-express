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

#ifndef SWITCH_CORE_H_
#define SWITCH_CORE_H_

#include "flow_parser.h"

/*
 * Count the total number of ports
 *
 * @app_dpdk_config [out]: application DPDK configuration values
 */
void switch_ports_count(struct application_dpdk_config *app_dpdk_config);

/*
 * Initialize Switch application
 *
 * @app_dpdk_config [in]: application DPDK configuration values
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t switch_init(struct application_dpdk_config *app_dpdk_config);

/*
 * Destroy Switch application resources
 */
void switch_destroy(void);

#endif /* SWITCH_CORE_H_ */
