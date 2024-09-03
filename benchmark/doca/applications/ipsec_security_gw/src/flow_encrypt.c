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

#include <doca_log.h>

#include <pack.h>
#include <utils.h>

#include "doca_flow.h"
#include "flow_encrypt.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW::flow_encrypt);

#define ENCAP_DST_IP_IDX_IP4 30		  /* index in encap raw data for destination IPv4 */
#define ENCAP_DST_IP_IDX_IP6 38		  /* index in encap raw data for destination IPv4 */
#define ENCAP_DST_UDP_PORT_IDX 2	  /* index in encap raw data for UDP destination port */
#define ENCAP_ESP_SPI_IDX_TUNNEL_IP4 34	  /* index in encap raw data for esp SPI in IPv4 tunnel */
#define ENCAP_ESP_SPI_IDX_TUNNEL_IP6 54	  /* index in encap raw data for esp SPI in IPv6 tunnel */
#define ENCAP_ESP_SPI_IDX_TRANSPORT 0	  /* index in encap raw data for esp SPI in transport mode*/
#define ENCAP_ESP_SPI_IDX_UDP_TRANSPORT 8 /* index in encap raw data for esp SPI in transport over UDP mode*/
#define ENCAP_ESP_SN_IDX_TUNNEL_IP4 38	  /* index in encap raw data for esp SN in IPv4 tunnel */
#define ENCAP_ESP_SN_IDX_TUNNEL_IP6 58	  /* index in encap raw data for esp SN in IPv6 tunnel */
#define ENCAP_ESP_SN_IDX_TRANSPORT 4	  /* index in encap raw data for esp SN in transport mode*/
#define ENCAP_ESP_SN_IDX_UDP_TRANSPORT 12 /* index in encap raw data for esp SN in transport over UDP mode*/

#define PADDING_ALIGN 4			  /* padding alignment */

static const uint8_t esp_pad_bytes[15] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

/*
 * Create reformat data for encapsulation in transport mode, and copy it to reformat_data pointer
 *
 * @rule [in]: current rule for encapsulation
 * @sw_sn_inc [in]: if true, sequence number will be incremented in software
 * @reformat_data [out]: pointer to created data
 * @reformat_data_sz [out]: data size
 */
static void
create_transport_encap(struct encrypt_rule *rule, bool sw_sn_inc, uint8_t *reformat_data, uint16_t *reformat_data_sz)
{
	uint8_t reformat_encap_data[16] = {
		0x00, 0x00, 0x00, 0x00, /* SPI */
		0x00, 0x00, 0x00, 0x00, /* SN */
		0x00, 0x00, 0x00, 0x00, /* IV */
		0x00, 0x00, 0x00, 0x00,
	};

	reformat_encap_data[ENCAP_ESP_SPI_IDX_TRANSPORT] = GET_BYTE(rule->esp_spi, 3);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TRANSPORT + 1] = GET_BYTE(rule->esp_spi, 2);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TRANSPORT + 2] = GET_BYTE(rule->esp_spi, 1);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TRANSPORT + 3] = GET_BYTE(rule->esp_spi, 0);

	if (sw_sn_inc == true) {
		reformat_encap_data[ENCAP_ESP_SN_IDX_TRANSPORT] = GET_BYTE(rule->current_sn, 3);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TRANSPORT + 1] = GET_BYTE(rule->current_sn, 2);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TRANSPORT + 2] = GET_BYTE(rule->current_sn, 1);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TRANSPORT + 3] = GET_BYTE(rule->current_sn, 0);
	}

	memcpy(reformat_data, reformat_encap_data, sizeof(reformat_encap_data));
	*reformat_data_sz = sizeof(reformat_encap_data);
}

/*
 * Create reformat data for encapsulation in UDP transport mode, and copy it to reformat_data pointer
 *
 * @rule [in]: current rule for encapsulation
 * @sw_sn_inc [in]: if true, sequence number will be incremented in software
 * @reformat_data [out]: pointer to created data
 * @reformat_data_sz [out]: data size
 */
static void
create_udp_transport_encap(struct encrypt_rule *rule, bool sw_sn_inc, uint8_t *reformat_data, uint16_t *reformat_data_sz)
{
	uint16_t udp_dst_port = 4500;
	uint8_t reformat_encap_data[24] = {
		0x30, 0x39, 0x00, 0x00, /* UDP src/dst */
		0x00, 0xa4, 0x00, 0x00, /* USD sum/len */
		0x00, 0x00, 0x00, 0x00, /* SPI */
		0x00, 0x00, 0x00, 0x00, /* SN */
		0x00, 0x00, 0x00, 0x00, /* IV */
		0x00, 0x00, 0x00, 0x00,
	};

	reformat_encap_data[ENCAP_ESP_SPI_IDX_UDP_TRANSPORT] = GET_BYTE(rule->esp_spi, 3);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_UDP_TRANSPORT + 1] = GET_BYTE(rule->esp_spi, 2);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_UDP_TRANSPORT + 2] = GET_BYTE(rule->esp_spi, 1);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_UDP_TRANSPORT + 3] = GET_BYTE(rule->esp_spi, 0);

	reformat_encap_data[ENCAP_DST_UDP_PORT_IDX] = GET_BYTE(udp_dst_port, 1);
	reformat_encap_data[ENCAP_DST_UDP_PORT_IDX + 1] = GET_BYTE(udp_dst_port, 0);

	if (sw_sn_inc == true) {
		reformat_encap_data[ENCAP_ESP_SN_IDX_UDP_TRANSPORT] = GET_BYTE(rule->current_sn, 3);
		reformat_encap_data[ENCAP_ESP_SN_IDX_UDP_TRANSPORT + 1] = GET_BYTE(rule->current_sn, 2);
		reformat_encap_data[ENCAP_ESP_SN_IDX_UDP_TRANSPORT + 2] = GET_BYTE(rule->current_sn, 1);
		reformat_encap_data[ENCAP_ESP_SN_IDX_UDP_TRANSPORT + 3] = GET_BYTE(rule->current_sn, 0);
	}

	memcpy(reformat_data, reformat_encap_data, sizeof(reformat_encap_data));
	*reformat_data_sz = sizeof(reformat_encap_data);
}

/*
 * Create reformat data for encapsulation IPV4 tunnel, and copy it to reformat_data pointer
 *
 * @rule [in]: current rule for encapsulation
 * @sw_sn_inc [in]: if true, sequence number will be incremented in software
 * @reformat_data [out]: pointer to created data
 * @reformat_data_sz [out]: data size
 */
