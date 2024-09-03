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

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <rte_byteorder.h>

#include <doca_flow.h>
#include <doca_log.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_GENEVE_OPT);

#define SAMPLE_CLASS_ID 0x107

#define CHANGEABLE_32 (UINT32_MAX)
#define FULL_MASK_32 (UINT32_MAX)

/*
 * Fill list of GENEVE options parser user configuration
 *
 * @list [out]: list of option configurations
 */
static void
fill_parser_geneve_opt_cfg_list(struct doca_flow_parser_geneve_opt_cfg *list)
{
	/*
	 * Prepare the configuration for first option.
	 *
	 * 0                   1                   2                   3
	 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |        class (0x107)          |    type (1)   |     | len (5) |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW0 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW1 (part of the parser)                     |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW2 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW3 (part of the parser)                     |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW4 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	list[0].match_on_class_mode = DOCA_FLOW_PARSER_GENEVE_OPT_MODE_FIXED;
	list[0].option_class = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	list[0].option_type = 1;
	list[0].option_len = 5; /* Data length - excluding the option header */
	list[0].data_mask[0] = 0x0;
	list[0].data_mask[1] = FULL_MASK_32;
	list[0].data_mask[2] = 0x0;
	list[0].data_mask[3] = FULL_MASK_32;
	list[0].data_mask[4] = 0x0;

	/*
	 * Prepare the configuration for second option.
	 *
	 * 0                   1                   2                   3
	 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |        class (0x107)          |    type (2)   |     | len (2) |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW0 (part of the parser)                     |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW1 (part of the parser)                     |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	list[1].match_on_class_mode = DOCA_FLOW_PARSER_GENEVE_OPT_MODE_FIXED;
	list[1].option_class = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	list[1].option_type = 2;
	list[1].option_len = 2; /* Data length - excluding the option header */
	list[1].data_mask[0] = FULL_MASK_32;
	list[1].data_mask[1] = FULL_MASK_32;

	/*
	 * Prepare the configuration for third option.
	 *
	 * 0                   1                   2                   3
	 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |        class (0x107)          |    type (3)   |     | len (4) |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW0 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW1 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW2 (not part of the parser)                 |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |                  DW3 (part of the parser)                     |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */
	list[2].match_on_class_mode = DOCA_FLOW_PARSER_GENEVE_OPT_MODE_FIXED;
	list[2].option_class = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	list[2].option_type = 3;
	list[2].option_len = 4; /* Data length - excluding the option header */
	list[2].data_mask[0] = 0x0;
	list[2].data_mask[1] = 0x0;
	list[2].data_mask[2] = 0x0;
	list[2].data_mask[3] = FULL_MASK_32;
}

