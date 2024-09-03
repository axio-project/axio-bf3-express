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
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include <doca_flow.h>
#include <doca_log.h>

#include "flow_decrypt.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW::flow_decrypt);

#define DECAP_MAC_TYPE_IDX 12 /* index in decap raw data for inner l3 type */

/*
 * Create ipsec decrypt pipe with ESP header match and changeable shared IPSEC decryption object
 *
 * @port [in]: port of the pipe
 * @l3_type [in]: DOCA_FLOW_L3_TYPE_IP4 / DOCA_FLOW_L3_TYPE_IP6
 * @sn_en [in]: whether to set sn_en by hw or not
 * @syndrome_pipe [in]: next pipe
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_decrypt_pipe(struct doca_flow_port *port, enum doca_flow_l3_type l3_type,
			  bool sn_en, struct doca_flow_pipe *syndrome_pipe, struct doca_flow_pipe **pipe)
{
	int nb_actions = 1;
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[nb_actions];
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "DECRYPT_PIPE";
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS;
	pipe_cfg.attr.dir_info = DOCA_FLOW_DIRECTION_NETWORK_TO_HOST;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = nb_actions;
	pipe_cfg.port = port;

	match.outer.l3_type = l3_type;
	if (l3_type == DOCA_FLOW_L3_TYPE_IP4)
		match.outer.ip4.dst_ip = 0xffffffff;
	else
		SET_IP6_ADDR(match.outer.ip6.dst_ip, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);

	match.tun.type = DOCA_FLOW_TUN_ESP;
	match.tun.esp_spi = 0xffffffff;

	actions.crypto.action_type = DOCA_FLOW_CRYPTO_ACTION_DECRYPT;
	actions.crypto.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	actions.crypto.esp.sn_en = sn_en;
	actions.crypto.crypto_id = DECRYPT_DUMMY_ID;
	actions.meta.pkt_meta = 0xffffffff;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = syndrome_pipe;

	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, &fwd_miss, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create decrypt pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create pipe that match on spi, IP, and bad syndromes, each entry will have a counter
 *
 * @port [in]: port of the pipe
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_bad_syndrome_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_fwd fwd;
	struct doca_flow_monitor monitor;
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&fwd, 0, sizeof(fwd));
	memset(&monitor, 0, sizeof(monitor));
	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "bad_syndrome";
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS;
	pipe_cfg.attr.dir_info = DOCA_FLOW_DIRECTION_NETWORK_TO_HOST;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.port = port;

	/* ipsec syndrome */
	match_mask.parser_meta.ipsec_syndrome = 0xff;
	match.parser_meta.ipsec_syndrome = 0xff;
	/* Anti replay syndrome */
	match_mask.meta.u32[0] = 0xff;
	match.meta.u32[0] = 0xffffffff;
	/* rule index */
	match_mask.meta.pkt_meta = 0xffffffff;
	match.meta.pkt_meta = 0xffffffff;

	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create decrypt pipe: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Add 6 entries for the rule, each entry match the same IP and spi, and different syndrome
 *
 * @pipe [in]: pipe to add the entries
 * @rule [in]: rule of the entries - spi and ip address
 * @rule_id [in]: rule idx to match on
 * @decrypt_status [in]: user ctx
 * @flags [in]: wait for batch / no wait flag for the last entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_bad_syndrome_pipe_entry(struct doca_flow_pipe *pipe, struct decrypt_rule *rule, uint32_t rule_id,
			    struct entries_status *decrypt_status, enum doca_flow_flags_type flags)
{
	struct doca_flow_match match;
	doca_error_t result;

	memset(&match, 0, sizeof(match));

	match.meta.pkt_meta = (1U << 31) | rule_id;
	match.parser_meta.ipsec_syndrome = 1;
	match.meta.u32[0] = 0;

	/* match on ipsec syndrome 1 */
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, decrypt_status, &rule->entries[0].entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add bad syndrome pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.ipsec_syndrome = 2;

	/* match on ipsec syndrome 2 */
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, decrypt_status, &rule->entries[1].entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add bad syndrome pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.ipsec_syndrome = 0;
	match.meta.u32[0] = 1;

	/* match on ASO syndrome 1 */
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, decrypt_status, &rule->entries[2].entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add bad syndrome pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.meta.u32[0] = 2;

