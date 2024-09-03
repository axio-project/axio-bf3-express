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

#ifndef CONFIG_H_
#define CONFIG_H_

#include <rte_hash.h>

#include "flow_decrypt.h"
#include "flow_encrypt.h"
#include "ipsec_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse IPv6 string
 *
 * @str_ip [in]: ipv6 string address
 * @ipv6_addr [out]: output parsed address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t parse_ipv6_str(const char *str_ip, doca_be32_t ipv6_addr[]);

/*
 * Parse the json input file and store the parsed rules values in rules array
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_parse_config(struct ipsec_security_gw_config *app_cfg);

/*
 * Register the command line parameters for the IPsec Security Gateway application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_ipsec_security_gw_params(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CONFIG_H_ */