/*
 * Create DOCA Flow pipe that match GENEVE traffic with changeable GENEVE VNI and option
 *
 * @port [in]: port of the pipe
 * @next_pipe [in]: next pipe to forward to
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_geneve_match_pipe(struct doca_flow_port *port, struct doca_flow_pipe *next_pipe,
			 struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "GENEVE_MATCH_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
	match_mask.parser_meta.outer_l4_type = FULL_MASK_32;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match_mask.parser_meta.outer_l3_type = FULL_MASK_32;
	match.tun.type = DOCA_FLOW_TUN_GENEVE;
	match.tun.geneve.vni = CHANGEABLE_32;
	match_mask.tun.geneve.vni = FULL_MASK_32;

	/* First option - index 0 describes the option header */
	match.tun.geneve_options[0].class_id = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	match.tun.geneve_options[0].type = 1;
	match.tun.geneve_options[0].length = 5;
	match_mask.tun.geneve_options[0].class_id = 0xffff;
	match_mask.tun.geneve_options[0].type = 0xff;
	/*
	 * Indexes 1-5 describe the option data, index 4 describes the 4th DW in data.
	 * Make data as changeable by cover all data (5 DWs).
	 */
	match.tun.geneve_options[1].data = CHANGEABLE_32;
	match.tun.geneve_options[2].data = CHANGEABLE_32;
	match.tun.geneve_options[3].data = CHANGEABLE_32;
	match.tun.geneve_options[4].data = CHANGEABLE_32;
	match.tun.geneve_options[5].data = CHANGEABLE_32;
	/* Mask the only DW we want to match */
	match_mask.tun.geneve_options[4].data = FULL_MASK_32;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = next_pipe;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Create DOCA Flow pipe that match GENEVE options data and either decap action
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_geneve_decap_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions1, actions2, *actions_arr[2];
	struct doca_flow_action_desc l2_decap_desc, l3_decap_desc;
	struct doca_flow_action_descs descs1 = {.desc_array = &l2_decap_desc, .nb_action_desc = 1},
				      descs2 = {.desc_array = &l3_decap_desc, .nb_action_desc = 1}, *descs_arr[2];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	uint8_t mac_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions1, 0, sizeof(actions1));
	memset(&actions2, 0, sizeof(actions2));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	actions_arr[0] = &actions1;
	actions_arr[1] = &actions2;
	descs_arr[0] = &descs1;
	descs_arr[1] = &descs2;

	pipe_cfg.attr.name = "GENEVE_DECAP_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.action_descs = descs_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = 2;
	pipe_cfg.port = port;

	match.tun.type = DOCA_FLOW_TUN_GENEVE;

	/*
	 * Second option - index 0 describes the option header.
	 * The order of options in match structure is regardless to options order in parser creation.
	 * This pipe will match if the options will be present in any kind of order.
	 */
	match.tun.geneve_options[0].class_id = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	match.tun.geneve_options[0].type = 2;
	match.tun.geneve_options[0].length = 2;
	match_mask.tun.geneve_options[0].class_id = 0xffff;
	match_mask.tun.geneve_options[0].type = 0xff;
	/*
	 * Indexes 1-2 describe the option data, index 1 describes the 1st DW in data and index 2
	 * describes the 2nd DW in data.
	 * Make data as changeable by cover all data (2 DWs).
	 */
	match.tun.geneve_options[1].data = CHANGEABLE_32;
	match.tun.geneve_options[2].data = CHANGEABLE_32;
	/* We want to match the all DWs in data */
	match_mask.tun.geneve_options[1].data = FULL_MASK_32;
	match_mask.tun.geneve_options[2].data = FULL_MASK_32;

	/* Third option - index 3 describes the option header */
	match.tun.geneve_options[3].class_id = rte_cpu_to_be_16(SAMPLE_CLASS_ID);
	match.tun.geneve_options[3].type = 3;
	match.tun.geneve_options[3].length = 4;
	match_mask.tun.geneve_options[3].class_id = 0xffff;
	match_mask.tun.geneve_options[3].type = 0xff;
	/*
	 * Indexes 4-7 describe the option data, index 7 describes the last DW in data (the 4th).
	 * Make data as changeable by cover all data (4 DWs).
	 */
	match.tun.geneve_options[4].data = CHANGEABLE_32;
	match.tun.geneve_options[5].data = CHANGEABLE_32;
	match.tun.geneve_options[6].data = CHANGEABLE_32;
	match.tun.geneve_options[7].data = CHANGEABLE_32;
	/* Mask the only DW we want to match */
	match_mask.tun.geneve_options[7].data = FULL_MASK_32;

	actions1.decap = true;
	l2_decap_desc.type = DOCA_FLOW_ACTION_DECAP_ENCAP;
	l2_decap_desc.decap_encap.is_l2 = true;

	actions2.decap = true;
	l3_decap_desc.type = DOCA_FLOW_ACTION_DECAP_ENCAP;
	l3_decap_desc.decap_encap.is_l2 = false;
	/* Append eth header after decap GENEVE L3 tunnel */
	SET_MAC_ADDR(actions2.outer.eth.src_mac,
		     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	SET_MAC_ADDR(actions2.outer.eth.dst_mac,
		     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	actions2.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries with example GENEVE VNI to match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_geneve_pipe_decap_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	uint8_t mac1[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	uint8_t mac2[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	match.tun.geneve_options[1].data = rte_cpu_to_be_32(0x00abcdef);
	match.tun.geneve_options[2].data = rte_cpu_to_be_32(0x00abcdef);
	match.tun.geneve_options[7].data = rte_cpu_to_be_32(0x00abcdef);
	actions.action_idx = 0;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.tun.geneve_options[1].data = rte_cpu_to_be_32(0x00123456);
	match.tun.geneve_options[2].data = rte_cpu_to_be_32(0x00123456);
	match.tun.geneve_options[7].data = rte_cpu_to_be_32(0x00123456);
	actions.decap = true;
	actions.action_idx = 1;
	SET_MAC_ADDR(actions.outer.eth.src_mac, mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
	SET_MAC_ADDR(actions.outer.eth.dst_mac, mac2[0], mac2[1], mac2[2], mac2[3], mac2[4], mac2[5]);
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.tun.geneve_options[1].data = rte_cpu_to_be_32(0x00778899);
	match.tun.geneve_options[2].data = rte_cpu_to_be_32(0x00778899);
	match.tun.geneve_options[7].data = rte_cpu_to_be_32(0x00778899);
	actions.decap = true;
	actions.action_idx = 1;
	SET_MAC_ADDR(actions.outer.eth.src_mac, mac2[0], mac2[1], mac2[2], mac2[3], mac2[4], mac2[5]);
	SET_MAC_ADDR(actions.outer.eth.dst_mac, mac1[0], mac1[1], mac1[2], mac1[3], mac1[4], mac1[5]);
	actions.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Add DOCA Flow pipe entries with example GENEVE VNI to match
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_geneve_pipe_match_entries(struct doca_flow_pipe *pipe, struct entries_status *status)
{
	struct doca_flow_match match;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;

	memset(&match, 0, sizeof(match));

	match.tun.geneve.vni = BUILD_VNI(0xabcdef);
	match.tun.geneve_options[4].data = rte_cpu_to_be_32(0x00abcdef);

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.tun.geneve.vni = BUILD_VNI(0x123456);
	match.tun.geneve_options[4].data = rte_cpu_to_be_32(0x00123456);

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	match.tun.geneve.vni = BUILD_VNI(0x778899);
	match.tun.geneve_options[4].data = rte_cpu_to_be_32(0x00778899);

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, 0, status, &entry);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Run flow_geneve_opt sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_geneve_opt(int nb_queues)
{
	int nb_ports = 2;
	uint8_t nb_options = 3;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_parser_geneve_opt_cfg tlv_list[nb_options];
	struct doca_flow_parser *parsers[nb_ports];
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *decap_pipes[nb_ports];
	struct doca_flow_pipe *pipes[nb_ports];
	struct entries_status status;
	int num_of_entries = 6;
	doca_error_t result;
	int port_id;

	result = init_doca_flow(nb_queues, "vnf,hws", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	memset(tlv_list, 0, sizeof(tlv_list[0]) * nb_options);
	fill_parser_geneve_opt_cfg_list(tlv_list);

	for (port_id = 0; port_id < nb_ports; port_id++) {
		memset(&status, 0, sizeof(status));

		result = doca_flow_parser_geneve_opt_create(ports[port_id], tlv_list, nb_options,
							    &parsers[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create geneve parser: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_geneve_decap_pipe(ports[port_id], port_id, &decap_pipes[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add geneve decap pipe: %s", doca_error_get_descr(result));
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_geneve_match_pipe(ports[port_id], decap_pipes[port_id], &pipes[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add geneve match pipe: %s", doca_error_get_descr(result));
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_geneve_pipe_decap_entries(decap_pipes[port_id], &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add geneve pipe decap entries: %s", doca_error_get_descr(result));
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_geneve_pipe_match_entries(pipes[port_id], &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add geneve pipe match entries: %s", doca_error_get_descr(result));
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, num_of_entries);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (status.nb_processed != num_of_entries || status.failure) {
			DOCA_LOG_ERR("Failed to process entries");
			doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return DOCA_ERROR_BAD_STATE;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(10);

	for (port_id = 0; port_id < nb_ports; port_id++) {
		/*
		 * It is important to destroy all pipes in port before parser destruction.
		 * If any pipe uses Geneve options, parser destruction will fail.
		 */
		doca_flow_pipe_destroy(pipes[port_id]);
		doca_flow_pipe_destroy(decap_pipes[port_id]);
		result = doca_flow_parser_geneve_opt_destroy(parsers[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy geneve parser: %s", doca_error_get_descr(result));
			return result;
		}
	}

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