	/* match on ASO syndrome 2 */
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, flags, decrypt_status, &rule->entries[3].entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add bad syndrome pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create pipe for decryption syndrome and add entry to it.
 * If syndrome is 0 forwarding the packets, else drop them.
 *
 * @port [in]: port of the pipe
 * @app_cfg [in]: application configuration struct
 * @fwd [in]: pointer to forward struct
 * @miss_pipe [in]: pipe to forward the packets with bad syndrome
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_syndrome_pipe(struct doca_flow_port *port, struct ipsec_security_gw_config *app_cfg, struct doca_flow_fwd *fwd, struct doca_flow_pipe *miss_pipe, struct doca_flow_pipe **pipe)
{
	int nb_actions = 1;
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions, *actions_arr[nb_actions];
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_fwd fwd_miss;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions, 0, sizeof(actions));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "DECRYPT_SYNDROME_PIPE";
	pipe_cfg.attr.dir_info = DOCA_FLOW_DIRECTION_NETWORK_TO_HOST;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = nb_actions;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.port = port;

	/* ipsec syndrome */
	match_mask.parser_meta.ipsec_syndrome = 0xff;
	match.parser_meta.ipsec_syndrome = 0xff;

	/* anti-replay syndrome */
	match_mask.meta.u32[0] = 0xff;
	match.meta.u32[0] = 0xffffffff;

	match_mask.meta.pkt_meta = 0xffffffff;
	match.meta.pkt_meta = 0xffffffff;

	if (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH || app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP)
		actions.has_crypto_encap = true;