static void
create_ipv4_tunnel_encap(struct encrypt_rule *rule, bool sw_sn_inc, uint8_t *reformat_data, uint16_t *reformat_data_sz)
{
	uint8_t reformat_encap_data[50] = {
		0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,			/* mac_dst */
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66,			/* mac_src */
		0x08, 0x00,						/* mac_type */
		0x45, 0x00, 0x00, 0x00, 0x00, 0x00,			/* IP v4 */
		0x00, 0x00, 0x00, 0x32, 0x00, 0x00,
		0x02, 0x02, 0x02, 0x02,					/* IP src */
		0x00, 0x00, 0x00, 0x00,					/* IP dst */
		0x00, 0x00, 0x00, 0x00,					/* SPI */
		0x00, 0x00, 0x00, 0x00,					/* SN */
		0x00, 0x00, 0x00, 0x00,					/* IV */
		0x00, 0x00, 0x00, 0x00,
	};

	/* dst IP was already converted to big endian */
	reformat_encap_data[ENCAP_DST_IP_IDX_IP4] = GET_BYTE(rule->encap_dst_ip4, 0);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP4 + 1] = GET_BYTE(rule->encap_dst_ip4, 1);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP4 + 2] = GET_BYTE(rule->encap_dst_ip4, 2);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP4 + 3] = GET_BYTE(rule->encap_dst_ip4, 3);

	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP4] = GET_BYTE(rule->esp_spi, 3);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP4 + 1] = GET_BYTE(rule->esp_spi, 2);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP4 + 2] = GET_BYTE(rule->esp_spi, 1);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP4 + 3] = GET_BYTE(rule->esp_spi, 0);

	if (sw_sn_inc == true) {
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP4] = GET_BYTE(rule->current_sn, 3);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP4 + 1] = GET_BYTE(rule->current_sn, 2);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP4 + 2] = GET_BYTE(rule->current_sn, 1);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP4 + 3] = GET_BYTE(rule->current_sn, 0);
	}

	memcpy(reformat_data, reformat_encap_data, sizeof(reformat_encap_data));
	*reformat_data_sz = sizeof(reformat_encap_data);
}

/*
 * Create reformat data for encapsulation IPV6 tunnel, and copy it to reformat_data pointer
 *
 * @rule [in]: current rule for encapsulation
 * @sw_sn_inc [in]: if true, sequence number will be incremented in software
 * @reformat_data [out]: pointer to created data
 * @reformat_data_sz [out]: data size
 */
static void
create_ipv6_tunnel_encap(struct encrypt_rule *rule, bool sw_sn_inc, uint8_t *reformat_data, uint16_t *reformat_data_sz)
{
	uint8_t reformat_encap_data[70] = {
		0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,		/* mac_dst */
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66,		/* mac_src */
		0x86, 0xdd,					/* mac_type */
		0x60, 0x00, 0x00, 0x00,				/* IP v6 */
		0x00, 0x00, 0x32, 0x40,
		0x02, 0x02, 0x02, 0x02,				/* IP src */
		0x02, 0x02, 0x02, 0x02,				/* IP src */
		0x02, 0x02, 0x02, 0x02,				/* IP src */
		0x02, 0x02, 0x02, 0x02,				/* IP src */
		0x01, 0x01, 0x01, 0x01,				/* IP dst */
		0x01, 0x01, 0x01, 0x01,				/* IP dst */
		0x01, 0x01, 0x01, 0x01,				/* IP dst */
		0x01, 0x01, 0x01, 0x01,				/* IP dst */
		0x00, 0x00, 0x00, 0x00,				/* SPI */
		0x00, 0x00, 0x00, 0x00,				/* SN */
		0x00, 0x00, 0x00, 0x00,				/* IV */
		0x00, 0x00, 0x00, 0x00,
	};

	/* dst IP was already converted to big endian */
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6] = GET_BYTE(rule->encap_dst_ip6[0], 0);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 1] = GET_BYTE(rule->encap_dst_ip6[0], 1);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 2] = GET_BYTE(rule->encap_dst_ip6[0], 2);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 3] = GET_BYTE(rule->encap_dst_ip6[0], 3);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 4] = GET_BYTE(rule->encap_dst_ip6[1], 0);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 5] = GET_BYTE(rule->encap_dst_ip6[1], 1);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 6] = GET_BYTE(rule->encap_dst_ip6[1], 2);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 7] = GET_BYTE(rule->encap_dst_ip6[1], 3);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 8] = GET_BYTE(rule->encap_dst_ip6[2], 0);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 9] = GET_BYTE(rule->encap_dst_ip6[2], 1);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 10] = GET_BYTE(rule->encap_dst_ip6[2], 2);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 11] = GET_BYTE(rule->encap_dst_ip6[2], 3);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 12] = GET_BYTE(rule->encap_dst_ip6[3], 0);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 13] = GET_BYTE(rule->encap_dst_ip6[3], 1);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 14] = GET_BYTE(rule->encap_dst_ip6[3], 2);
	reformat_encap_data[ENCAP_DST_IP_IDX_IP6 + 15] = GET_BYTE(rule->encap_dst_ip6[3], 3);

	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP6] = GET_BYTE(rule->esp_spi, 3);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP6 + 1] = GET_BYTE(rule->esp_spi, 2);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP6 + 2] = GET_BYTE(rule->esp_spi, 1);
	reformat_encap_data[ENCAP_ESP_SPI_IDX_TUNNEL_IP6 + 3] = GET_BYTE(rule->esp_spi, 0);

	if (sw_sn_inc == true) {
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP6] = GET_BYTE(rule->current_sn, 3);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP6 + 1] = GET_BYTE(rule->current_sn, 2);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP6 + 2] = GET_BYTE(rule->current_sn, 1);
		reformat_encap_data[ENCAP_ESP_SN_IDX_TUNNEL_IP6 + 3] = GET_BYTE(rule->current_sn, 0);
	}

	memcpy(reformat_data, reformat_encap_data, sizeof(reformat_encap_data));
	*reformat_data_sz = sizeof(reformat_encap_data);
}

