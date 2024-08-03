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

#ifndef NAT_CORE_H_
#define NAT_CORE_H_

#include <doca_flow.h>
#include "flow_parser.h"

#include <dpdk_utils.h>
#include <utils.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FILE_NAME 255
#define MAX_INTF_NAME 5	/* should be sf + max 3 digits sf num*/

enum nat_mode {
	STATIC = 0,		/* assign global ip address to each local ip address */
	DYNAMIC = 1,		/* assign global ip address from address pool for each new local ip address */
	PAT = 2,		/* assign global port to local port - use the same global address to all local addresses */
	NAT_INVALID_MODE = 3,
};


struct nat_cfg {
	enum nat_mode mode;				/* application NAT mode */
	int lan_intf_id;				/* lan interface id */
	int wan_intf_id;				/* wan interface id */
	char json_path[MAX_FILE_NAME];			/* Path to the JSON file with NAT rules */
	bool has_json;					/* true when a json file path was given */
};

struct nat_rule_match {
	doca_be32_t local_ip;	/* local network ip - packets comes from local network */
	int local_port;		/* local network port - packets comes from local network */
	doca_be32_t global_ip;	/* global network ip - packets comes from WAN */
	int global_port;	/* global network port - packets comes from WAN */
};

/* User context struct that will be used in entries process callback */
struct entries_status {
	bool is_failure;	/* Will be set to true if some entry status will not be success */
	int nb_processed;	/* Number of entries that was already processed */
};

/*
 * Init doca flow and ports
 *
 * @app_cfg [in]: app configuration values
 * @app_dpdk_config [in]: application DPDK configuration values
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
doca_error_t nat_init(struct nat_cfg *app_cfg, struct application_dpdk_config *app_dpdk_config);

/*
 * Register nat params into argp
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
doca_error_t register_nat_params(void);

/*
 * Parse and create nat rules from json
 *
 * @file_path [in]: json configuration file path
 * @mode [in]: nat mode
 * @n_rules [out]: number of rules
 * @nat_rules [out]: array of rules
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
doca_error_t parsing_nat_rules(char *file_path, enum nat_mode mode, int *n_rules, struct nat_rule_match **nat_rules);

/*
 * Create nat pipes
 *
 * @nat_rules [in]: nat rules array
 * @nat_num_rules [in]: number of rules
 * @app_cfg [in]: app configureation values
 * @nb_ports [in]: number of ports
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
doca_error_t nat_pipes_init(struct nat_rule_match *nat_rules, int nat_num_rules, struct nat_cfg *app_cfg, int nb_ports);

/*
 * Destroy doca ports and flow
 *
 * @nb_ports [in]: number of ports
 * @nat_rules [in]: nat rules array
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
void nat_destroy(int nb_ports, struct nat_rule_match *nat_rules);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NAT_CORE_H_ */
