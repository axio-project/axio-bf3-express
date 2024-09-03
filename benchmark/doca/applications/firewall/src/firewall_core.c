/*
 * Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <json-c/json.h>
#include <rte_byteorder.h>
#include <rte_ethdev.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <utils.h>

#include "firewall_core.h"

DOCA_LOG_REGISTER(FIREWALL_CORE);

#define MAX_PORT_STR 128		/* maximum port string length */
#define NB_ACTIONS_ARR 1		/* default number of actions in pipe */
#define DEFAULT_TIMEOUT_US (10000)	/* default timeout for processing entries */
#define NB_PORTS 2			/* number of ports */

static struct port_pipe_ids fw_ports_pipes_ids[NB_PORTS];

/*
 * ARGP Callback - Handle running mode parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
firewall_mode_callback(void *param, void *config)
{
	struct firewall_cfg *firewall_cfg = (struct firewall_cfg *)config;
	const char *mode = (char *)param;

	if (strcmp(mode, "static") == 0)
		firewall_cfg->mode = FIREWALL_MODE_STATIC;
	else if (strcmp(mode, "interactive") == 0)
		firewall_cfg->mode = FIREWALL_MODE_INTERACTIVE;
	else {
		DOCA_LOG_ERR("Illegal running mode = [%s]", mode);
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle rules file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
firewall_rules_callback(void *param, void *config)
{
	struct firewall_cfg *firewall_cfg = (struct firewall_cfg *)config;
	const char *json_path = (char *)param;

	if (strnlen(json_path, MAX_FILE_NAME) == MAX_FILE_NAME) {
		DOCA_LOG_ERR("JSON file name is too long - MAX=%d", MAX_FILE_NAME - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (access(json_path, F_OK) == -1) {
		DOCA_LOG_ERR("JSON file was not found %s", json_path);
		return DOCA_ERROR_NOT_FOUND;
	}
	strlcpy(firewall_cfg->json_path, json_path, MAX_FILE_NAME);
	firewall_cfg->has_json = true;
	return DOCA_SUCCESS;
}

/*
 * ARGP validation Callback - check if there is an input file in static mode
 *
 * @config [in]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
firewall_args_validation_callback(void *config)
{
	struct firewall_cfg *firewall_cfg = (struct firewall_cfg *) config;

	if (firewall_cfg->mode == FIREWALL_MODE_STATIC && !firewall_cfg->has_json) {
		DOCA_LOG_ERR("Missing rules file path for static mode");
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

doca_error_t
register_firewall_params(void)
{
	doca_error_t result;
	struct doca_argp_param *mode_param,  *rules_param;

	/* Create and register firewall running mode param */
	result = doca_argp_param_create(&mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(mode_param, "m");
	doca_argp_param_set_long_name(mode_param, "mode");
	doca_argp_param_set_description(mode_param, "Set running mode {static, interactive}");
	doca_argp_param_set_callback(mode_param, firewall_mode_callback);
	doca_argp_param_set_type(mode_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(mode_param);
	result = doca_argp_register_param(mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register regex pci address param */
	result = doca_argp_param_create(&rules_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(rules_param, "r");
	doca_argp_param_set_long_name(rules_param, "firewall-rules");
	doca_argp_param_set_arguments(rules_param, "<path>");
	doca_argp_param_set_description(rules_param, "Path to the JSON file with 5-tuple rules when running with static mode");
	doca_argp_param_set_callback(rules_param, firewall_rules_callback);
	doca_argp_param_set_type(rules_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(rules_param);
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
	result = doca_argp_register_validation_callback(firewall_args_validation_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program validation callback: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Parse protocol type from json object rule
 *
 * @cur_rule [in]: json object of the current rule to parse
 * @rule [out]: struct of 5 tuple rule to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_protocol(struct json_object *cur_rule, struct rule_match *rule)
{
	doca_error_t result;
	struct json_object *protocol;
	const char *protocol_str;

	if (!json_object_object_get_ex(cur_rule, "protocol", &protocol)) {
		DOCA_LOG_ERR("Missing protocol type");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(protocol) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"protocol\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	protocol_str = json_object_get_string(protocol);
	result = parse_protocol_string(protocol_str, &rule->protocol);
	if (result != DOCA_SUCCESS)
		return result;
	return DOCA_SUCCESS;
}

/*
 * Parse source IP from json object rule
 *
 * @cur_rule [in]: json object of the current rule to parse
 * @rule [out]: struct of 5 tuple rule to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_src_ip(struct json_object *cur_rule, struct rule_match *rule)
{
	doca_error_t result;
	struct json_object *src_ip;

	if (!json_object_object_get_ex(cur_rule, "src-ip", &src_ip)) {
		DOCA_LOG_ERR("Missing src-ip");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(src_ip) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"src-ip\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = parse_ipv4_str(json_object_get_string(src_ip), &rule->src_ip);
	if (result != DOCA_SUCCESS)
		return result;
	return DOCA_SUCCESS;
}

/*
 * Parse destination IP from json object rule
 *
 * @cur_rule [in]: json object of the current rule to parse
 * @rule [out]: struct of 5 tuple rule to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dst_ip(struct json_object *cur_rule, struct rule_match *rule)
{
	doca_error_t result;
	struct json_object *dst_ip;

	if (!json_object_object_get_ex(cur_rule, "dst-ip", &dst_ip)) {
		DOCA_LOG_ERR("Missing dst-ip");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(dst_ip) != json_type_string) {
		DOCA_LOG_ERR("Expecting a string value for \"dst-ip\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	result = parse_ipv4_str(json_object_get_string(dst_ip), &rule->dst_ip);
	if (result != DOCA_SUCCESS)
		return result;
	return DOCA_SUCCESS;
}

/*
 * Parse source port from json object rule
 *
 * @cur_rule [in]: json object of the current rule to parse
 * @rule [out]: struct of 5 tuple rule to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_src_port(struct json_object *cur_rule, struct rule_match *rule)
{
	struct json_object *src_port;

	if (!json_object_object_get_ex(cur_rule, "src-port", &src_port)) {
		DOCA_LOG_ERR("Missing src-port");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(src_port) != json_type_int) {
		DOCA_LOG_ERR("Expecting a int value for \"src-port\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rule->src_port = json_object_get_int(src_port);
	return DOCA_SUCCESS;
}

/*
 * Parse destination port from json object rule
 *
 * @cur_rule [in]: json object of the current rule to parse
 * @rule [out]: struct of 5 tuple rule to update
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_dst_port(struct json_object *cur_rule, struct rule_match *rule)
{
	struct json_object *dst_port;

	if (!json_object_object_get_ex(cur_rule, "dst-port", &dst_port)) {
		DOCA_LOG_ERR("Missing dst-port");
		return DOCA_ERROR_INVALID_VALUE;
	}
	if (json_object_get_type(dst_port) != json_type_int) {
		DOCA_LOG_ERR("Expecting a int value for \"dst-port\"");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rule->dst_port = json_object_get_int(dst_port);
	return DOCA_SUCCESS;
}

/*
 * Parse json object of the rules and set it in rule_match array
 *
 * @rules [in]: json object of the rules to parse
 * @n_rules [out]: number of parsed rules
 * @drop_rules [out]: parsed rules in array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_drop_rules(struct json_object *rules, int *n_rules, struct rule_match **drop_rules)
{
	int i;
	doca_error_t result;
	struct json_object *cur_rule;
	struct rule_match *rules_arr = NULL;
	*n_rules = json_object_array_length(rules);

	DOCA_LOG_TRC("Num of rules in input file: %d", *n_rules);

	rules_arr = (struct rule_match *)calloc(*n_rules, sizeof(struct rule_match));
	if (rules_arr == NULL) {
		DOCA_LOG_ERR("calloc() function failed");
		return DOCA_ERROR_NO_MEMORY;
	}

	for (i = 0; i < *n_rules; i++) {
		cur_rule = json_object_array_get_idx(rules, i);
		result = create_protocol(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_src_ip(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_dst_ip(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_src_port(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
		result = create_dst_port(cur_rule, &rules_arr[i]);
		if (result != DOCA_SUCCESS) {
			free(rules_arr);
			return result;
		}
	}
	*drop_rules = rules_arr;
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
init_drop_rules(char *file_path, int *n_rules, struct rule_match **drop_rules)
{
	FILE *json_fp;
	size_t file_length;
	char *json_data = NULL;
	struct json_object *parsed_json;
	struct json_object *rules;
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

	if (fread(json_data, 1, file_length, json_fp) != file_length) {
		fclose(json_fp);
		free(json_data);
		DOCA_LOG_ERR("Error reading JSON file");
		return DOCA_ERROR_IO_FAILED;
	}
	fclose(json_fp);

	parsed_json = json_tokener_parse(json_data);
	if (!json_object_object_get_ex(parsed_json, "rules", &rules)) {
		DOCA_LOG_ERR("Missing \"rules\" parameter");
		free(json_data);
		return DOCA_ERROR_INVALID_VALUE;
	}

	free(json_data);
	return create_drop_rules(rules, n_rules, drop_rules);
}

/*
 * Build hairpin pipe that matches all traffic and add an entry to it
 *
 * @port_id [in]: port ID of the pipe
 * @pipe_id [out]: created pipe ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
build_hairpin_pipe(uint16_t port_id, uint64_t *pipe_id)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_grpc_fwd client_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_grpc_pipe_cfg client_cfg;
	uint64_t entry_id;
	doca_error_t result;
	enum doca_flow_entry_status status;
	int processed, num_of_entries = 1;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "HAIRPIN_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	client_cfg.cfg = &pipe_cfg;
	client_cfg.port_id = port_id;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;
	client_fwd.fwd = &fwd;

	result = doca_flow_grpc_pipe_create(&client_cfg, &client_fwd, NULL, pipe_id);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_grpc_pipe_add_entry(0, *pipe_id, &match, &actions,
						NULL, &client_fwd, DOCA_FLOW_NO_WAIT, &entry_id);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_grpc_entries_process(port_id, 0, DEFAULT_TIMEOUT_US, num_of_entries, &processed);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry process function failed with error");
		return result;
	}

	result = doca_flow_grpc_pipe_entry_get_status(entry_id, &status);
	if (result != DOCA_SUCCESS || status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entry");
		return DOCA_ERROR_BAD_STATE;
	}
	return DOCA_SUCCESS;
}

/*
 * Build pipe with 5 tuple match ("TRANSPORT" matches UDP and TCP) and drop action.
 * Packets that will not match the rules will get forwarded to hairpin pipe.
 *
 * @port_id [in]: port ID of the pipe
 * @next_pipe_id [in]: ID of the hairpin pipe to forward the missed packets
 * @pipe_id [out]: created pipe ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
build_drop_pipe(uint16_t port_id, uint64_t next_pipe_id, uint64_t *pipe_id)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_grpc_fwd client_fwd;
	struct doca_flow_fwd miss_fwd;
	struct doca_flow_grpc_fwd client_miss_fwd;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_grpc_pipe_cfg client_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "DROP_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.attr.is_root = true;
	client_cfg.cfg = &pipe_cfg;
	client_cfg.port_id = port_id;

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TRANSPORT;
	match.outer.transport.src_port = 0xffff;
	match.outer.transport.dst_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_DROP;
	client_fwd.fwd = &fwd;
	miss_fwd.type = DOCA_FLOW_FWD_PIPE;
	client_miss_fwd.fwd = &miss_fwd;
	client_miss_fwd.next_pipe_id = next_pipe_id;

	result = doca_flow_grpc_pipe_create(&client_cfg, &client_fwd, &client_miss_fwd, pipe_id);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Add the entries to the drop pipes according to the json file rules.
 *
 * @port_id [in]: port ID for which process entries should be called
 * @transport_pipe_id [in]: TRANSPORT pipe ID to add drop rules
 * @drop_rules [in]: rules array to add to the pipes
 * @n_rules [in]: number of rules in the array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_drop_entries(uint16_t port_id, uint64_t transport_pipe_id, struct rule_match *drop_rules, int n_rules)
{
	struct doca_flow_match match;
	struct doca_flow_grpc_fwd client_fwd;
	struct doca_flow_fwd fwd;
	struct doca_flow_actions actions;
	doca_error_t result;
	uint64_t entry_id;
	int i;
	enum doca_flow_entry_status status;
	int processed, num_of_entries = 1;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));

	fwd.type = DOCA_FLOW_FWD_DROP;
	client_fwd.fwd = &fwd;

	for (i = 0; i < n_rules; i++) {
		match.outer.l4_type_ext = drop_rules[i].protocol;
		match.outer.ip4.dst_ip = drop_rules[i].dst_ip;
		match.outer.ip4.src_ip = drop_rules[i].src_ip;
		match.outer.transport.src_port = rte_cpu_to_be_16(drop_rules[i].dst_port);
		match.outer.transport.dst_port = rte_cpu_to_be_16(drop_rules[i].src_port);

		/* add entry to drop pipe*/
		result = doca_flow_grpc_pipe_add_entry(0, transport_pipe_id, &match, &actions, NULL,
							&client_fwd, DOCA_FLOW_NO_WAIT, &entry_id);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
			return result;
		}

		result = doca_flow_grpc_entries_process(port_id, 0, DEFAULT_TIMEOUT_US, num_of_entries, &processed);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Entry process function failed with error");
			return result;
		}

		result = doca_flow_grpc_pipe_entry_get_status(entry_id, &status);
		if (result != DOCA_SUCCESS || status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entry");
			return DOCA_ERROR_BAD_STATE;
		}
	}
	return DOCA_SUCCESS;
}

/*
 * Invoke doca_flow_grpc_pipe_add_entry function
 *
 * @port_id [in]: port ID of the entry; should use it upon calling process entries
 * @match [in]: pointer to match, indicates a specific packet match information
 */
static void
pipe_add_entry(uint16_t port_id, struct doca_flow_match *match)
{
	uint64_t entry_id;
	doca_error_t result;
	enum doca_flow_entry_status status;
	int processed, num_of_entries = 1;
	uint16_t pipe_queue = 0;

	result = doca_flow_grpc_pipe_add_entry(pipe_queue, fw_ports_pipes_ids[port_id].transport_pipe_id, match, NULL, NULL, NULL, 0, &entry_id);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
	else {
		DOCA_LOG_INFO("Entry created successfully, entry id: %" PRIu64, entry_id);
		/* Add an additional new line for output readability */
		DOCA_LOG_INFO("");
	}

	result = doca_flow_grpc_entries_process(0, port_id, DEFAULT_TIMEOUT_US, num_of_entries, &processed);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Entry process function failed with error");
		return;
	}

	result = doca_flow_grpc_pipe_entry_get_status(entry_id, &status);
	if (result != DOCA_SUCCESS || status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
		DOCA_LOG_ERR("Failed to process entry");
}

/*
 * Invoke doca_flow_grpc_pipe_rm_entry function
 *
 * @entry_id [in]: entry ID of the entry to remove
 */
static void
pipe_rm_entry(uint64_t entry_id)
{
	doca_error_t result;
	uint16_t pipe_queue = 0;

	result = doca_flow_grpc_pipe_rm_entry(pipe_queue, entry_id, DOCA_FLOW_NO_WAIT);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to remove entry: %s", doca_error_get_descr(result));
}

/*
 * Invoke doca_flow_grpc_port_pipes_flush function
 *
 * @port_id [in]: port ID
 */
static void
port_pipes_flush(uint16_t port_id)
{
	doca_error_t result;

	result = doca_flow_grpc_port_pipes_flush(port_id);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to flush pipes: %s", doca_error_get_descr(result));
}

/*
 * Invoke doca_flow_grpc_port_pipes_dump function
 *
 * @port_id [in]: port ID
 * @fd [out]: the output file of the pipe information
 */
static void
port_pipes_dump(uint16_t port_id, FILE *fd)
{
	doca_error_t result;

	result = doca_flow_grpc_port_pipes_dump(port_id, fd);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to dump pipes: %s", doca_error_get_descr(result));
}

doca_error_t
firewall_add_drop_rules(struct rule_match *drop_rules, int n_rules)
{
	uint16_t port_id;
	doca_error_t result;

	for (port_id = 0; port_id < NB_PORTS; port_id++) {
		/* Add entries based on the json file data */
		result = add_drop_entries(port_id, fw_ports_pipes_ids[port_id].transport_pipe_id, drop_rules, n_rules);
		if (result != DOCA_SUCCESS) {
			free(drop_rules);
			return result;
		}
	}

	free(drop_rules);
	return DOCA_SUCCESS;
}

doca_error_t
firewall_pipes_init(void)
{
	int nb_ports = NB_PORTS;
	uint16_t port_id;
	uint64_t hairpin_pipe_id;
	doca_error_t result;

	for (port_id = 0; port_id < nb_ports; port_id++) {
		/* create doca flow hairpin pipe */
		result = build_hairpin_pipe(port_id, &hairpin_pipe_id);
		if (result != DOCA_SUCCESS)
			return result;

		/* create doca flow drop pipe with 5-tuple match*/
		result = build_drop_pipe(port_id, hairpin_pipe_id, &fw_ports_pipes_ids[port_id].transport_pipe_id);
		if (result != DOCA_SUCCESS)
			return result;

	}

	return DOCA_SUCCESS;
}

void
firewall_ports_stop(int nb_ports)
{
	int portid;

	for (portid = 0; portid < nb_ports; portid++)
		doca_flow_grpc_port_stop(portid);
}

doca_error_t
firewall_ports_init(const char *grpc_address)
{
	int nb_ports = NB_PORTS;
	int nb_queues = 1;
	int nb_counters = 8192;
	int nb_meters = 8192;
	uint16_t port_id;
	uint16_t res_port_id;
	struct doca_flow_cfg cfg = {0};
	doca_error_t result;

	cfg.queues = nb_queues;
	cfg.mode_args = "vnf,hws";
	cfg.resource.nb_counters = nb_counters;
	cfg.resource.nb_meters = nb_meters;
	doca_flow_grpc_client_create(grpc_address);

	result = doca_flow_grpc_init(&cfg, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		/* create doca flow port */
		struct doca_flow_port_cfg port_cfg;
		char port_id_str[MAX_PORT_STR];

		port_cfg.port_id = port_id;
		port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
		snprintf(port_id_str, MAX_PORT_STR, "%d", port_id);
		port_cfg.devargs = port_id_str;
		port_cfg.priv_data_size = 0;
		result = doca_flow_grpc_port_start(&port_cfg, &res_port_id);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to build DOCA Flow port: %s", doca_error_get_descr(result));
			firewall_ports_stop(port_id);
			doca_flow_grpc_destroy();
			return result;
		}
		/* Pair ports should be done in the following order: port0 with port1, port2 with port3 etc */
		if (!port_id || !(port_id % 2))
			continue;
		/* pair odd port with previous port */
		result = doca_flow_grpc_port_pair(port_id, port_id ^ 1);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to pair doca ports: %s", doca_error_get_descr(result));
			firewall_ports_stop(port_id + 1);
			doca_flow_grpc_destroy();
			return result;
		}
	}
	return DOCA_SUCCESS;
}

void
register_actions_on_flow_parser(void)
{
	set_pipe_fw_add_entry(pipe_add_entry);
	set_pipe_fw_rm_entry(pipe_rm_entry);
	set_port_pipes_flush(port_pipes_flush);
	set_port_pipes_dump(port_pipes_dump);
}