/*
 * Create egress IP classifier to choose to which encrypt pipe to forward the packet
 *
 * @port [in]: port of the pipe
 * @is_root [in]: true for vnf mode
 * @encrypt_pipes [in]: all the encrypt pipes
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_egress_ip_classifier(struct doca_flow_port *port, bool is_root, struct encrypt_pipes *encrypt_pipes)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;
	struct entries_status status;
	int num_of_entries = 2;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "IP_CLASSIFIER_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.is_root = is_root;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS;
	pipe_cfg.attr.dir_info = DOCA_FLOW_DIRECTION_HOST_TO_NETWORK;
	pipe_cfg.match = &match;
	pipe_cfg.port = port;

	match.parser_meta.outer_l3_type = UINT32_MAX;

	fwd.type = DOCA_FLOW_FWD_PIPE;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, &encrypt_pipes->egress_ip_classifier);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create IP classifier pipe: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	fwd.next_pipe = encrypt_pipes->ipv4_encrypt_pipe;

	result = doca_flow_pipe_add_entry(0, encrypt_pipes->egress_ip_classifier, &match, NULL, NULL, &fwd, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add ipv4 entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
	fwd.next_pipe = encrypt_pipes->ipv6_encrypt_pipe;

	result = doca_flow_pipe_add_entry(0, encrypt_pipes->egress_ip_classifier, &match, NULL, NULL, &fwd, DOCA_FLOW_NO_WAIT, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add ipv6 entry: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS)
		return result;
	if (status.nb_processed != num_of_entries || status.failure)
		return DOCA_ERROR_BAD_STATE;

	return DOCA_SUCCESS;
}

/*
 * Create ipsec encrypt pipe changeable meta data match and changeable shared IPSEC encryption object
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID to forward the packet to
 * @app_cfg [in]: application configuration struct
 * @l3_type [in]: DOCA_FLOW_L3_TYPE_IP4 / DOCA_FLOW_L3_TYPE_IP6
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_encrypt_pipe(struct doca_flow_port *port, uint16_t port_id, struct ipsec_security_gw_config *app_cfg,
			  enum doca_flow_l3_type l3_type, struct doca_flow_pipe **pipe)
{
	int nb_actions = 2;
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions, actions_arr[nb_actions];
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "ENCRYPT_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS;
	pipe_cfg.attr.dir_info = DOCA_FLOW_DIRECTION_HOST_TO_NETWORK;
	pipe_cfg.attr.enable_strict_matching = true;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.attr.nb_actions = app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL ? nb_actions : nb_actions - 1; /* 2 actions only in tunnel mode */
	pipe_cfg.port = port;

	match_mask.meta.pkt_meta = 0x0fffffff;
	match.meta.pkt_meta = 0xffffffff;
	match_mask.outer.l3_type = l3_type;
	match.outer.l3_type = l3_type;

	if (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH || app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_ENCAP)
		actions.has_crypto_encap = true;

	actions.crypto_encap.action_type = DOCA_FLOW_CRYPTO_REFORMAT_ENCAP;
	actions.crypto_encap.icv_size = 0xffff;
	actions.crypto.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	actions.crypto.action_type = DOCA_FLOW_CRYPTO_ACTION_ENCRYPT;
	actions.crypto.crypto_id = ENCRYPT_DUMMY_ID;
	actions.crypto.esp.sn_en = !app_cfg->sw_sn_inc_enable;

	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL) {
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_ESP_TUNNEL;
		actions_arr[0] = actions;
		actions_arr[1] = actions;
		/* action idx 0 will add encap ipv4 */
		memset(actions_arr[0].crypto_encap.encap_data, 0xff, 50);
		actions_arr[0].crypto_encap.data_size = 50;
		/* action idx 1 will add encap ipv6 */
		memset(actions_arr[1].crypto_encap.encap_data, 0xff, 70);
		actions_arr[1].crypto_encap.data_size = 70;
	} else if (app_cfg->mode == IPSEC_SECURITY_GW_TRANSPORT) {
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_ESP_OVER_IP;
		memset(actions.crypto_encap.encap_data, 0xff, 16);
		actions.crypto_encap.data_size = 16;
		actions_arr[0] = actions;
	} else {
		actions.crypto_encap.net_type = DOCA_FLOW_CRYPTO_HEADER_UDP_ESP_OVER_IP;
		memset(actions.crypto_encap.encap_data, 0xff, 24);
		actions.crypto_encap.data_size = 24;
		actions_arr[0] = actions;
	}

	struct doca_flow_actions *actions_list[] = {&actions_arr[0], &actions_arr[1]};

	pipe_cfg.actions = actions_list;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create encrypt pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create pipe with source ipv6 match, and fwd to the hairpin pipe
 *
 * @port [in]: port of the pipe
 * @protocol_type [in]: DOCA_FLOW_L4_TYPE_EXT_TCP / DOCA_FLOW_L4_TYPE_EXT_UDP
 * @hairpin_pipe [in]: pipe to forward the packets
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_src_ip6_pipe(struct doca_flow_port *port, enum doca_flow_l4_type_ext protocol_type,
			  struct doca_flow_pipe *hairpin_pipe, struct doca_flow_pipe **pipe)
{
	int nb_actions = 1;
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_actions actions, *actions_arr[nb_actions];
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));
	memset(&actions, 0, sizeof(actions));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SRC_IP6_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = nb_actions;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.port = port;

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
	match.parser_meta.outer_l4_type = (protocol_type == DOCA_FLOW_L4_TYPE_EXT_TCP) ? DOCA_FLOW_L4_META_TCP : DOCA_FLOW_L4_META_UDP;
	SET_IP6_ADDR(match.outer.ip6.src_ip, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);

	actions.meta.u32[0] = UINT32_MAX;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = hairpin_pipe;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create hairpin pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create pipe with 5 tuple match, changeable set meta, and fwd to the second port
 *
 * @port [in]: port of the pipe
 * @protocol_type [in]: DOCA_FLOW_L4_TYPE_EXT_TCP / DOCA_FLOW_L4_TYPE_EXT_UDP
 * @l3_type [in]: DOCA_FLOW_L3_TYPE_IP4 / DOCA_FLOW_L3_TYPE_IP6
 * @fwd [in]: pointer to forward struct
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_hairpin_pipe(struct doca_flow_port *port, enum doca_flow_l4_type_ext protocol_type,
			  enum doca_flow_l3_type l3_type, struct doca_flow_fwd *fwd, struct doca_flow_pipe **pipe)
{
	int nb_actions = 1;
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[nb_actions];
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "HAIRPIN_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = nb_actions;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.port = port;

