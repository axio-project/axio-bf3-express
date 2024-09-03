/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef FIREWALL_CORE_H_
#define FIREWALL_CORE_H_

#include <doca_flow_grpc_client.h>
#include <doca_argp.h>

#include "utils.h"
#include "flow_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FILE_NAME 255	/* Maximum file name length */

/* Firewall running mode */
enum firewall_running_mode {
	FIREWALL_MODE_INVALID = 0,	/* Invalid mode */
	FIREWALL_MODE_STATIC,		/* Static running mode, need to provide the app a rules file */
	FIREWALL_MODE_INTERACTIVE,	/* Interactive running mode, adding rules in runtime from the command line */
};

/* Firewall configuration struct */
struct firewall_cfg {
	enum firewall_running_mode mode;	/* Application running mode */
	char json_path[MAX_FILE_NAME];		/* Path to the JSON file with 5-tuple rules to drop */
	bool has_json;				/* true when a json file path was given */
};

/* rule 5 tuple match struct */
struct rule_match {
	enum doca_flow_l4_type_ext protocol;	/* protocol */
	doca_be32_t src_ip;			/* source IP */
	doca_be32_t dst_ip;			/* destination IP */
	int src_port;				/* source port */
	int dst_port;				/* destination port */
};

/* ports pipe ID struct */
struct port_pipe_ids {
	uint64_t transport_pipe_id;			/* TRANSPORT pipe id */
};

/*
 * Register the command line parameters for the Firewall application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_firewall_params(void);

/*
 * Parse the json input file and store the parsed rules values in drop_rules array
 *
 * @file_path [in]: json file path with 5 tuple rules to add
 * @n_rules [out]: pointer to the number of rules in the file
 * @drop_rules [out]: pointer to array of initalized rules
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_drop_rules(char *file_path, int *n_rules, struct rule_match **drop_rules);

/*
 * Open a gRPC channel over doca_flow_grpc, and send requests to initialize DOCA Flow ports
 *
 * @grpc_address [in]: String representing the server's IP address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t firewall_ports_init(const char *grpc_address);

/*
 * Build Firewall pipes
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t firewall_pipes_init(void);

/*
 * Add Firewall drop rules parsed from json file
 *
 * @drop_rules [in]: array of parsed rules to add to the drop pipes
 * @n_rules [in]: number of rules in the drop_rules array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t firewall_add_drop_rules(struct rule_match *drop_rules, int n_rules);

/*
 * Firewall ports stop
 *
 * @nb_ports [in]: number of running ports to stop
 */
void firewall_ports_stop(int nb_ports);

/*
 * Set all the functions to be invoked by flow parser command line
 */
void register_actions_on_flow_parser(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIREWALL_CORE_H_ */