	actions.crypto_encap.action_type = DOCA_FLOW_CRYPTO_REFORMAT_DECAP;
	actions.crypto_encap.icv_size = 0xffff;
	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL) {
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_ESP_TUNNEL;
		uint8_t reformat_decap_data[14] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* mac_dst */
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* mac_src */
			0xff, 0xff			    /* mac_type */
		};

		memcpy(actions.crypto_encap.encap_data, reformat_decap_data, sizeof(reformat_decap_data));
		actions.crypto_encap.data_size = sizeof(reformat_decap_data);
	} else if (app_cfg->mode == IPSEC_SECURITY_GW_TRANSPORT)
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_ESP_OVER_IP;
	else
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_UDP_ESP_OVER_IP;

	fwd_miss.type = DOCA_FLOW_FWD_PIPE;
	fwd_miss.next_pipe = miss_pipe;

	result = doca_flow_pipe_create(&pipe_cfg, fwd, &fwd_miss, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create syndrome pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create control pipe for secured port
 *
 * @port [in]: port of the pipe
 * @is_root [in]: true in vnf mode
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_control_pipe(struct doca_flow_port *port, bool is_root, struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "CONTROL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS;
	pipe_cfg.attr.is_root = is_root;
	pipe_cfg.port = port;

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create control pipe: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Add control pipe entries - one entry that forwards IPv4 traffic to decrypt IPv4 pipe,
 * and one entry that forwards IPv6 traffic to decrypt IPv6 pipe
 *
 * @control_pipe [in]: control pipe pointer
 * @pipes [in]: all the pipes to forward the packets to
 * @mode [in]: application running mode
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_control_pipe_entries(struct doca_flow_pipe *control_pipe, struct decrypt_pipes pipes, enum ipsec_security_gw_mode mode)
{
	struct doca_flow_pipe_entry *entry;
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	if (mode != IPSEC_SECURITY_GW_UDP_TRANSPORT) {
		match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
		match.outer.ip4.next_proto = IPPROTO_ESP;
	} else {
		match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
	}

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.decrypt_ipv4_pipe;

	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add IPv4 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	memset(&match, 0, sizeof(match));

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
	if (mode != IPSEC_SECURITY_GW_UDP_TRANSPORT) {
		match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
		match.outer.ip6.next_proto = IPPROTO_ESP;
	} else {
		match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;
	}

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.decrypt_ipv6_pipe;

	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add IPv6 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Update the crypto config for decrypt trunnel mode
 *
 * @crypto_cfg [in]: shared object config
 * @inner_l3_type [in]: DOCA_FLOW_L3_TYPE_IP4 / DOCA_FLOW_L3_TYPE_IP6
 */
static void
create_ipsec_decrypt_shared_object_tunnel(struct doca_flow_crypto_encap_action *crypto_cfg,
					  enum doca_flow_l3_type inner_l3_type)
{
	uint8_t reformat_decap_data[14] = {
		0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, /* mac_dst */
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, /* mac_src */
		0x00, 0x00			    /* mac_type */
	};

	if (inner_l3_type == DOCA_FLOW_L3_TYPE_IP4) {
		reformat_decap_data[DECAP_MAC_TYPE_IDX] = 0x08;
		reformat_decap_data[DECAP_MAC_TYPE_IDX + 1] = 0x00;
	} else {
		reformat_decap_data[DECAP_MAC_TYPE_IDX] = 0x86;
		reformat_decap_data[DECAP_MAC_TYPE_IDX + 1] = 0xdd;
	}

	memcpy(crypto_cfg->encap_data, reformat_decap_data, sizeof(reformat_decap_data));
	crypto_cfg->data_size = sizeof(reformat_decap_data);
}

/*
 * Config and bind shared IPSEC object for decryption
 *
 * @port [in]: port to bind the shared object to
 * @sa [in]: crypto object handle (IPsec offload object)
 * @ipsec_id [in]: shared object ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_decrypt_shared_object(struct doca_flow_port *port, void *sa, uint32_t ipsec_id)
{
	struct doca_flow_shared_resource_cfg cfg;
	struct doca_flow_resource_crypto_cfg crypto_cfg;
	doca_error_t result;

	memset(&crypto_cfg, 0, sizeof(crypto_cfg));
	memset(&cfg, 0, sizeof(cfg));

	cfg.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS;
	crypto_cfg.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	crypto_cfg.security_ctx = sa;
	cfg.crypto_cfg = crypto_cfg;

	/* config ipsec object */
	result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_CRYPTO, ipsec_id, &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to cfg shared ipsec object: %s", doca_error_get_descr(result));
		return result;
	}
	/* bind shared ipsec decrypt object to port */
	result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_CRYPTO, &ipsec_id, 1, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to bind shared ipsec object to port: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Create dummy SA, and config and bind doca flow shared resource with it
 *
 * @cfg [in]: application configuration struct
 * @port [in]: port to bind the shared object to
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_decrypt_dummy_shared_object(struct ipsec_security_gw_config *cfg, struct doca_flow_port *port)
{
	doca_error_t result;
	struct ipsec_security_gw_sa_attrs dummy_sa_attr = {
		.icv_length = DOCA_IPSEC_ICV_LENGTH_16,
		.key_type = DOCA_ENCRYPTION_KEY_AESGCM_256,
		.enc_key_data[0] = 0x01,
		.salt = 0x12345678,
		.direction = DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT,
	};

	result = ipsec_security_gw_create_ipsec_sa(&dummy_sa_attr, cfg,	&cfg->app_rules.dummy_decrypt_sa);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_decrypt_shared_object(port, cfg->app_rules.dummy_decrypt_sa, DECRYPT_DUMMY_ID);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

doca_error_t
add_decrypt_entry(struct decrypt_rule *rule, int rule_id, struct doca_flow_port *port,
		  struct ipsec_security_gw_config *app_cfg)

{
	struct doca_flow_match match;
	struct doca_flow_match syndrome_match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	struct entries_status decrypt_status;
	struct doca_flow_pipe *decrypt_pipe;
	struct doca_flow_pipe *syndrome_pipe;
	struct doca_flow_pipe *bad_syndrome_pipe;
	doca_error_t result;

	memset(&decrypt_status, 0, sizeof(decrypt_status));
	memset(&match, 0, sizeof(match));
	memset(&syndrome_match, 0, sizeof(syndrome_match));
	memset(&actions, 0, sizeof(actions));

	/* create ipsec shared objects */
	result = create_ipsec_decrypt_shared_object(port, (void *)rule->sa, rule_id);
	if (result != DOCA_SUCCESS)
		return result;

	/* build rule match with specific destination IP and ESP SPI */
	match.outer.l3_type = rule->l3_type;
	match.tun.esp_spi = RTE_BE32(rule->esp_spi);
	match.tun.type = DOCA_FLOW_TUN_ESP;

	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4) {
		decrypt_pipe = app_cfg->decrypt_pipes.decrypt_ipv4_pipe;
		syndrome_pipe = app_cfg->decrypt_pipes.syndrome_ipv4_pipe;
		bad_syndrome_pipe = app_cfg->decrypt_pipes.bad_syndrome_ipv4_pipe;
		match.outer.ip4.dst_ip = rule->dst_ip4;
	} else {
		decrypt_pipe = app_cfg->decrypt_pipes.decrypt_ipv6_pipe;
		syndrome_pipe = app_cfg->decrypt_pipes.syndrome_ipv6_pipe;
		bad_syndrome_pipe = app_cfg->decrypt_pipes.bad_syndrome_ipv6_pipe;
		memcpy(match.outer.ip6.dst_ip, rule->dst_ip6, sizeof(rule->dst_ip6));
	}

	actions.action_idx = 0;
	actions.crypto.crypto_id = rule_id;
	actions.meta.pkt_meta = (1U << 31) | rule_id; /* save rule index in metadata */

	result = doca_flow_pipe_add_entry(0, decrypt_pipe, &match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &decrypt_status,
					  &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	decrypt_status.entries_in_queue++;

	/* Add entry with decap info to syndrome pipe */
	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL)
		create_ipsec_decrypt_shared_object_tunnel(&actions.crypto_encap, rule->inner_l3_type);
	actions.crypto_encap.icv_size = rule->sa_attrs.icv_length;

	syndrome_match.meta.pkt_meta = (1U << 31) | rule_id;
	syndrome_match.parser_meta.ipsec_syndrome = 0;
	syndrome_match.meta.u32[0] = 0;

	result = doca_flow_pipe_add_entry(0, syndrome_pipe, &syndrome_match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &decrypt_status, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	decrypt_status.entries_in_queue++;

	result = add_bad_syndrome_pipe_entry(bad_syndrome_pipe, rule, rule_id, &decrypt_status, DOCA_FLOW_NO_WAIT);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	decrypt_status.entries_in_queue += NUM_OF_SYNDROMES;

	/* process the entries in the decryption pipe*/
	do {
		result = process_entries(port, &decrypt_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (decrypt_status.entries_in_queue > 0);
	return DOCA_SUCCESS;
}

/*
 * Add decryption entries to the decrypt pipe
 *
 * @rules [in]: array of rules to insert for decryption
 * @nb_rules [in]: number of rules in array
 * @pipes [in]: decryption pipes to add the entries to
 * @nb_encrypt_rules [in]: number of initalized shared ipsec objects
 * @port [in]: port of the entries
 * @app_cfg [in]: application configuration struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_decrypt_entries(struct decrypt_rule *rules, int nb_rules, struct decrypt_pipes pipes, int nb_encrypt_rules,
		    struct doca_flow_port *port, struct ipsec_security_gw_config *app_cfg)
{
	struct doca_flow_match decrypt_match;
	struct doca_flow_match syndrome_match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	struct entries_status decrypt_status;
	struct doca_flow_pipe *decrypt_pipe;
	struct doca_flow_pipe *syndrome_pipe;
	struct doca_flow_pipe *bad_syndrome_pipe;
	enum doca_flow_flags_type flags;
	int i;
	doca_error_t result;

	memset(&decrypt_status, 0, sizeof(decrypt_status));
	memset(&syndrome_match, 0, sizeof(syndrome_match));
	memset(&decrypt_match, 0, sizeof(decrypt_match));
	memset(&actions, 0, sizeof(actions));

	decrypt_match.tun.type = DOCA_FLOW_TUN_ESP;

	syndrome_match.parser_meta.ipsec_syndrome = 0;
	syndrome_match.meta.u32[0] = 0;
	for (i = 0; i < nb_rules; i++) {
		if (i == nb_rules - 1 || decrypt_status.entries_in_queue == QUEUE_DEPTH - 8)
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;

		result = ipsec_security_gw_create_ipsec_sa(&rules[i].sa_attrs, app_cfg, &rules[i].sa);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create SA: %s", doca_error_get_descr(result));
			return result;
		}

		/* create ipsec shared objects */
		result = create_ipsec_decrypt_shared_object(port, rules[i].sa, nb_encrypt_rules + i);
		if (result != DOCA_SUCCESS)
			return result;

		/* build rule match with specific destination IP and ESP SPI */
		decrypt_match.outer.l3_type = rules[i].l3_type;
		decrypt_match.tun.esp_spi = RTE_BE32(rules[i].esp_spi);
		actions.action_idx = 0;
		actions.crypto.crypto_id = nb_encrypt_rules + i;

		if (rules[i].l3_type == DOCA_FLOW_L3_TYPE_IP4) {
			decrypt_pipe = pipes.decrypt_ipv4_pipe;
			syndrome_pipe = pipes.syndrome_ipv4_pipe;
			bad_syndrome_pipe = pipes.bad_syndrome_ipv4_pipe;
			decrypt_match.outer.ip4.dst_ip = rules[i].dst_ip4;
		} else {
			decrypt_pipe = pipes.decrypt_ipv6_pipe;
			syndrome_pipe = pipes.syndrome_ipv6_pipe;
			bad_syndrome_pipe = pipes.bad_syndrome_ipv6_pipe;
			memcpy(decrypt_match.outer.ip6.dst_ip, rules[i].dst_ip6, sizeof(rules[i].dst_ip6));
		}

		actions.meta.pkt_meta = (1U << 31) | i; /* save rule index in metadata */

		result = doca_flow_pipe_add_entry(0, decrypt_pipe, &decrypt_match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &decrypt_status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
		decrypt_status.entries_in_queue++;

		/* Add entry with decap info to syndrome pipe */
		if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL)
			create_ipsec_decrypt_shared_object_tunnel(&actions.crypto_encap, rules[i].inner_l3_type);
		actions.crypto_encap.icv_size = rules[i].sa_attrs.icv_length;
		syndrome_match.meta.pkt_meta = (1U << 31) | i;
		result = doca_flow_pipe_add_entry(0, syndrome_pipe, &syndrome_match, &actions, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &decrypt_status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
		decrypt_status.entries_in_queue++;

		result = add_bad_syndrome_pipe_entry(bad_syndrome_pipe, &rules[i], i, &decrypt_status, flags);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
		decrypt_status.entries_in_queue += NUM_OF_SYNDROMES;

		if (decrypt_status.entries_in_queue >= QUEUE_DEPTH - 8) {
			result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, decrypt_status.entries_in_queue);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
				return result;
			}
			if (decrypt_status.failure || decrypt_status.entries_in_queue == QUEUE_DEPTH) {
				DOCA_LOG_ERR("Failed to process entries");
				return DOCA_ERROR_BAD_STATE;
			}
		}
	}

	/* process the entries in the decryption pipe*/
	do {
		result = process_entries(port, &decrypt_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (decrypt_status.entries_in_queue > 0);
	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_insert_decrypt_rules(struct ipsec_security_gw_ports_map *port, struct ipsec_security_gw_config *app_cfg,
					uint16_t hairpin_queue_id, struct doca_flow_pipe **decrypt_root)
{
	uint32_t nb_queues = app_cfg->dpdk_config->port_config.nb_queues;
	uint16_t rss_queues[nb_queues];
	uint32_t rss_flags;
	struct doca_flow_pipe *empty_pipe = NULL;
	struct doca_flow_port *secured_port;
	struct doca_flow_fwd fwd;
	bool is_root;
	doca_error_t result;

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		secured_port = port->port;
		is_root = true;
	} else {
		secured_port = doca_flow_port_switch_get(NULL);
		is_root = false;

		result = create_empty_pipe(&empty_pipe);
		if (result != DOCA_SUCCESS)
			return result;

		result = create_rss_pipe(port->port, hairpin_queue_id, app_cfg->offload, DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT);
		if (result != DOCA_SUCCESS)
			return result;
	}

	result = create_bad_syndrome_pipe(secured_port, &app_cfg->decrypt_pipes.bad_syndrome_ipv4_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_bad_syndrome_pipe(secured_port, &app_cfg->decrypt_pipes.bad_syndrome_ipv6_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	rss_flags = DOCA_FLOW_RSS_IPV4;
	create_hairpin_pipe_fwd(app_cfg, port->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT, rss_queues,
				rss_flags, &fwd);

	result = create_ipsec_syndrome_pipe(secured_port, app_cfg, &fwd, app_cfg->decrypt_pipes.bad_syndrome_ipv4_pipe, &app_cfg->decrypt_pipes.syndrome_ipv4_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	rss_flags = DOCA_FLOW_RSS_IPV6;
	create_hairpin_pipe_fwd(app_cfg, port->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT, rss_queues,
				rss_flags, &fwd);

	result = create_ipsec_syndrome_pipe(secured_port, app_cfg, &fwd, app_cfg->decrypt_pipes.bad_syndrome_ipv6_pipe, &app_cfg->decrypt_pipes.syndrome_ipv6_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_decrypt_dummy_shared_object(app_cfg, secured_port);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_decrypt_pipe(secured_port, DOCA_FLOW_L3_TYPE_IP4, !app_cfg->sw_antireplay,
					   app_cfg->decrypt_pipes.syndrome_ipv4_pipe, &app_cfg->decrypt_pipes.decrypt_ipv4_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_decrypt_pipe(secured_port, DOCA_FLOW_L3_TYPE_IP6, !app_cfg->sw_antireplay,
					   app_cfg->decrypt_pipes.syndrome_ipv6_pipe, &app_cfg->decrypt_pipes.decrypt_ipv6_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_control_pipe(secured_port, is_root, decrypt_root);
	if (result != DOCA_SUCCESS)
		return result;

	result = add_control_pipe_entries(*decrypt_root, app_cfg->decrypt_pipes, app_cfg->mode);
	if (result != DOCA_SUCCESS)
		return result;

	/* if socket configuration is enabled, no need to add entries from the config file, entries will be added
	 * in runtime.
	 */
	if (!app_cfg->socket_ctx.socket_conf) {
		result = add_decrypt_entries(app_cfg->app_rules.decrypt_rules, app_cfg->app_rules.nb_decrypted_rules,
					     app_cfg->decrypt_pipes, app_cfg->app_rules.nb_encrypted_rules,
					     secured_port, app_cfg);
		if (result != DOCA_SUCCESS)
			return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Find packet's segment for the specified offset.
 *
 * @mb [in]: packet mbuf
 * @offset [in]: offset in the packet
 * @seg_buf [out]: the segment that contain the offset
 * @seg_offset [out]: offset in the segment
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
mbuf_get_seg_ofs(struct rte_mbuf *mb, uint32_t offset, struct rte_mbuf **seg_buf, uint32_t *seg_offset)
{
	uint32_t packet_len, seg_len;
	struct rte_mbuf *tmp_buf;

	packet_len = mb->pkt_len;

	/* if offset is the end of packet */
	if (offset >= packet_len) {
		DOCA_LOG_ERR("Packet offset is invalid");
		return DOCA_ERROR_INVALID_VALUE;
	}

	tmp_buf = mb;
	for (seg_len = rte_pktmbuf_data_len(tmp_buf); seg_len <= offset; seg_len = rte_pktmbuf_data_len(tmp_buf)) {
		tmp_buf = tmp_buf->next;
		offset -= seg_len;
	}

	*seg_offset = offset;
	*seg_buf = tmp_buf;
	return DOCA_SUCCESS;
}

/*
 * Remove packet trailer - padding, ESP tail, and ICV
 *
 * @m [in]: the mbuf to update
 * @icv_len [in]: ICV length
 * @next_proto [out]: ESP tail next protocol field
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
remove_packet_tail(struct rte_mbuf **m, uint32_t icv_len, uint32_t *next_proto)
{
	struct rte_mbuf *ml;
	const struct rte_esp_tail *esp_tail;
	uint32_t esp_tail_offset, esp_tail_seg_offset, trailer_len;
	doca_error_t result;

	/* remove trailing zeros */
	remove_trailing_zeros(m);

	/* find tail offset */
	trailer_len = icv_len + sizeof(struct rte_esp_tail);

	/* find tail offset */
	esp_tail_offset = (*m)->pkt_len - trailer_len;

	/* get the segment with the tail offset */
	result = mbuf_get_seg_ofs(*m, esp_tail_offset, &ml, &esp_tail_seg_offset);
	if (result != DOCA_SUCCESS)
		return result;

	esp_tail = rte_pktmbuf_mtod_offset(ml, const struct rte_esp_tail *, esp_tail_seg_offset);
	*next_proto = esp_tail->next_proto;
	trailer_len += esp_tail->pad_len;
	esp_tail_seg_offset -= esp_tail->pad_len;

	/* remove padding, tail and icv from the end of the packet */
	(*m)->pkt_len -= trailer_len;
	ml->data_len = esp_tail_seg_offset;
	return DOCA_SUCCESS;
}

/*
 * Decap mbuf for tunnel mode
 *
 * @m [in]: the mbuf to update
 * @ctx [in]: the security gateway context
 * @rule_idx [in]: the index of the rule to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
decap_packet_tunnel(struct rte_mbuf **m, struct ipsec_security_gw_core_ctx *ctx, uint32_t rule_idx)
{
	uint32_t iv_len = 8;
	struct rte_ether_hdr *l2_header;
	struct rte_ipv4_hdr *ipv4;
	uint32_t proto, l3_len, l2_len;
	char *op, *np;
	int i;
	struct decrypt_rule *rule = &ctx->decrypt_rules[rule_idx];
	uint32_t icv_len = rule->sa_attrs.icv_length;
	doca_error_t result;

	result = remove_packet_tail(m, icv_len, &proto);
	if (result != DOCA_SUCCESS)
		return result;

	/* calculate l3 len  */
	l2_header = rte_pktmbuf_mtod(*m, struct rte_ether_hdr *);
	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(l2_header + 1);
		l3_len = rte_ipv4_hdr_len(ipv4);
	} else {
		l3_len = sizeof(struct rte_ipv6_hdr);
	}

	op = rte_pktmbuf_mtod(*m, char *);
	/* remove l3 and ESP header from the beginning of the packet */
	np = rte_pktmbuf_adj(*m, l3_len + sizeof(struct rte_esp_hdr) + iv_len);
	if (unlikely(np == NULL))
		return DOCA_ERROR_INVALID_VALUE;

	/* copy old l2 to the new beginning */
	l2_len = sizeof(struct rte_ether_hdr);
	for (i = l2_len - 1; i >= 0; i--)
		np[i] = op[i];

	/* change the ether type based on the tail proto */
	l2_header = rte_pktmbuf_mtod(*m, struct rte_ether_hdr *);
	if (proto == IPPROTO_IPV6)
		l2_header->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6);
	else
		l2_header->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	return DOCA_SUCCESS;
}

/*
 * Decap mbuf for transport and udp transport mode
 *
 * @m [in]: the mbuf to update
 * @ctx [in]: the security gateway context
 * @rule_idx [in]: the index of the rule to use
 * @udp_transport [in]: true for UDP transport mode
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
decap_packet_transport(struct rte_mbuf **m, struct ipsec_security_gw_core_ctx *ctx, uint32_t rule_idx, bool udp_transport)
{
	uint32_t iv_len = 8;
	struct rte_ether_hdr *l2_header;
	char *op, *np;
	struct rte_ipv4_hdr *ipv4 = NULL;
	struct rte_ipv6_hdr *ipv6 = NULL;
	uint32_t l2_l3_len, proto;
	int i;
	struct decrypt_rule *rule = &ctx->decrypt_rules[rule_idx];
	uint32_t icv_len = rule->sa_attrs.icv_length;
	doca_error_t result;

	result = remove_packet_tail(m, icv_len, &proto);
	if (result != DOCA_SUCCESS)
		return result;

	/* calculate l2 and l3 len  */
	l2_header = rte_pktmbuf_mtod(*m, struct rte_ether_hdr *);
	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(l2_header + 1);
		l2_l3_len = rte_ipv4_hdr_len(ipv4) + sizeof(struct rte_ether_hdr);
	} else {
		ipv6 = (void *)(l2_header + 1);
		l2_l3_len = sizeof(struct rte_ipv6_hdr) + sizeof(struct rte_ether_hdr);
	}

	/* remove ESP header from the beginning of the packet and UDP header in udp_transport mode*/
	op = rte_pktmbuf_mtod(*m, char *);
	if (!udp_transport)
		np = rte_pktmbuf_adj(*m, sizeof(struct rte_esp_hdr) + iv_len);
	else
		np = rte_pktmbuf_adj(*m, sizeof(struct rte_esp_hdr) + sizeof(struct rte_udp_hdr) + iv_len);
	if (unlikely(np == NULL))
		return DOCA_ERROR_INVALID_VALUE;

	/* align IP length, next protocol and IPv4 checksum */
	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4->next_proto_id = proto;
		ipv4->total_length = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr));
		ipv4->hdr_checksum = 0;
		ipv4->hdr_checksum = rte_ipv4_cksum(ipv4);
	} else {
		ipv6->proto = proto;
		ipv6->payload_len = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr) - sizeof(*ipv6));
		/* IPv6 does not have checksum */
	}

	/* copy old l2 and l3 to the new beginning */
	for (i = l2_l3_len - 1; i >= 0; i--)
		np[i] = op[i];
	return DOCA_SUCCESS;
}

/*
 * extract the sn from the mbuf
 *
 * @m [in]: the mbuf to extract from
 * @mode [in]: application running mode
 * @sn [out]: the sn
 */
static void
get_esp_sn(struct rte_mbuf *m, enum ipsec_security_gw_mode mode, uint32_t *sn)
{
	uint32_t l2_l3_len;
	struct rte_ether_hdr *oh;
	struct rte_ipv4_hdr *ipv4;
	struct rte_esp_hdr *esp_hdr;

	oh = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	if (RTE_ETH_IS_IPV4_HDR(m->packet_type)) {
		ipv4 = (void *)(oh + 1);
		l2_l3_len = rte_ipv4_hdr_len(ipv4) + sizeof(struct rte_ether_hdr);
	} else {
		l2_l3_len = sizeof(struct rte_ipv6_hdr) + sizeof(struct rte_ether_hdr);
	}

	if (mode == IPSEC_SECURITY_GW_UDP_TRANSPORT)
		l2_l3_len += sizeof(struct rte_udp_hdr);

	esp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_esp_hdr *, l2_l3_len);
	*sn = rte_be_to_cpu_32(esp_hdr->seq);
}

