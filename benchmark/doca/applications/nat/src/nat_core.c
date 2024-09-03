/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <unistd.h>

#include <json-c/json.h>
#include <rte_byteorder.h>
#include <rte_ethdev.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <utils.h>

#include "nat_core.h"

DOCA_LOG_REGISTER(NAT_CORE);

#define BE_IPV4_ADDR(a, b, c, d) (RTE_BE32(((a) << 24) + ((b) << 16) + ((c) << 8) + (d)))	/* Convert IPv4 address to big endian */
#define MAX_PORT_STR 128									/* Maximum length of the string name of the port */
#define NAT_PORTS_NUM 2										/* number of needed port for NAT application */
#define MAX_PORT_STR_LEN 128									/* Maximal length of port name */
#define DEFAULT_TIMEOUT_US (10000)								/* Timeout for processing pipe entries */
#define NUM_OF_SUPPORTED_PROTOCOLS 2								/* number of support L4 protocols */
#define NB_ACTIONS_ARR (1)									/* default number of actions in pipe */
#define MAX_PORT_NAME 30									/* Maximal length of port name */
#define QUEUE_DEPTH 256										/* DOCA Flow queue depth */

static struct doca_flow_port *ports[NAT_PORTS_NUM];

/*
 * ARGP Callback - Handle nat mode parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
nat_mode_callback(void *param, void *config)
{
	struct nat_cfg *nat_cfg = (struct nat_cfg *)config;
	char *mode = (char *) param;

	if (strcmp(mode, "static") == 0)
		nat_cfg->mode = STATIC;
	else if (strcmp(mode, "pat") == 0)
		nat_cfg->mode = PAT;
	else {
		nat_cfg->mode = NAT_INVALID_MODE;
		DOCA_LOG_ERR("Illegal nat mode = %s", mode);
		return DOCA_ERROR_INVALID_VALUE;
	}
	DOCA_LOG_DBG("Mode = %s, app_cfg mode = %d", mode, nat_cfg->mode);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle lan interface parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
lan_intf_callback(void *param, void *config)
{
	struct nat_cfg *nat_cfg = (struct nat_cfg *)config;
	char *lan_intf = (char *)param;

	if (strnlen(lan_intf, MAX_INTF_NAME+1) == MAX_INTF_NAME+1) {
		DOCA_LOG_ERR("LAN interface name is too long - MAX=%d", MAX_INTF_NAME);
		return DOCA_ERROR_INVALID_VALUE;
	}

	if (strstr(lan_intf, "sf") != lan_intf) {
		DOCA_LOG_ERR("LAN interface expected format sfxxx");
		return DOCA_ERROR_INVALID_VALUE;
	}

	nat_cfg->lan_intf_id = atoi(&lan_intf[2]);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle wan interface parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
wan_intf_callback(void *param, void *config)
{
	struct nat_cfg *nat_cfg = (struct nat_cfg *)config;
	char *wan_intf = (char *)param;

	if (strnlen(wan_intf, MAX_INTF_NAME+1) == MAX_INTF_NAME+1) {
		DOCA_LOG_ERR("WAN interface name is too long - MAX=%d", MAX_INTF_NAME);
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (strstr(wan_intf, "sf") != wan_intf) {
		DOCA_LOG_ERR("WAN interface expected format - sfxxx");
		return DOCA_ERROR_INVALID_VALUE;
	}

	nat_cfg->wan_intf_id = atoi(&wan_intf[2]);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle nat rules config file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
nat_rules_callback(void *param, void *config)
{
	struct nat_cfg *nat_cfg = (struct nat_cfg *)config;
	char *json_path = (char *)param;

	if (strnlen(json_path, MAX_FILE_NAME) == MAX_FILE_NAME) {
		DOCA_LOG_ERR("JSON file name is too long - MAX=%d", MAX_FILE_NAME - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (access(json_path, F_OK) == -1) {
		DOCA_LOG_ERR("JSON file was not found %s", json_path);
		return DOCA_ERROR_NOT_FOUND;
	}
	strlcpy(nat_cfg->json_path, json_path, MAX_FILE_NAME);
	nat_cfg->has_json = true;
	return DOCA_SUCCESS;
}

/*
 * ARGP validation Callback - check if lan and wan sfs are different
 *
 * @config [in]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
nat_args_validation_callback(void *config)
{
	struct nat_cfg *nat_cfg = (struct nat_cfg *) config;

	if (nat_cfg->wan_intf_id == nat_cfg->lan_intf_id) {
		DOCA_LOG_ERR("LAN interface cant be equal to wan interface");
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

doca_error_t
register_nat_params(void)
{
	doca_error_t result;
	struct doca_argp_param *nat_mode, *rules_param, *lan_intf, *wan_intf;

	/* Create and register static mode param */
	result = doca_argp_param_create(&nat_mode);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	doca_argp_param_set_short_name(nat_mode, "m");
	doca_argp_param_set_long_name(nat_mode, "mode");
	doca_argp_param_set_arguments(nat_mode, "<mode>");
	doca_argp_param_set_description(nat_mode, "set NAT mode");
	doca_argp_param_set_callback(nat_mode, nat_mode_callback);
	doca_argp_param_set_type(nat_mode, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(nat_mode);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register rules file path param */
	result = doca_argp_param_create(&rules_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(rules_param, "r");
	doca_argp_param_set_long_name(rules_param, "nat-rules");
	doca_argp_param_set_arguments(rules_param, "<path>");
	doca_argp_param_set_description(rules_param, "Path to the JSON file with NAT rules");
	doca_argp_param_set_callback(rules_param, nat_rules_callback);
	doca_argp_param_set_type(rules_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(rules_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register lan interface param */
	result = doca_argp_param_create(&lan_intf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(lan_intf, "lan");
	doca_argp_param_set_long_name(lan_intf, "lan-intf");
	doca_argp_param_set_arguments(lan_intf, "<lan intf>");
	doca_argp_param_set_description(lan_intf, "name of LAN interface");
	doca_argp_param_set_callback(lan_intf, lan_intf_callback);
	doca_argp_param_set_type(lan_intf, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(lan_intf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register wan interface param */
	result = doca_argp_param_create(&wan_intf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(wan_intf, "wan");
	doca_argp_param_set_long_name(wan_intf, "wan-intf");
	doca_argp_param_set_arguments(wan_intf, "<wan intf>");
	doca_argp_param_set_description(wan_intf, "name of wan interface");
	doca_argp_param_set_callback(wan_intf, wan_intf_callback);
	doca_argp_param_set_type(wan_intf, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(wan_intf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_error_get_descr(result));
		return result;
	}

	/* Register application callback */
	result = doca_argp_register_validation_callback(nat_args_validation_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program validation callback: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

/*
 * parse and set local ip from json file to nat rule struct
 *
 * @cur_rule [in]: rule in json object format
 * @rule [out]: rule in app structure format.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_local_ip(struct json_object *cur_rule, struct nat_rule_match *rule)
{
	doca_error_t result;
	struct json_object *local_ip;

	if (!json_object_object_get_ex(cur_rule, "local ip", &local_ip)) {
		DOCA_LOG_ERR("Missing local IP");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(local_ip) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"local ip\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = parse_ipv4_str(json_object_get_string(local_ip), &rule->local_ip);
	if (result != DOCA_SUCCESS)
		return result;
	return DOCA_SUCCESS;
}
/*
 * parse and set global ip from json file to nat rule struct
 *
 * @cur_rule [in]: rule in json object format
 * @rule [out]: rule in app structure format.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_global_ip(struct json_object *cur_rule, struct nat_rule_match *rule)
{
	doca_error_t result;
	struct json_object *global_ip;

	if (!json_object_object_get_ex(cur_rule, "global ip", &global_ip)) {
		DOCA_LOG_ERR("Missing global IP");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(global_ip) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"global ip\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = parse_ipv4_str(json_object_get_string(global_ip), &rule->global_ip);
	if (result != DOCA_SUCCESS)
		return result;
	return DOCA_SUCCESS;
}

/*
 * parse and set local port from json file to nat rule struct
 *
 * @cur_rule [in]: rule in json object format
 * @rule [out]: rule in app structure format.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_local_port(struct json_object *cur_rule, struct nat_rule_match *rule)
{
	struct json_object *local_port;

	if (!json_object_object_get_ex(cur_rule, "local port", &local_port)) {
		DOCA_LOG_ERR("Missing local port");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(local_port) != json_type_int) {
		DOCA_LOG_ERR("Expecting an int value for \"local port\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rule->local_port = json_object_get_int(local_port);
	return DOCA_SUCCESS;
}

/*
 * parse and set global port from json file to nat rule struct
 *
 * @cur_rule [in]: rule in json object format
 * @rule [out]: rule in app structure format.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_global_port(struct json_object *cur_rule, struct nat_rule_match *rule)
{
	struct json_object *global_port;

	if (!json_object_object_get_ex(cur_rule, "global port", &global_port)) {
		DOCA_LOG_ERR("Missing global port");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(global_port) != json_type_int) {
		DOCA_LOG_ERR("Expecting an int value for \"global port\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rule->global_port = json_object_get_int(global_port);
	return DOCA_SUCCESS;
}

/*
 * Create doca flow ports
 *
 * @portid [in]: port id to create
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
nat_port_create(uint8_t portid, struct doca_flow_port **port)
{
	char port_id_str[MAX_PORT_STR_LEN];
	struct doca_flow_port_cfg port_cfg = {0};
	doca_error_t result;

	port_cfg.port_id = portid;
	port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
	snprintf(port_id_str, MAX_PORT_STR_LEN, "%d", port_cfg.port_id);
	port_cfg.devargs = port_id_str;
	result = doca_flow_port_start(&port_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize doca flow port: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * stop doca ports
 *
 * @nb_ports [in]: number of ports
 */
static void
nat_stop_ports(int nb_ports)
{
	int portid;

	for (portid = 0; portid < nb_ports; portid++) {
		if (ports[portid])
			doca_flow_port_stop(ports[portid]);
	}
}

void
nat_destroy(int nb_ports, struct nat_rule_match *nat_rules)
{
	nat_stop_ports(nb_ports);
	doca_flow_destroy();
	if (nat_rules != NULL)
		free(nat_rules);
}

/*
 * Create and update rules array for static mode
 *
 * @parsed_json [in]: rules in json object format
 * @nat_num_rules [out]: num of rules to configure
 * @nat_rules [out]: array of rules
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_static_mode_rules(struct json_object *parsed_json, int *nat_num_rules, struct nat_rule_match **nat_rules)
{
	int i;
	doca_error_t result;
	struct json_object *rules;
	struct json_object *cur_rule;
	struct nat_rule_match *rules_arr = NULL;

	if (!json_object_object_get_ex(parsed_json, "rules", &rules)) {
		DOCA_LOG_ERR("Missing \"rules\" parameter");
		return DOCA_ERROR_INVALID_VALUE;
	}

	*nat_num_rules = json_object_array_length(rules);

	DOCA_LOG_INFO("Number of rules in input file: %d", *nat_num_rules);

	rules_arr = (struct nat_rule_match *)calloc(*nat_num_rules, sizeof(struct nat_rule_match));
	if (rules_arr == NULL) {
		DOCA_LOG_ERR("calloc() function failed");
		return DOCA_ERROR_NO_MEMORY;
	}

	for (i = 0; i < *nat_num_rules; i++) {
		cur_rule = json_object_array_get_idx(rules, i);
		result = create_local_ip(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_global_ip(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
	}
	*nat_rules = rules_arr;
	return DOCA_SUCCESS;
}

/*
 * Create and update rules array for pat mode
 *
 * @parsed_json [in]: rules in json object format
 * @nat_num_rules [out]: num of rules to configure
 * @nat_rules [out]: array of rules
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_pat_mode_rules(struct json_object *parsed_json, int *nat_num_rules, struct nat_rule_match **nat_rules)
{
	int i;
	doca_error_t result;
	struct json_object *cur_rule;
	struct json_object *global_ip;
	struct json_object *rules;
	struct nat_rule_match *rules_arr = NULL;
	doca_be32_t parsed_global_ip;

	if (!json_object_object_get_ex(parsed_json, "global ip", &global_ip)) {
		DOCA_LOG_ERR("Missing global IP");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(global_ip) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"global ip\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = parse_ipv4_str(json_object_get_string(global_ip), &parsed_global_ip);
	if (result != DOCA_SUCCESS)
		return result;
	DOCA_LOG_DBG("PAT global IP = %d.%d.%d.%d", (parsed_global_ip & 0xff), (parsed_global_ip >> 8 & 0xff), (parsed_global_ip >> 16 & 0xff), (parsed_global_ip >> 24 & 0xff));

	if (!json_object_object_get_ex(parsed_json, "rules", &rules)) {
		DOCA_LOG_ERR("Missing \"rules\" parameter");
		return DOCA_ERROR_INVALID_VALUE;
	}

	*nat_num_rules = json_object_array_length(rules);

	DOCA_LOG_DBG("Number of rules in input file: %d", *nat_num_rules);

	rules_arr = (struct nat_rule_match *)calloc(*nat_num_rules, sizeof(struct nat_rule_match));
	if (rules_arr == NULL) {
		DOCA_LOG_ERR("calloc() function failed");
		return DOCA_ERROR_NO_MEMORY;
	}

	for (i = 0; i < *nat_num_rules; i++) {
		cur_rule = json_object_array_get_idx(rules, i);
		result = create_local_ip(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_local_port(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_global_port(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		rules_arr[i].global_ip = parsed_global_ip;
	}
	*nat_rules = rules_arr;
	return DOCA_SUCCESS;
}

/*
 * Check the input file size and allocate a buffer to read it
 *
 * @fp [in]: file pointer to the input rules file
 * @file_length [out]: total bytes in file
 * @json_data [out]: allocated buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allocate_json_buffer_dynamic(FILE *fp, size_t *file_length, char **json_data)
{
	ssize_t buf_len = 0;

	/* use fseek to put file counter to the end, and calculate file length */
	if (fseek(fp, 0L, SEEK_END) == 0) {
		buf_len = ftell(fp);
		if (buf_len < 0) {
			DOCA_LOG_ERR("ftell() function failed");
			return DOCA_ERROR_IO_FAILED;
		}

		/* dynamic allocation */
		*json_data = (char *)malloc(buf_len + 1);
		if (*json_data == NULL) {
			DOCA_LOG_ERR("malloc() function failed");
			return DOCA_ERROR_NO_MEMORY;
		}

		/* return file counter to the beginning */
		if (fseek(fp, 0L, SEEK_SET) != 0) {
			free(*json_data);
			*json_data = NULL;
			DOCA_LOG_ERR("fseek() function failed");
			return DOCA_ERROR_IO_FAILED;
		}
	}
	*file_length = buf_len;
	return DOCA_SUCCESS;
}

doca_error_t
parsing_nat_rules(char *file_path, enum nat_mode mode, int *nat_num_rules, struct nat_rule_match **nat_rules)
{
	FILE *json_fp;
	size_t file_length;
	char *json_data = NULL;
	struct json_object *parsed_json;
	doca_error_t result;

	json_fp = fopen(file_path, "r");
	if (json_fp == NULL) {
		DOCA_LOG_ERR("JSON file open failed");
		return DOCA_ERROR_IO_FAILED;
	}

	result = allocate_json_buffer_dynamic(json_fp, &file_length, &json_data);
	if (result != DOCA_SUCCESS) {
		fclose(json_fp);
		DOCA_LOG_ERR("Failed to allocate data buffer for the json file");
		return result;
	}

	if (fread(json_data, file_length, 1, json_fp) < file_length)
		DOCA_LOG_DBG("EOF reached");
	fclose(json_fp);
	parsed_json = json_tokener_parse(json_data);

	free(json_data);

	switch (mode) {
	case STATIC:
		result = create_static_mode_rules(parsed_json, nat_num_rules, nat_rules);
		break;
	case PAT:
		result = create_pat_mode_rules(parsed_json, nat_num_rules, nat_rules);
		break;
	default:
		DOCA_LOG_ERR("Invalid NAT mode");
		return DOCA_ERROR_INVALID_VALUE;
	}
	return result;
}

/*
 * Entry processing callback
 *
 * @entry [in]: DOCA Flow entry
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
static void
check_for_valid_entry(struct doca_flow_pipe_entry *entry, uint16_t pipe_queue,
		      enum doca_flow_entry_status status, enum doca_flow_entry_op op, void *user_ctx)
{
	(void)entry;
	(void)pipe_queue;

	struct entries_status *entry_status = (struct entries_status *)user_ctx;

	if (entry_status == NULL || op != DOCA_FLOW_ENTRY_OP_ADD)
		return;
	if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Entry processing failed. entry_op=%d", op);
		entry_status->is_failure = true; /* Set is_failure to true if processing failed */
	}
	entry_status->nb_processed++;
}

doca_error_t
nat_init(struct nat_cfg *app_cfg, struct application_dpdk_config *app_dpdk_config)
{
	(void)app_cfg;

	uint16_t nb_ports;
	struct doca_flow_cfg nat_flow_cfg = {0};
	doca_error_t result;
	int portid;

	/* Initialize doca framework */
	nat_flow_cfg.queues = app_dpdk_config->port_config.nb_queues;
	nat_flow_cfg.mode_args = "vnf,hws";
	nat_flow_cfg.cb = check_for_valid_entry;
	nat_flow_cfg.queue_depth = QUEUE_DEPTH;

	nb_ports = app_dpdk_config->port_config.nb_ports;

	result = doca_flow_init(&nat_flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca flow ports: %s", doca_error_get_descr(result));
		return result;
	}

	for (portid = 0; portid < nb_ports; portid++) {
		/* Create doca flow port */
		result = nat_port_create(portid, &ports[portid]);
		if (result != DOCA_SUCCESS) {
			nat_stop_ports(portid);
			doca_flow_destroy();
			return result;
		}
		/* Pair ports should be the same as DPDK hairpin binding order */
		if (!portid || !(portid % 2))
			continue;
		result = doca_flow_port_pair(ports[portid], ports[portid ^ 1]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Pair port %u %u fail", portid, portid ^ 1);
			nat_stop_ports(portid + 1);
			doca_flow_destroy();
			return result;
		}
	}

	DOCA_LOG_DBG("Application configuration and rules offload done");
	return DOCA_SUCCESS;
}

/*
 * build pipe for data come from LAN in NAT static mode
 *
 * @port_id [in]: port id to build the pipe for
 * @nat_rules [in]: rules defintion to configure
 * @nat_num_rules [in]: number of rules to configure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
build_static_local_pipe(uint16_t port_id, struct nat_rule_match *nat_rules, int nat_num_rules)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd miss_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_pipe *nat_pipe;
	struct doca_flow_pipe_entry *entry;
	struct entries_status status;
	uint32_t flags;
	uint16_t ruleid;
	int nb_entries = 0;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&miss_fwd, 0, sizeof(miss_fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&status, 0, sizeof(status));

	pipe_cfg.attr.name = "NAT_STATIC_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = ports[port_id];

	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;

	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.src_ip = 0xffffffff;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	miss_fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create nat pipe: %s", doca_error_get_descr(result));
		return result;
	}

	for (ruleid = 0; ruleid < nat_num_rules; ruleid++) {
		memset(&match, 0, sizeof(match));
		memset(&actions, 0, sizeof(actions));

		match.outer.ip4.src_ip = nat_rules[ruleid].local_ip;
		actions.outer.ip4.src_ip = nat_rules[ruleid].global_ip;

		/* Last entry in a batch should be with NO_WAIT flag */
		if (nb_entries == (QUEUE_DEPTH - 1))
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;

		result = doca_flow_pipe_add_entry(0, nat_pipe, &match, &actions, NULL, NULL, flags, &status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Entry creation FAILED: %s", doca_error_get_descr(result));
			continue;
		}
		nb_entries++;

		/* Process entries to make a space for the next entries */
		if (nb_entries == QUEUE_DEPTH) {
			result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to process entries");
				return result;
			}
			if (status.is_failure || status.nb_processed == 0) {
				DOCA_LOG_ERR("Failed to process entries");
				return DOCA_ERROR_BAD_STATE;
			}
			nb_entries -= status.nb_processed;
			status.nb_processed = 0;
		}
	}

	/* Processing pipes entries */
	while (nb_entries - status.nb_processed > 0) {
		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries - status.nb_processed);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries");
			return result;
		}
		if (status.is_failure) {
			DOCA_LOG_ERR("Failed to process entries");
			return DOCA_ERROR_BAD_STATE;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * build pipe for data come from WAN in NAT static mode
 *
 * @port_id [in]: port id to build the pipe for
 * @nat_rules [in]: rules defintion to configure
 * @nat_num_rules [in]: number of rules to configure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
build_static_global_pipe(uint16_t port_id, struct nat_rule_match *nat_rules, int nat_num_rules)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd miss_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_pipe *nat_pipe;
	struct doca_flow_pipe_entry *entry;
	struct entries_status status;
	uint32_t flags;
	uint16_t ruleid;
	int nb_entries = 0;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd, 0, sizeof(miss_fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&status, 0, sizeof(status));

	pipe_cfg.attr.name = "NAT_STATIC_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = ports[port_id];

	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;

	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.dst_ip = 0xffffffff;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	miss_fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create nat pipe: %s", doca_error_get_descr(result));
		return result;
	}

	for (ruleid = 0; ruleid < nat_num_rules; ruleid++) {
		memset(&match, 0, sizeof(match));
		memset(&actions, 0, sizeof(actions));

		match.outer.ip4.dst_ip = nat_rules[ruleid].global_ip;
		actions.outer.ip4.dst_ip = nat_rules[ruleid].local_ip;

		/* Last entry in a batch should be with NO_WAIT flag */
		if (nb_entries == (QUEUE_DEPTH - 1))
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;

		result = doca_flow_pipe_add_entry(0, nat_pipe, &match, &actions, NULL, NULL, flags, &status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Entry creation FAILED: %s", doca_error_get_descr(result));
			continue;
		}
		nb_entries++;

		/* Process entries to make a space for the next entries */
		if (nb_entries == QUEUE_DEPTH) {
			result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to process entries");
				return result;
			}
			if (status.is_failure || status.nb_processed == 0) {
				DOCA_LOG_ERR("Failed to process entries");
				return DOCA_ERROR_BAD_STATE;
			}
			nb_entries -= status.nb_processed;
			status.nb_processed = 0;
		}
	}

	/* Processing pipes entries */
	while (nb_entries - status.nb_processed > 0) {
		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries - status.nb_processed);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries");
			return result;
		}
		if (status.is_failure) {
			DOCA_LOG_ERR("Failed to process entries");
			return DOCA_ERROR_BAD_STATE;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Create control pipe as root pipe
 *
 * @port [in]: port to configure the pipe for
 * @pipe [out]: created control pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_control_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "CONTROL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;
	pipe_cfg.port = port;
	pipe_cfg.attr.is_root = true;

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Add the entries to the control pipe. One entry that matches TCP traffic, and one that matches UDP traffic.
 *
 *
 * @control_pipe [in]: control pipe ID
 * @udp_pipe [in]: UDP pipe to forward UDP traffic to
 * @tcp_pipe [in]: TCP pipe to forward TCP traffic to
 * @port [in]: port to configure the pipe for
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_control_pipe_entries(struct doca_flow_pipe *control_pipe,  struct doca_flow_pipe *udp_pipe,  struct doca_flow_pipe *tcp_pipe, struct doca_flow_port *port)
{
	(void)port;

	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	uint8_t priority = 0;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = udp_pipe;
	result = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						  NULL, NULL, NULL, NULL, NULL, &fwd, NULL, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = tcp_pipe;
	result = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						  NULL, NULL, NULL, NULL, NULL, &fwd, NULL, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * build pipe for data come from LAN in NAT PAT mode
 *
 * @port_id [in]: port id to build the pipe for
 * @nat_rules [in]: rules defintion to configure
 * @nat_num_rules [in]: number of rules to configure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
build_pat_local_pipe(uint16_t port_id, struct nat_rule_match *nat_rules, int nat_num_rules)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd miss_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_pipe *nat_tcp_pipe, *nat_udp_pipe, *control_pipe;
	struct entries_status status;
	doca_error_t result;
	uint16_t ruleid;
	uint32_t flags;
	int nb_entries = 0;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&miss_fwd, 0, sizeof(miss_fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&status, 0, sizeof(status));

	pipe_cfg.attr.name = "NAT_PAT_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.port = ports[port_id];

	/* first - set tcp pipe with outer.l4_type_ext */
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.tcp.l4_port.src_port = 0xffff;

	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.src_ip = 0xffffffff;
	actions.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	actions.outer.tcp.l4_port.src_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	miss_fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_tcp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create NAT TCP pipe: %s", doca_error_get_descr(result));
		return result;
	}

	/* add udp pipe */
	match.outer.tcp.l4_port.src_port = 0;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.outer.udp.l4_port.src_port = 0xffff;

	actions.outer.tcp.l4_port.src_port = 0;
	actions.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	actions.outer.udp.l4_port.src_port = 0xffff;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_udp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create NAT UDP pipe: %s", doca_error_get_descr(result));
		return result;
	}

	for (ruleid = 0; ruleid < nat_num_rules; ruleid++) {
		memset(&match, 0, sizeof(match));
		memset(&actions, 0, sizeof(actions));
		match.outer.ip4.src_ip = nat_rules[ruleid].local_ip;
		match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(nat_rules[ruleid].local_port);
		actions.outer.ip4.src_ip = nat_rules[ruleid].global_ip;
		actions.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(nat_rules[ruleid].global_port);

		/* Last entry in a batch should be with NO_WAIT flag */
		if (nb_entries == (QUEUE_DEPTH - 2))
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;

		doca_flow_pipe_add_entry(0, nat_tcp_pipe, &match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);

		match.outer.udp.l4_port.src_port = rte_cpu_to_be_16(nat_rules[ruleid].local_port);
		actions.outer.udp.l4_port.src_port = rte_cpu_to_be_16(nat_rules[ruleid].global_port);

		doca_flow_pipe_add_entry(0, nat_udp_pipe, &match, &actions, NULL, NULL, flags, &status, NULL);

		nb_entries += 2;

		/* Process entries to make a space for the next entries */
		if (nb_entries == QUEUE_DEPTH) {
			result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to process entries");
				return result;
			}
			if (status.is_failure || status.nb_processed == 0) {
				DOCA_LOG_ERR("Failed to process entries");
				return DOCA_ERROR_BAD_STATE;
			}
			nb_entries -= status.nb_processed;
			status.nb_processed = 0;
		}
	}

	/* Processing pipes entries */
	while (nb_entries - status.nb_processed > 0) {
		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries - status.nb_processed);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries");
			return result;
		}
		if (status.is_failure) {
			DOCA_LOG_ERR("Failed to process entries");
			return DOCA_ERROR_BAD_STATE;
		}
	}

	result = create_control_pipe(ports[port_id], &control_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create control pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = add_control_pipe_entries(control_pipe, nat_udp_pipe, nat_tcp_pipe, ports[port_id]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entries: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * build pipe for data come from WAN in NAT PAT mode
 *
 * @port_id [in]: port id to build the pipe for
 * @nat_rules [in]: rules defintion to configure
 * @nat_num_rules [in]: number of rules to configure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
build_pat_global_pipe(uint16_t port_id, struct nat_rule_match *nat_rules, int nat_num_rules)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd miss_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_pipe *nat_tcp_pipe, *nat_udp_pipe, *control_pipe;
	struct entries_status status;
	doca_error_t result;
	uint16_t ruleid;
	uint32_t flags;
	int nb_entries = 0;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd, 0, sizeof(miss_fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&status, 0, sizeof(status));

	pipe_cfg.attr.name = "NAT_PAT_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.port = ports[port_id];

	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	actions.outer.ip4.dst_ip = 0xffffffff;
	actions.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	actions.outer.tcp.l4_port.dst_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	miss_fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_tcp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create NAT TCP pipe: %s", doca_error_get_descr(result));
		return result;
	}

	/* now - set udp pipe */
	match.outer.tcp.l4_port.dst_port = 0;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	match.outer.udp.l4_port.dst_port = 0xffff;

	actions.outer.tcp.l4_port.dst_port = 0;
	actions.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;
	actions.outer.udp.l4_port.dst_port = 0xffff;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &miss_fwd, &nat_udp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create NAT UDP pipe: %s", doca_error_get_descr(result));
		return result;
	}

	for (ruleid = 0; ruleid < nat_num_rules; ruleid++) {
		memset(&match, 0, sizeof(match));
		memset(&actions, 0, sizeof(actions));

		match.outer.ip4.dst_ip = nat_rules[ruleid].global_ip;
		match.outer.tcp.l4_port.dst_port = rte_cpu_to_be_16(nat_rules[ruleid].global_port);
		actions.outer.ip4.dst_ip = nat_rules[ruleid].local_ip;
		actions.outer.tcp.l4_port.dst_port = rte_cpu_to_be_16(nat_rules[ruleid].local_port);

		/* Last entry in a batch should be with NO_WAIT flag */
		if (nb_entries == (QUEUE_DEPTH - 2))
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;

		doca_flow_pipe_add_entry(0, nat_tcp_pipe, &match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
		/* add the same entry also to UDP pipe */

		match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(nat_rules[ruleid].local_port);
		actions.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(nat_rules[ruleid].global_port);

		doca_flow_pipe_add_entry(0, nat_udp_pipe, &match, &actions, NULL, NULL, flags, &status, NULL);
		nb_entries += 2;

		/* Process entries to make a space for the next entries */
		if (nb_entries == QUEUE_DEPTH) {
			result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to process entries");
				return result;
			}
			if (status.is_failure || status.nb_processed == 0) {
				DOCA_LOG_ERR("Failed to process entries");
				return DOCA_ERROR_BAD_STATE;
			}
			nb_entries -= status.nb_processed;
			status.nb_processed = 0;
		}
	}

	/* Processing pipes entries */
	while (nb_entries - status.nb_processed > 0) {
		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, nb_entries - status.nb_processed);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries");
			return result;
		}
		if (status.is_failure) {
			DOCA_LOG_ERR("Failed to process entries");
			return DOCA_ERROR_BAD_STATE;
		}
	}

	result = create_control_pipe(ports[port_id], &control_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create control pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = add_control_pipe_entries(control_pipe, nat_udp_pipe, nat_tcp_pipe, ports[port_id]);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add control pipe entries: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

doca_error_t
nat_pipes_init(struct nat_rule_match *nat_rules, int nat_num_rules, struct nat_cfg *app_cfg, int nb_ports)
{

	uint16_t portid;
	struct rte_eth_dev_info dev_info = {0};
	int ret;
	char lan_port_intf_name[MAX_PORT_NAME] = {0};
	char wan_port_intf_name[MAX_PORT_NAME] = {0};
	doca_error_t result;

	for (portid = 0; portid < nb_ports; portid++) {
		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret < 0) {
			DOCA_LOG_ERR("Getting device (port %u) info: %s", portid, strerror(-ret));
			return DOCA_ERROR_DRIVER;
		}
		snprintf(lan_port_intf_name, MAX_PORT_NAME, "mlx5_core.sf.%d", app_cfg->lan_intf_id);
		snprintf(wan_port_intf_name, MAX_PORT_NAME, "mlx5_core.sf.%d", app_cfg->wan_intf_id);
		switch (app_cfg->mode) {
		case STATIC:
			if (dev_info.switch_info.name != NULL &&
				strstr(dev_info.switch_info.name, lan_port_intf_name) != 0) {
				result = build_static_local_pipe(portid, nat_rules, nat_num_rules);
				if (result != DOCA_SUCCESS)
					return result;
			} else if (dev_info.switch_info.name != NULL &&
				strstr(dev_info.switch_info.name, wan_port_intf_name) != 0) {
				result = build_static_global_pipe(portid, nat_rules, nat_num_rules);
				if (result != DOCA_SUCCESS)
					return result;
			} else {
				DOCA_LOG_ERR("Getting interface index (%d) which isn't match to any configured port: %s", portid, strerror(-ret));
				return DOCA_ERROR_INVALID_VALUE;
			}
			break;
		case DYNAMIC:
			break;
		case PAT:
			if (dev_info.switch_info.name != NULL &&
				strstr(dev_info.switch_info.name, lan_port_intf_name) != 0) {
				result = build_pat_local_pipe(portid, nat_rules, nat_num_rules);
				if (result != DOCA_SUCCESS)
					return result;
			} else if (dev_info.switch_info.name != NULL &&
				strstr(dev_info.switch_info.name, wan_port_intf_name) != 0) {
				result = build_pat_global_pipe(portid, nat_rules, nat_num_rules);
				if (result != DOCA_SUCCESS)
					return result;
			} else {
				DOCA_LOG_ERR("Getting interface index (%d) which isn't match to any configured port: %s", portid, strerror(-ret));
				return DOCA_ERROR_INVALID_VALUE;
			}
			break;
		default:
			break;
		}
	}
	return DOCA_SUCCESS;
}