	match.outer.l4_type_ext = protocol_type;
	match.outer.l3_type = l3_type;
	if (l3_type == DOCA_FLOW_L3_TYPE_IP4) {
		match.outer.ip4.dst_ip = 0xffffffff;
		match.outer.ip4.src_ip = 0xffffffff;
	} else {
		match.meta.u32[0] = UINT32_MAX;
		SET_IP6_ADDR(match.outer.ip6.dst_ip, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
	}

	SET_L4_PORT(outer, src_port, 0xffff);
	SET_L4_PORT(outer, dst_port, 0xffff);

	actions.meta.pkt_meta = 0xffffffff;

	result = doca_flow_pipe_create(&pipe_cfg, fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create hairpin pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create control pipe for unsecured port
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
 * Add control pipe entries
 * - entry that forwards IPv4 TCP traffic to IPv4 TCP pipe,
 * - entry that forwards IPv4 UDP traffic to IPv4 UDP pipe,
 * - entry that forwards IPv6 TCP traffic to source IPv6 TCP pipe,
 * - entry that forwards IPv6 UDP traffic to source IPv6 UDP pipe,
 * - entry with lower priority that drop the packets
 *
 * @control_pipe [in]: control pipe pointer
 * @pipes [in]: all the pipes to forward the packets to
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_control_pipe_entries(struct doca_flow_pipe *control_pipe, struct encrypt_pipes pipes)
{
	struct doca_flow_pipe_entry *entry;
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv4_tcp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add TCP IPv4 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv4_udp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add UDP IPv4 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv6_src_tcp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add TCP IPv6 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv6_src_udp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add UDP IPv6 control entry: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Update the crypto config for encrypt transport mode according to the rule
 *
 * @crypto_cfg [in]: shared object config
 * @rule [in]: encrypt rule
 */
static void
create_ipsec_encrypt_shared_object_transport(struct doca_flow_crypto_encap_action *crypto_cfg, struct encrypt_rule *rule)
{
	create_transport_encap(rule, false, crypto_cfg->encap_data, &crypto_cfg->data_size);
}

/*
 * Update the crypto config for encrypt transport over UDP mode according to the rule
 *
 * @crypto_cfg [in]: shared object config
 * @rule [in]: encrypt rule
 */
static void
create_ipsec_encrypt_shared_object_transport_over_udp(struct doca_flow_crypto_encap_action *crypto_cfg,
						      struct encrypt_rule *rule)
{
	create_udp_transport_encap(rule, false, crypto_cfg->encap_data, &crypto_cfg->data_size);
}

/*
 * Update the crypto config for encrypt trunnel mode according to the rule
 *
 * @crypto_cfg [in]: shared object config
 * @rule [in]: encrypt rule
 */
static void
create_ipsec_encrypt_shared_object_tunnel(struct doca_flow_crypto_encap_action *crypto_cfg, struct encrypt_rule *rule)
{
	if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
		create_ipv4_tunnel_encap(rule, false, crypto_cfg->encap_data, &crypto_cfg->data_size);
	else
		create_ipv6_tunnel_encap(rule, false, crypto_cfg->encap_data, &crypto_cfg->data_size);
}

/*
 * Config and bind shared IPSEC object for encryption
 *
 * @port [in]: port to bind the shared object to
 * @sa [in]: crypto object handle (IPsec offload object)
 * @ipsec_id [in]: shared object ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_encrypt_shared_object(struct doca_flow_port *port, void *sa, uint32_t ipsec_id)
{
	struct doca_flow_shared_resource_cfg cfg;
	struct doca_flow_resource_crypto_cfg crypto_cfg;
	doca_error_t result;

	memset(&crypto_cfg, 0, sizeof(crypto_cfg));
	memset(&cfg, 0, sizeof(cfg));

	cfg.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS;
	crypto_cfg.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	crypto_cfg.security_ctx = sa;
	cfg.crypto_cfg = crypto_cfg;

	/* config ipsec object */
	result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_CRYPTO, ipsec_id, &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to cfg shared ipsec object: %s", doca_error_get_descr(result));
		return result;
	}
	/* bind shared ipsec encrypt object to port */
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
create_ipsec_encrypt_dummy_shared_object(struct ipsec_security_gw_config *cfg, struct doca_flow_port *port)
{
	doca_error_t result;
	struct ipsec_security_gw_sa_attrs dummy_sa_attr = {
		.icv_length = DOCA_IPSEC_ICV_LENGTH_16,
		.key_type = DOCA_ENCRYPTION_KEY_AESGCM_256,
		.enc_key_data[0] = 0x01,
		.salt = 0x12345678,
		.direction = DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT,
	};

	result = ipsec_security_gw_create_ipsec_sa(&dummy_sa_attr, cfg,	&cfg->app_rules.dummy_encrypt_sa);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_encrypt_shared_object(port, cfg->app_rules.dummy_encrypt_sa, ENCRYPT_DUMMY_ID);
	if (result != DOCA_SUCCESS)
		return result;

	return DOCA_SUCCESS;
}

/*
 * Get the relevant pipe for adding the rule
 *
 * @rule [in]: the rule that need to add
 * @pipes [in]: encrypt pipes struct
 * @src_ip6 [in]: true if we want to get the source ipv6 pipe
 * @pipe [out]: output pipe
 */
static void
get_pipe_for_rule(struct encrypt_rule *rule, struct encrypt_pipes pipes, bool src_ip6, struct doca_flow_pipe **pipe)
{
	if (!src_ip6) {
		if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4) {
			if (rule->protocol == DOCA_FLOW_L4_TYPE_EXT_TCP)
				*pipe = pipes.ipv4_tcp_pipe;
			else
				*pipe = pipes.ipv4_udp_pipe;
		} else {
			if (rule->protocol == DOCA_FLOW_L4_TYPE_EXT_TCP)
				*pipe = pipes.ipv6_tcp_pipe;
			else
				*pipe = pipes.ipv6_udp_pipe;
		}
	} else {
		if (rule->protocol == DOCA_FLOW_L4_TYPE_EXT_TCP)
			*pipe = pipes.ipv6_src_tcp_pipe;
		else
			*pipe = pipes.ipv6_src_udp_pipe;
	}
}

/*
 * Add entry to source IPv6 pipe
 *
 * @port [in]: port of the pipe
 * @rule [in]: encrypt rule
 * @pipes [in]: encrypt pipes struct
 * @hairpin_status [in]: the entries status
 * @src_ip_id [in]: source IP unique ID
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_src_ip6_entry(struct doca_flow_port *port, struct encrypt_rule *rule, struct encrypt_pipes pipes,
		  struct entries_status *hairpin_status, uint32_t src_ip_id)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	enum doca_flow_flags_type flags;
	struct doca_flow_pipe *pipe;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	get_pipe_for_rule(rule, pipes, true, &pipe);

	memcpy(match.outer.ip6.src_ip, rule->ip6.src_ip, sizeof(rule->ip6.src_ip));
	actions.meta.u32[0] = src_ip_id;

	if (hairpin_status->entries_in_queue == QUEUE_DEPTH - 1)
		flags = DOCA_FLOW_NO_WAIT;
	else
		flags = DOCA_FLOW_WAIT_FOR_BATCH;

	/* add entry to hairpin pipe*/
	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, flags, hairpin_status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add hairpin pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	hairpin_status->entries_in_queue++;
	if (hairpin_status->entries_in_queue == QUEUE_DEPTH) {
		result = process_entries(port, hairpin_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Add 5-tuple entries based on a rule
 * If ipv6 - add the source IP to different pipe
 *
 * @port [in]: port of the pipe
 * @rule [in]: encrypt rule
 * @pipes [in]: encrypt pipes struct
 * @nb_rules [in]: number of encryption rules
 * @i [in]: rule index
 * @hairpin_status [in]: the entries status
 * @ip6_table [in]: IPv6 addresses hash table
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_five_tuple_match_entry(struct doca_flow_port *port, struct encrypt_rule *rule, struct encrypt_pipes pipes,
			   int nb_rules, int i, struct entries_status *hairpin_status, struct rte_hash *ip6_table)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe *pipe;
	enum doca_flow_flags_type flags;
	int src_ip_id = 0;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP6) {
		src_ip_id = rte_hash_lookup(ip6_table, (void *)&rule->ip6.dst_ip);
		if (src_ip_id < 0) {
			DOCA_LOG_ERR("Failed to find source IP in table");
			return DOCA_ERROR_NOT_FOUND;
		}
		result = add_src_ip6_entry(port, rule, pipes, hairpin_status, src_ip_id);
		if (result != DOCA_SUCCESS)
			return result;
	}

	get_pipe_for_rule(rule, pipes, false, &pipe);

	match.outer.l4_type_ext = rule->protocol;
	SET_L4_PORT(outer, src_port, rte_cpu_to_be_16(rule->src_port));
	SET_L4_PORT(outer, dst_port, rte_cpu_to_be_16(rule->dst_port));

	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4) {
		match.outer.ip4.dst_ip = rule->ip4.dst_ip;
		match.outer.ip4.src_ip = rule->ip4.src_ip;
	} else {
		match.meta.u32[0] = src_ip_id;
		memcpy(match.outer.ip6.dst_ip, rule->ip6.dst_ip, sizeof(rule->ip6.dst_ip));
	}

	actions.meta.pkt_meta = (1 << 30) | i;
	actions.action_idx = 0;

	if (i == nb_rules - 1 || hairpin_status->entries_in_queue == QUEUE_DEPTH - 1)
		flags = DOCA_FLOW_NO_WAIT;
	else
		flags = DOCA_FLOW_WAIT_FOR_BATCH;

	/* add entry to hairpin pipe*/
	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, flags, hairpin_status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add hairpin pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	hairpin_status->entries_in_queue++;
	if (hairpin_status->entries_in_queue == QUEUE_DEPTH) {
		result = process_entries(port, hairpin_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	}

	return DOCA_SUCCESS;
}

doca_error_t
add_encrypt_entry(struct encrypt_rule *rule, int rule_id, struct ipsec_security_gw_ports_map **ports,
		  struct ipsec_security_gw_config *app_cfg)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	struct doca_flow_pipe *encrypt_pipe;
	struct entries_status hairpin_status;
	struct entries_status encrypt_status;
	struct doca_flow_port *secured_port = NULL;
	struct doca_flow_port *unsecured_port = NULL;
	doca_error_t result;

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_SWITCH) {
		secured_port = doca_flow_port_switch_get(NULL);
		unsecured_port = doca_flow_port_switch_get(NULL);
	} else {
		secured_port = ports[SECURED_IDX]->port;
		unsecured_port = ports[UNSECURED_IDX]->port;
	}

