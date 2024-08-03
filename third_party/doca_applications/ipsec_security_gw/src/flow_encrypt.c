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
 * Create ipsec encrypt pipe changeable meta data match and changeable shared IPSEC encryption object
 *
 * @port [in]: port of the pipe
 * @is_root [in]: true in vnf mode
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_encrypt_pipe(struct doca_flow_port *port, bool is_root, struct doca_flow_pipe **pipe)
{
	int nb_actions = 1;
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_actions actions, *actions_arr[nb_actions];
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&actions, 0, sizeof(actions));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "ENCRYPT_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.is_root = is_root;
	pipe_cfg.attr.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.nb_actions = nb_actions;
	pipe_cfg.port = port;

	match_mask.meta.pkt_meta = 0x0fffffff;
	match.meta.pkt_meta = 0xffffffff;

	actions.security.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	actions.security.crypto_id = 0xffffffff;

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create encrypt pipe: %s", doca_get_error_string(result));
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

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
	match.outer.l4_type_ext = protocol_type;
	SET_IP6_ADDR(match.outer.ip6.src_ip, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);

	actions.meta.u32[0] = UINT32_MAX;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = hairpin_pipe;

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create hairpin pipe: %s", doca_get_error_string(result));
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
		DOCA_LOG_ERR("Failed to create hairpin pipe: %s", doca_get_error_string(result));
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
		DOCA_LOG_ERR("Failed to create control pipe: %s", doca_get_error_string(result));
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

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv4_tcp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add TCP IPv4 control entry: %s", doca_get_error_string(result));
		return result;
	}

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv4_udp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add UDP IPv4 control entry: %s", doca_get_error_string(result));
		return result;
	}

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv6_src_tcp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add TCP IPv6 control entry: %s", doca_get_error_string(result));
		return result;
	}

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = pipes.ipv6_src_udp_pipe;
	result = doca_flow_pipe_control_add_entry(0, 0, control_pipe, &match, NULL, NULL, NULL, NULL, NULL, &fwd, NULL, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add UDP IPv6 control entry: %s", doca_get_error_string(result));
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
create_ipsec_encrypt_shared_object_transport(struct doca_flow_resource_crypto_cfg *crypto_cfg, struct encrypt_rule *rule)
{
	create_transport_encap(rule, false, crypto_cfg->reformat_data, &crypto_cfg->reformat_data_sz);

	crypto_cfg->net_type = DOCA_FLOW_CRYPTO_NET_TRANSPORT;
	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4)
		crypto_cfg->header_type = DOCA_FLOW_CRYPTO_HEADER_IPV4;
	else
		crypto_cfg->header_type = DOCA_FLOW_CRYPTO_HEADER_IPV6;
}

/*
 * Update the crypto config for encrypt transport over UDP mode according to the rule
 *
 * @crypto_cfg [in]: shared object config
 * @rule [in]: encrypt rule
 */
static void
create_ipsec_encrypt_shared_object_transport_over_udp(struct doca_flow_resource_crypto_cfg *crypto_cfg,
						      struct encrypt_rule *rule)
{
	create_udp_transport_encap(rule, false, crypto_cfg->reformat_data, &crypto_cfg->reformat_data_sz);

	crypto_cfg->net_type = DOCA_FLOW_CRYPTO_NET_TRANSPORT;
	if (rule->l3_type == DOCA_FLOW_L3_TYPE_IP4)
		crypto_cfg->header_type = DOCA_FLOW_CRYPTO_HEADER_IPV4_UDP;
	else
		crypto_cfg->header_type = DOCA_FLOW_CRYPTO_HEADER_IPV6_UDP;
}

/*
 * Update the crypto config for encrypt trunnel mode according to the rule
 *
 * @crypto_cfg [in]: shared object config
 * @rule [in]: encrypt rule
 */
static void
create_ipsec_encrypt_shared_object_tunnel(struct doca_flow_resource_crypto_cfg *crypto_cfg, struct encrypt_rule *rule)
{
	if (rule->encap_l3_type == DOCA_FLOW_L3_TYPE_IP4)
		create_ipv4_tunnel_encap(rule, false, crypto_cfg->reformat_data, &crypto_cfg->reformat_data_sz);
	else
		create_ipv6_tunnel_encap(rule, false, crypto_cfg->reformat_data, &crypto_cfg->reformat_data_sz);

	crypto_cfg->net_type = DOCA_FLOW_CRYPTO_NET_TUNNEL;
	crypto_cfg->header_type = DOCA_FLOW_CRYPTO_HEADER_NONE;
}