/*
 * Perform anti replay check on a packet and update the state accordingly
 * (1) If sn is left (smaller) from window - drop.
 * (2) Else, if sn is in the window - check if it was already received (drop) or not (update bitmap).
 * (3) Else, if sn is larger than window - slide the window so that sn is the last packet in the window and update bitmap.
 *
 * @sn [in]: the sequence number to check
 * @state [in/out]: the anti replay state
 * @drop [out]: true if the packet should be dropped
 *
 * @NOTE: Only supports 64 window size and regular sn (not ESN)
 */
static void
anti_replay(uint32_t sn, struct antireplay_state *state, bool *drop)
{
	uint32_t diff, beg_win_sn;
	uint32_t window_size = state->window_size;
	uint32_t *end_win_sn = &state->end_win_sn;
	uint64_t *bitmap = &state->bitmap;

	beg_win_sn = *end_win_sn + 1 - window_size; /* the first sn in the window */
	*drop = true;

	/* (1) Check if sn is smaller than beggining of the window */
	if (sn < beg_win_sn)
		return; /* drop */
	/* (2) Check if sn is in the window */
	if (sn <= *end_win_sn) {
		diff = sn - beg_win_sn;
		/* Check if sn is already received */
		if (*bitmap & (((uint64_t)1) << diff))
			return; /* drop */
		else {
			*bitmap |= (((uint64_t)1) << diff);
			*drop = false;
		}
	/* (3) sn is larger than end of window */
	} else { /* move window and set last bit */
		diff = sn - *end_win_sn;
		if (diff >= window_size) {
			*bitmap = (((uint64_t)1) << (window_size - 1));
			*drop = false;
		} else {
			*bitmap = (*bitmap >> diff);
			*bitmap |= (((uint64_t)1) << (window_size - 1));
			*drop = false;
		}
		*end_win_sn = sn;
	}
}