	memset(&hairpin_status, 0, sizeof(hairpin_status));
	memset(&encrypt_status, 0, sizeof(encrypt_status));
	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL &&
		(app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_NONE || app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP)) {
		/* SW encap in tunnel mode */
		if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
			encrypt_pipe = app_cfg->encrypt_pipes.ipv4_encrypt_pipe;
		else
			encrypt_pipe = app_cfg->encrypt_pipes.ipv6_encrypt_pipe;
	} else {
		if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4)
			encrypt_pipe = app_cfg->encrypt_pipes.ipv4_encrypt_pipe;
		else
			encrypt_pipe = app_cfg->encrypt_pipes.ipv6_encrypt_pipe;
	}
	/* add entry to hairpin pipe*/
	result = add_five_tuple_match_entry(unsecured_port, rule, app_cfg->encrypt_pipes, rule_id + 1, rule_id,
					    &hairpin_status, app_cfg->ip6_table);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	/* create ipsec shared object */
	result = create_ipsec_encrypt_shared_object(secured_port, (void *)rule->sa, rule_id);
	if (result != DOCA_SUCCESS)
		return result;

	memset(&match, 0, sizeof(match));

	match.meta.pkt_meta = rule_id;

	actions.action_idx = 0;
	actions.crypto.crypto_id = rule_id;

	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL) {
		create_ipsec_encrypt_shared_object_tunnel(&actions.crypto_encap, rule);
		if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
			actions.action_idx = 0;
		else
			actions.action_idx = 1;
	} else if (app_cfg->mode == IPSEC_SECURITY_GW_TRANSPORT)
		create_ipsec_encrypt_shared_object_transport(&actions.crypto_encap, rule);
	else
		create_ipsec_encrypt_shared_object_transport_over_udp(&actions.crypto_encap, rule);
	actions.crypto_encap.icv_size = rule->sa_attrs.icv_length;
	/* add entry to encrypt pipe*/
	result = doca_flow_pipe_add_entry(0, encrypt_pipe, &match, &actions, NULL, NULL,
					  DOCA_FLOW_NO_WAIT, &encrypt_status, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
		return result;
	}
	encrypt_status.entries_in_queue++;

	/* process the entries in the encryption pipe*/
	do {
		result = process_entries(secured_port, &encrypt_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (encrypt_status.entries_in_queue > 0);

	/* process the entries in the 5 tuple match pipes */
	do {
		result = process_entries(unsecured_port, &hairpin_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (hairpin_status.entries_in_queue > 0);
	return DOCA_SUCCESS;
}

/*
 * Add encryption entries to the encrypt pipes:
 * - 5 tuple rule in the TCP / UDP pipe with specific set meta data value (shared obj ID)
 * - specific meta data match on encryption pipe (shared obj ID) with shared object ID in actions
 *
 * @rules [in]: array of rules to insert for encryption
 * @nb_rules [in]: number of rules
 * @pipes [in]: the relevant pipes to add entries to
 * @ports [in]: array of ports
 * @app_cfg [in]: application configuration struct
 * @ip6_table [in]: IPv6 addresses hash table
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_encrypt_entries(struct encrypt_rule *rules, int nb_rules, struct encrypt_pipes pipes,
		    struct ipsec_security_gw_ports_map **ports, struct ipsec_security_gw_config *app_cfg,
		    struct rte_hash *ip6_table)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	struct doca_flow_pipe *encrypt_pipe;
	struct entries_status hairpin_status;
	struct entries_status encrypt_status;
	enum doca_flow_flags_type flags;
	struct doca_flow_port *secured_port = NULL;
	struct doca_flow_port *unsecured_port = NULL;
	int i;
	doca_error_t result;

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_SWITCH) {
		secured_port = doca_flow_port_switch_get(NULL);
		unsecured_port = doca_flow_port_switch_get(NULL);
	} else {
		secured_port = ports[SECURED_IDX]->port;
		unsecured_port = ports[UNSECURED_IDX]->port;
	}

	memset(&hairpin_status, 0, sizeof(hairpin_status));
	memset(&encrypt_status, 0, sizeof(encrypt_status));
	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	for (i = 0; i < nb_rules; i++) {
		if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL &&
		   (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_NONE || app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP)) {
			/* SW encap in tunnel mode */
			if (rules[i].encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
				encrypt_pipe = pipes.ipv4_encrypt_pipe;
			else
				encrypt_pipe = pipes.ipv6_encrypt_pipe;
		} else {
			if (rules[i].l3_type == DOCA_FLOW_L3_TYPE_IP4)
				encrypt_pipe = pipes.ipv4_encrypt_pipe;
			else
				encrypt_pipe = pipes.ipv6_encrypt_pipe;
		}
		result = ipsec_security_gw_create_ipsec_sa(&rules[i].sa_attrs, app_cfg, &rules[i].sa);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create SA: %s", doca_error_get_descr(result));
			return result;
		}

		/* add entry to hairpin pipe*/
		result = add_five_tuple_match_entry(unsecured_port, &rules[i], pipes, nb_rules, i, &hairpin_status,
						    ip6_table);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}

		/* create ipsec shared object */
		result = create_ipsec_encrypt_shared_object(secured_port, rules[i].sa, i);
		if (result != DOCA_SUCCESS)
			return result;

		memset(&match, 0, sizeof(match));

		match.meta.pkt_meta = i;

		actions.action_idx = 0;
		actions.crypto.crypto_id = i;
		if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL) {
			create_ipsec_encrypt_shared_object_tunnel(&actions.crypto_encap, &rules[i]);
			if (rules[i].encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
				actions.action_idx = 0;
			else
				actions.action_idx = 1;
		} else if (app_cfg->mode == IPSEC_SECURITY_GW_TRANSPORT)
			create_ipsec_encrypt_shared_object_transport(&actions.crypto_encap, &rules[i]);
		else
			create_ipsec_encrypt_shared_object_transport_over_udp(&actions.crypto_encap, &rules[i]);
		actions.crypto_encap.icv_size = rules[i].sa_attrs.icv_length;

		if (i == nb_rules - 1 || encrypt_status.entries_in_queue == QUEUE_DEPTH - 1)
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;
		/* add entry to encrypt pipe*/
		result = doca_flow_pipe_add_entry(0, encrypt_pipe, &match, &actions, NULL, NULL, flags,
						  &encrypt_status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
		encrypt_status.entries_in_queue++;
		if (encrypt_status.entries_in_queue == QUEUE_DEPTH) {
			result = process_entries(secured_port, &encrypt_status, DEFAULT_TIMEOUT_US);
			if (result != DOCA_SUCCESS)
				return result;
		}
	}
	/* process the entries in the encryption pipe*/
	do {
		result = process_entries(secured_port, &encrypt_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (encrypt_status.entries_in_queue > 0);

	/* process the entries in the 5 tuple match pipes */
	do {
		result = process_entries(unsecured_port, &hairpin_status, DEFAULT_TIMEOUT_US);
		if (result != DOCA_SUCCESS)
			return result;
	} while (hairpin_status.entries_in_queue > 0);
	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_insert_encrypt_rules(struct ipsec_security_gw_ports_map *ports[], struct ipsec_security_gw_config *app_cfg, uint16_t hairpin_queue_id,
				      struct doca_flow_pipe **encrypt_root, struct doca_flow_pipe **encrypt_pipe)
{
	uint32_t nb_queues = app_cfg->dpdk_config->port_config.nb_queues;
	uint16_t rss_queues[nb_queues];
	uint32_t rss_flags;
	struct doca_flow_pipe *empty_pipe = NULL;
	struct doca_flow_port *secured_port = NULL;
	struct doca_flow_port *unsecured_port = NULL;
	struct doca_flow_fwd fwd;
	bool is_root;
	doca_error_t result;

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		secured_port = ports[SECURED_IDX]->port;
		unsecured_port = ports[UNSECURED_IDX]->port;
		is_root = true;
	} else {
		secured_port = doca_flow_port_switch_get(NULL);
		unsecured_port = doca_flow_port_switch_get(NULL);
		is_root = false;

		result = create_empty_pipe(&empty_pipe);
		if (result != DOCA_SUCCESS)
			return result;

		result = create_rss_pipe(ports[UNSECURED_IDX]->port, hairpin_queue_id, app_cfg->offload,
					 DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT);
		if (result != DOCA_SUCCESS)
			return result;
	}
	result = create_ipsec_encrypt_dummy_shared_object(app_cfg, secured_port);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_encrypt_pipe(secured_port, ports[SECURED_IDX]->port_id, app_cfg, DOCA_FLOW_L3_TYPE_IP4, &app_cfg->encrypt_pipes.ipv4_encrypt_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_ipsec_encrypt_pipe(secured_port, ports[SECURED_IDX]->port_id, app_cfg, DOCA_FLOW_L3_TYPE_IP6, &app_cfg->encrypt_pipes.ipv6_encrypt_pipe);
	if (result != DOCA_SUCCESS)
		return result;

	result = create_egress_ip_classifier(secured_port, is_root, &app_cfg->encrypt_pipes);
	if (result != DOCA_SUCCESS)
		return result;

	rss_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_TCP;
	create_hairpin_pipe_fwd(app_cfg, ports[UNSECURED_IDX]->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT,
				rss_queues, rss_flags, &fwd);

	result = create_ipsec_hairpin_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_TCP, DOCA_FLOW_L3_TYPE_IP4, &fwd,
					   &app_cfg->encrypt_pipes.ipv4_tcp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create IPv4 TCP hairpin pipe");
		return result;
	}

	rss_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_UDP;
	create_hairpin_pipe_fwd(app_cfg, ports[UNSECURED_IDX]->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT,
				rss_queues, rss_flags, &fwd);

	result = create_ipsec_hairpin_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_UDP, DOCA_FLOW_L3_TYPE_IP4, &fwd,
					   &app_cfg->encrypt_pipes.ipv4_udp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create IPv4 UDP hairpin pipe");
		return result;
	}

	rss_flags = DOCA_FLOW_RSS_IPV6 | DOCA_FLOW_RSS_TCP;
	create_hairpin_pipe_fwd(app_cfg, ports[UNSECURED_IDX]->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT,
				rss_queues, rss_flags, &fwd);

	result = create_ipsec_hairpin_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_TCP, DOCA_FLOW_L3_TYPE_IP6, &fwd,
					   &app_cfg->encrypt_pipes.ipv6_tcp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create IPv6 TCP hairpin pipe");
		return result;
	}

	rss_flags = DOCA_FLOW_RSS_IPV6 | DOCA_FLOW_RSS_UDP;
	create_hairpin_pipe_fwd(app_cfg, ports[UNSECURED_IDX]->port_id, empty_pipe, DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT,
				rss_queues, rss_flags, &fwd);

	result = create_ipsec_hairpin_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_UDP, DOCA_FLOW_L3_TYPE_IP6, &fwd,
					   &app_cfg->encrypt_pipes.ipv6_udp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create IPv6 UDP hairpin pipe");
		return result;
	}

	result = create_ipsec_src_ip6_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_TCP,
					   app_cfg->encrypt_pipes.ipv6_tcp_pipe,
					   &app_cfg->encrypt_pipes.ipv6_src_tcp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create source ip6 TCP hairpin pipe");
		return result;
	}

	result = create_ipsec_src_ip6_pipe(unsecured_port, DOCA_FLOW_L4_TYPE_EXT_UDP,
					   app_cfg->encrypt_pipes.ipv6_udp_pipe,
					   &app_cfg->encrypt_pipes.ipv6_src_udp_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed create source ip6 UDP hairpin pipe");
		return result;
	}

	result = create_control_pipe(unsecured_port, is_root, encrypt_root);
	if (result != DOCA_SUCCESS)
		return result;

	result = add_control_pipe_entries(*encrypt_root, app_cfg->encrypt_pipes);
	if (result != DOCA_SUCCESS)
		return result;

	if (!app_cfg->socket_ctx.socket_conf) {
		result = add_encrypt_entries(app_cfg->app_rules.encrypt_rules, app_cfg->app_rules.nb_encrypted_rules,
					     app_cfg->encrypt_pipes, ports, app_cfg, app_cfg->ip6_table);
		if (result != DOCA_SUCCESS)
			return result;
	}

	*encrypt_pipe = app_cfg->encrypt_pipes.egress_ip_classifier;

	return DOCA_SUCCESS;
}