/*
 * Convert doca_ipsec_icv_length enum to doca_flow_crypto_icv_size
 *
 * @icv_length [in]: doca_ipsec_icv_length value
 * @return: ICV length in type doca_flow_crypto_icv_size
 */
static enum doca_flow_crypto_icv_size
convert_ipsec_icv_to_flow(enum doca_ipsec_icv_length icv_length)
{
	switch (icv_length) {
	case DOCA_IPSEC_ICV_LENGTH_8:
		return  DOCA_FLOW_CRYPTO_ICV_8B;
	case DOCA_IPSEC_ICV_LENGTH_12:
		return  DOCA_FLOW_CRYPTO_ICV_12B;
	case DOCA_IPSEC_ICV_LENGTH_16:
		return  DOCA_FLOW_CRYPTO_ICV_16B;
	default:
		return DOCA_FLOW_CRYPTO_ICV_DEFAULT;
	}
}

/*
 * Config and bind shared IPSEC object for encryption
 *
 * @port [in]: port to bind the shared object to
 * @sa [in]: crypto object handle (IPsec offload object)
 * @ipsec_id [in]: shared object ID
 * @port_id [in]: shared object port ID
 * @rule [in]: encrypt rule
 * @app_cfg [in]: application configuration struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_ipsec_encrypt_shared_object(struct doca_flow_port *port, void *sa, uint32_t ipsec_id, int port_id,
				   struct encrypt_rule *rule, struct ipsec_security_gw_config *app_cfg)
{
	struct doca_flow_shared_resource_cfg cfg;
	struct doca_flow_resource_crypto_cfg crypto_cfg;
	doca_error_t result;

	memset(&crypto_cfg, 0, sizeof(crypto_cfg));
	memset(&cfg, 0, sizeof(cfg));

	cfg.domain = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS;
	crypto_cfg.proto_type = DOCA_FLOW_CRYPTO_PROTOCOL_ESP;
	crypto_cfg.action_type = DOCA_FLOW_CRYPTO_ACTION_ENCRYPT;
	if (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_ENCAP ||
	    app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH) {
		crypto_cfg.reformat_type = DOCA_FLOW_CRYPTO_REFORMAT_ENCAP;
		crypto_cfg.reformat_icv_sz = convert_ipsec_icv_to_flow(rule->sa_attrs.icv_length);
	} else
		crypto_cfg.reformat_type = DOCA_FLOW_CRYPTO_REFORMAT_NONE;
	crypto_cfg.security_ctx = sa;
	crypto_cfg.fwd.type = DOCA_FLOW_FWD_PORT; /* fwd encrypted packets to the wire */
	crypto_cfg.fwd.port_id = port_id;
	if (app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL)
		create_ipsec_encrypt_shared_object_tunnel(&crypto_cfg, rule);
	else if (app_cfg->mode == IPSEC_SECURITY_GW_TRANSPORT)
		create_ipsec_encrypt_shared_object_transport(&crypto_cfg, rule);
	else
		create_ipsec_encrypt_shared_object_transport_over_udp(&crypto_cfg, rule);
	cfg.crypto_cfg = crypto_cfg;

	/* config ipsec object */
	result = doca_flow_shared_resource_cfg(DOCA_FLOW_SHARED_RESOURCE_CRYPTO, ipsec_id, &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to cfg shared ipsec object: %s", doca_get_error_string(result));
		return result;
	}
	/* bind shared ipsec encrypt object to port */
	result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_CRYPTO, &ipsec_id, 1, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to bind shared ipsec object to port: %s", doca_get_error_string(result));
		return result;
	}
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
		DOCA_LOG_ERR("Failed to add hairpin pipe entry: %s", doca_get_error_string(result));
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
		DOCA_LOG_ERR("Failed to add hairpin pipe entry: %s", doca_get_error_string(result));
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

	/* add entry to hairpin pipe*/
	result = add_five_tuple_match_entry(unsecured_port, rule, app_cfg->encrypt_pipes, rule_id + 1, rule_id,
					    &hairpin_status, app_cfg->ip6_table);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_get_error_string(result));
		return result;
	}

	/* create ipsec shared object */
	result = create_ipsec_encrypt_shared_object(secured_port, (void *)rule->sa, rule_id,
						    ports[SECURED_IDX]->port_id, rule, app_cfg);
	if (result != DOCA_SUCCESS)
		return result;

	memset(&match, 0, sizeof(match));

	match.meta.pkt_meta = rule_id;

	actions.action_idx = 0;
	actions.security.crypto_id = rule_id;

	/* add entry to encrypt pipe*/
	result = doca_flow_pipe_add_entry(0, app_cfg->encrypt_pipes.encrypt_pipe, &match, &actions, NULL, NULL,
					  DOCA_FLOW_NO_WAIT, &encrypt_status, &entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_get_error_string(result));
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
		result = ipsec_security_gw_create_ipsec_sa(&rules[i].sa_attrs, app_cfg, &rules[i].sa);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create SA: %s", doca_get_error_string(result));
			return result;
		}

		/* add entry to hairpin pipe*/
		result = add_five_tuple_match_entry(unsecured_port, &rules[i], pipes, nb_rules, i, &hairpin_status,
						    ip6_table);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_get_error_string(result));
			return result;
		}

		/* create ipsec shared object */
		result = create_ipsec_encrypt_shared_object(secured_port, rules[i].sa, i, ports[SECURED_IDX]->port_id, &rules[i],
							    app_cfg);
		if (result != DOCA_SUCCESS)
			return result;

		memset(&match, 0, sizeof(match));

		match.meta.pkt_meta = i;

		actions.action_idx = 0;
		actions.security.crypto_id = i;
		if (i == nb_rules - 1 || encrypt_status.entries_in_queue == QUEUE_DEPTH - 1)
			flags = DOCA_FLOW_NO_WAIT;
		else
			flags = DOCA_FLOW_WAIT_FOR_BATCH;
		/* add entry to encrypt pipe*/
		result = doca_flow_pipe_add_entry(0, pipes.encrypt_pipe, &match, &actions, NULL, NULL, flags,
						  &encrypt_status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add pipe entry: %s", doca_get_error_string(result));
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

	result = create_ipsec_encrypt_pipe(secured_port, is_root, &app_cfg->encrypt_pipes.encrypt_pipe);
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

	*encrypt_pipe = app_cfg->encrypt_pipes.encrypt_pipe;

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

	/* update next protocol to ESP/UDP and total length */
	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(nh + 1);
		next_protocol = ipv4->next_proto_id;
		ipv4->next_proto_id = protocol;
		ipv4->total_length = rte_cpu_to_be_16((*m)->pkt_len - sizeof(struct rte_ether_hdr));
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

/*
 * Validate the sequence number of the packet is legal
 *
 * @ctx [in]: the security gateway context
 * @rule_idx [in]: the rule index
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
check_if_last_sn(struct ipsec_security_gw_core_ctx *ctx, uint32_t rule_idx)
{
	uint32_t current_sn;

	if (ctx->config->sw_sn_inc_enable == false)
		return DOCA_SUCCESS;
	current_sn = ctx->encrypt_rules[rule_idx].current_sn;

	if (current_sn == ctx->config->sn_initial - 1) /* reached end of legal SN */
		return DOCA_ERROR_NOT_PERMITTED;
	return DOCA_SUCCESS;
}

void
handle_unsecured_packets_received(uint16_t nb_packets, struct rte_mbuf **packets, struct ipsec_security_gw_core_ctx *ctx,
				  uint16_t *nb_processed_packets, struct rte_mbuf **processed_packets,
				  struct rte_mbuf **unprocessed_packets)
{
	uint32_t rule_idx;
	uint32_t meta_mask;
	uint32_t current_packet;
	doca_error_t result;
	int nb_unprocessed_packets = 0;

	*nb_processed_packets = 0;

	meta_mask = (1 << 31) | (1 << 30);
	meta_mask -= 1; /* rule index is set on the 30 LSB */

	for (current_packet = 0; current_packet < nb_packets; current_packet++) {
		if (!rte_flow_dynf_metadata_avail())
			goto add_dropped;

		rule_idx = *RTE_FLOW_DYNF_METADATA(packets[current_packet]);
		rule_idx &= meta_mask;

		if (check_if_last_sn(ctx, rule_idx) != DOCA_SUCCESS) {
			DOCA_DLOG_WARN("Reached end of legal SN for rule %d", rule_idx);
			goto add_dropped;
		}

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