void
handle_secured_packets_received(uint16_t nb_packets, struct rte_mbuf **packets,  struct ipsec_security_gw_core_ctx *ctx,
	uint16_t *nb_processed_packets, struct rte_mbuf **processed_packets, struct rte_mbuf **unprocessed_packets)
{
	uint32_t meta_mask;
	uint32_t rule_idx;
	uint32_t current_packet;
	uint32_t sn;
	int unprocessed_packets_idx = 0;
	bool drop;
	doca_error_t result;

	*nb_processed_packets = 0;

	meta_mask = (1 << 30);
	meta_mask -= 1; /* rule index is set on the 30 LSB */

	for (current_packet = 0; current_packet < nb_packets; current_packet++) {
		if (!rte_flow_dynf_metadata_avail())
			goto add_dropped;
		rule_idx = *RTE_FLOW_DYNF_METADATA(packets[current_packet]);
		rule_idx &= meta_mask;
		if (ctx->config->sw_antireplay) {
			/* Validate anti replay according to the entry's state */
			get_esp_sn(packets[current_packet], ctx->config->mode, &sn);
			anti_replay(sn, &(ctx->antireplay_states[rule_idx]), &drop); /* No synchronization needed, same rule is processed by the same core */
			if (drop) {
				DOCA_LOG_WARN("Anti Replay mechanism dropped packet- sn: %u, rule index: %d", sn, rule_idx);
				goto add_dropped;
			}
		}

		if (ctx->config->mode == IPSEC_SECURITY_GW_TRANSPORT)
			result = decap_packet_transport(&packets[current_packet], ctx, rule_idx, false);
		else if (ctx->config->mode == IPSEC_SECURITY_GW_UDP_TRANSPORT)
			result = decap_packet_transport(&packets[current_packet], ctx, rule_idx, true);
		else
			result = decap_packet_tunnel(&packets[current_packet], ctx, rule_idx);
		if (result != DOCA_SUCCESS)
			goto add_dropped;

		processed_packets[(*nb_processed_packets)++] = packets[current_packet];
		continue;

add_dropped:
		unprocessed_packets[unprocessed_packets_idx++] = packets[current_packet];
	}
}