/*
 * Update mbuf with the new headers and trailer data for tunnel mode
 *
 * @m [in]: the mbuf to update
 * @ctx [in]: the security gateway context
 * @rule_idx [in]: the index of the rule to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
prepare_packet_tunnel(struct rte_mbuf **m, struct ipsec_security_gw_core_ctx *ctx, uint32_t rule_idx)
{
	struct rte_ether_hdr *nh;
	struct rte_esp_tail *esp_tail;
	struct rte_ipv4_hdr *ipv4;
	struct rte_ipv6_hdr *ipv6;
	struct rte_mbuf *last_seg;
	struct encrypt_rule *rule = &ctx->encrypt_rules[rule_idx];
	uint32_t icv_len = rule->sa_attrs.icv_length;
	bool sw_sn_inc = ctx->config->sw_sn_inc_enable;
	void *trailer_pointer;
	uint32_t payload_len, esp_len, encrypted_len, padding_len, trailer_len, padding_offset;
	uint16_t reformat_encap_data_len;

	if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
		reformat_encap_data_len = 50;
	else
		reformat_encap_data_len = 70;

	/* remove trailing zeros */
	remove_trailing_zeros(m);

	/* in tunnel mode need to encrypt everything beside the eth header */
	payload_len = (*m)->pkt_len - sizeof(struct rte_ether_hdr);
	/* extra header space required */
	esp_len = reformat_encap_data_len - sizeof(struct rte_ether_hdr);

	encrypted_len = payload_len + (sizeof(struct rte_esp_tail));
	/* align payload to 16 bytes */
	encrypted_len = RTE_ALIGN_CEIL(encrypted_len, PADDING_ALIGN);

	padding_len = encrypted_len - payload_len;
	/* extra trailer space is required */
	trailer_len = padding_len + icv_len;

	/* append the needed space at the beginning of the packet */
	nh = (struct rte_ether_hdr *)(void *)rte_pktmbuf_prepend(*m, esp_len);
	if (nh == NULL)
		return DOCA_ERROR_NO_MEMORY;

	last_seg = rte_pktmbuf_lastseg(*m);

	/* append tail */
	padding_offset = last_seg->data_len;
	last_seg->data_len += trailer_len;
	(*m)->pkt_len += trailer_len;
	trailer_pointer = rte_pktmbuf_mtod_offset(last_seg, typeof(trailer_pointer), padding_offset);

	/* add the new IP and ESP headers */
	if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4) {
		create_ipv4_tunnel_encap(rule, sw_sn_inc, (void *)nh, &reformat_encap_data_len);
		ipv4 = (void *)(nh + 1);
		ipv4->total_length = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr));
		ipv4->hdr_checksum = 0;
		ipv4->hdr_checksum = rte_ipv4_cksum(ipv4);
	} else {
		create_ipv6_tunnel_encap(rule, sw_sn_inc, (void *)nh, &reformat_encap_data_len);
		ipv6 = (void *)(nh + 1);
		ipv6->payload_len = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr) - sizeof(*ipv6));
	}

	padding_len -= sizeof(struct rte_esp_tail);

	/* add padding */
	memcpy(trailer_pointer, esp_pad_bytes, RTE_MIN(padding_len, sizeof(esp_pad_bytes)));

	esp_tail = (struct rte_esp_tail *)(trailer_pointer + padding_len);
	esp_tail->pad_len = padding_len;
	/* set the next proto according to the original packet */
	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4)
		esp_tail->next_proto = 4; /* ipv4 */
	else
		esp_tail->next_proto = 41; /* ipv6 */

	ctx->encrypt_rules[rule_idx].current_sn++;
	return DOCA_SUCCESS;
}

/*
 * Update mbuf with the new headers and trailer data for transport and udp transport mode
 *
 * @m [in]: the mbuf to update
 * @ctx [in]: the security gateway context
 * @rule_idx [in]: the index of the rule to use
 * @udp_transport [in]: true for UDP transport mode
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
prepare_packet_transport(struct rte_mbuf **m, struct ipsec_security_gw_core_ctx *ctx, uint32_t rule_idx,
			 bool udp_transport)
{
	struct rte_ether_hdr *oh, *nh;
	struct rte_esp_tail *esp_tail;
	struct rte_ipv4_hdr *ipv4;
	struct rte_ipv6_hdr *ipv6;
	struct rte_mbuf *last_seg;
	struct encrypt_rule *rule = &ctx->encrypt_rules[rule_idx];
	uint32_t icv_len = rule->sa_attrs.icv_length;
	void *trailer_pointer;
	uint32_t payload_len, esp_len, encrypted_len, padding_len, trailer_len, padding_offset, l2_l3_len;
	uint16_t reformat_encap_data_len;
	int protocol, next_protocol = 0;
	bool sw_sn_inc = ctx->config->sw_sn_inc_enable;

	if (udp_transport) {
		reformat_encap_data_len = 24;
		protocol = IPPROTO_UDP;
	} else {
		reformat_encap_data_len = 16;
		protocol = IPPROTO_ESP;
	}

	/* remove trailing zeros */
	remove_trailing_zeros(m);

	/* get l2 and l3 headers length */
	oh = rte_pktmbuf_mtod(*m, struct rte_ether_hdr *);

	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(oh + 1);
		l2_l3_len = rte_ipv4_hdr_len(ipv4) + sizeof(struct rte_ether_hdr);
	} else
		l2_l3_len = sizeof(struct rte_ipv6_hdr) + sizeof(struct rte_ether_hdr);

	/* in transport mode need to encrypt everything beside l2 and l3 headers*/
	payload_len = (*m)->pkt_len - l2_l3_len;
	/* extra header space required */
	esp_len = reformat_encap_data_len;

	encrypted_len = payload_len + (sizeof(struct rte_esp_tail));
	/* align payload to 16 bytes */
	encrypted_len = RTE_ALIGN_CEIL(encrypted_len, PADDING_ALIGN);

	padding_len = encrypted_len - payload_len;
	/* extra trailer space is required */
	trailer_len = padding_len + icv_len;

	nh = (struct rte_ether_hdr *)(void *)rte_pktmbuf_prepend(*m, esp_len);
	if (nh == NULL)
		return DOCA_ERROR_NO_MEMORY;

	last_seg = rte_pktmbuf_lastseg(*m);

	/* append tail */
	padding_offset = last_seg->data_len;
	last_seg->data_len += trailer_len;
	(*m)->pkt_len += trailer_len;
	trailer_pointer = rte_pktmbuf_mtod_offset(last_seg, typeof(trailer_pointer), padding_offset);

	/* move l2 and l3 to beginning of packet, and copy ESP header after */
	memmove(nh, oh, l2_l3_len);
	if (udp_transport)
		create_udp_transport_encap(rule, sw_sn_inc, ((void *)nh) + l2_l3_len, &reformat_encap_data_len);
	else
		create_transport_encap(rule, sw_sn_inc, ((void *)nh) + l2_l3_len, &reformat_encap_data_len);

	/* update next protocol to ESP/UDP, total length and checksum */
	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(nh + 1);
		next_protocol = ipv4->next_proto_id;
		ipv4->next_proto_id = protocol;
		ipv4->total_length = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr));
		ipv4->hdr_checksum = 0;
		ipv4->hdr_checksum = rte_ipv4_cksum(ipv4);
	} else if (RTE_ETH_IS_IPV6_HDR((*m)->packet_type)) {
		ipv6 = (void *)(nh + 1);
		next_protocol = ipv6->proto;
		ipv6->proto = protocol;
		ipv6->payload_len = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr) - sizeof(*ipv6));
	}

	padding_len -= sizeof(struct rte_esp_tail);

	/* add padding */
	memcpy(trailer_pointer, esp_pad_bytes, RTE_MIN(padding_len, sizeof(esp_pad_bytes)));

	/* set the next proto according to the original packet */
	esp_tail = (struct rte_esp_tail *)(trailer_pointer + padding_len);
	esp_tail->pad_len = padding_len;
	esp_tail->next_proto = next_protocol;

	ctx->encrypt_rules[rule_idx].current_sn++;
	return DOCA_SUCCESS;
}

void handle_unsecured_packets_received(uint16_t nb_packets,
				       struct rte_mbuf **packets,
				       struct ipsec_security_gw_core_ctx *ctx,
				       uint16_t *nb_processed_packets,
				       struct rte_mbuf **processed_packets,
				       struct rte_mbuf **unprocessed_packets)
{
	uint32_t rule_idx;
	uint32_t meta_mask;
	uint32_t current_packet;
	doca_error_t result;
	int nb_unprocessed_packets = 0;

	*nb_processed_packets = 0;

	meta_mask = (1U << 31) | (1 << 30);
	meta_mask -= 1; /* rule index is set on the 30 LSB */

	for (current_packet = 0; current_packet < nb_packets; current_packet++) {
		if (!rte_flow_dynf_metadata_avail())
			goto add_dropped;

		rule_idx = *RTE_FLOW_DYNF_METADATA(packets[current_packet]);
		rule_idx &= meta_mask;

		if (ctx->config->mode == IPSEC_SECURITY_GW_TRANSPORT)
			result = prepare_packet_transport(&packets[current_packet], ctx, rule_idx, false);
		else if (ctx->config->mode == IPSEC_SECURITY_GW_UDP_TRANSPORT)
			result = prepare_packet_transport(&packets[current_packet], ctx, rule_idx, true);
		else
			result = prepare_packet_tunnel(&packets[current_packet], ctx, rule_idx);

		if (result == DOCA_SUCCESS)
			processed_packets[(*nb_processed_packets)++] = packets[current_packet];
		else
			goto add_dropped;

		continue;

add_dropped:
		unprocessed_packets[nb_unprocessed_packets++] = packets[current_packet];
	}
}
