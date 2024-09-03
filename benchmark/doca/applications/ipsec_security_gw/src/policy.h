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

/**
 *
 * DOCA Policy for Security Association (SA) Attributes (IPSec):
 * =============================================================
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                               MSG length (4)                                     |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |            src_port (2)           |                   dst_port (2)               |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  | l3_protocol (1) | l4_protocol (1) | outer_l3_protocol (1) |      direction (1)   |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |  layer_mode (1) |      ESN (1)    |     icv_length (1)    |      key_type (1)    |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                  SPI (4)                                         |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                  salt (4)                                        |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                                                                  |
 *  |                              src_ip_addr (47)                                    |
 *  |                                                                                  |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                                                                  |
 *  |                              dst_ip_addr (47)                                    |
 *  |                                                                                  |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                                                                  |
 *  |                              outer_src_ip (47)                                   |
 *  |                                                                                  |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                                                                  |
 *  |                              outer_dst_ip (47)                                   |
 *  |                                                                                  |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *  |                                                                                  |
 *  |                                enc_key (K)                                       |
 *  |                                                                                  |
 *  +-----------------+-----------------+-----------------------+----------------------+
 *
 * Notes:
 * ======
 *  * All fields are to be represented in Network-Order (Big Endian)
 *  * Each message over UDS transport starts with 4 bytes message length for policy record size
 *  * Valid policy record sizes (not including message length): 224 bytes (K = 16), 240 Bytes (K = 32)
 *
 *
 * Fields Explained:
 * =================
 * src_port			- Inner source port (L4)
 * dst_port			- Inner destination port (L4)
 * l3_protocol			- Inner L3 protocol: {IPPROTO_IPV4 (0x04), IPPROTO_IPV6 (0x06)}
 * l4_protocol			- Inner L4 protocol: {IPPROTO_UDP (0x11), IPPROTO_TCP (0x06)}
 * outer_l3_protocol		- Outer L3 protocol: {IPPROTO_IPV4 (0x04), IPPROTO_IPV6 (0x06)}
 * direction			- Traffic direction {Ingress traffic (0), Egress  traffic (1)}
 * layer_mode			- IPSEC mode: {POLICY_MODE_TRANSPORT (0), POLICY_MODE_TUNNEL (1)}
 * ESN				- Is ESN enabled? {FALSE (0), TRUE (1)}
 * icv_length			- ICV length: {8, 12, 16}
 * key_type			- AES Key type: {128 Bits (0), 256 Bits (1)}
 * SPI				- Security Parameter Index (SPI)
 * salt				- Cryptographic salt
 * src_ip_addr			- Inner IP source address - String format, padded with \0 bytes to max size (INET6_ADDRSTRLEN := 46)
 * dst_ip_addr			- Inner IP destination address - String format, padded with \0 bytes to max size (INET6_ADDRSTRLEN := 46)
 * outer_src_ip			- Outer IP source address - String format, padded with \0 bytes to max size (INET6_ADDRSTRLEN := 46)
 * outer_dst_ip			- Outer IP destination address - String format, padded with \0 bytes to max size (INET6_ADDRSTRLEN := 46)
 * enc_key			- Encryption key - Length (K) matching key_type (16 bytes / 32 bytes)
 */

#ifndef POLICY_H_
#define POLICY_H_

#include <sys/un.h>

#include "flow_encrypt.h"
#include "flow_decrypt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_IP_ADDR_LEN (INET6_ADDRSTRLEN)	/* Maximal IP address size */
#define POLICY_DIR_IN (0)			/* Ingress traffic */
#define POLICY_DIR_OUT (1)			/* Egress  traffic */
#define POLICY_MODE_TRANSPORT (0)		/* Policy transport mode */
#define POLICY_MODE_TUNNEL (1)			/* Policy tunnel mode */
#define POLICY_L3_TYPE_IPV4 (4)			/* Policy L3 type IPV4 */
#define POLICY_L3_TYPE_IPV6 (6)			/* Policy L3 type IPV6 */
#define POLICY_L4_TYPE_UDP (IPPROTO_UDP)	/* Policy L4 type UDP */
#define POLICY_L4_TYPE_TCP (IPPROTO_TCP)	/* Policy L4 type TCP */
#define POLICY_KEY_TYPE_128 (0)			/* Policy key type 128 */
#define POLICY_KEY_TYPE_256 (1)			/* Policy key type 256 */
#define POLICY_RECORD_MIN_SIZE (224)		/* Record size for Key of 16 bytes */
#define POLICY_RECORD_MAX_SIZE (240)		/* Record size for Key of 32 bytes */

/* Policy struct */
struct ipsec_security_gw_ipsec_policy {
	/* Protocols attributes */
	uint16_t src_port;				/* Policy inner source port */
	uint16_t dst_port;				/* Policy inner destination port */
	uint8_t l3_protocol;				/* Policy L3 proto {POLICY_L3_TYPE_IPV4, POLICY_L3_TYPE_IPV6} */
	uint8_t l4_protocol;				/* Policy L4 proto {POLICY_L4_TYPE_UDP, POLICY_L4_TYPE_TCP} */
	uint8_t outer_l3_protocol;			/* Policy outer L3 type {POLICY_L3_TYPE_IPV4, POLICY_L3_TYPE_IPV6} */

	/* Policy attributes */
	uint8_t policy_direction;			/* Policy direction {POLICY_DIR_IN, POLICY_DIR_OUT} */
	uint8_t policy_mode;				/* Policy IPSEC mode {POLICY_MODE_TRANSPORT, POLICY_MODE_TUNNEL} */

	/* Security Association attributes */
	uint8_t esn;					/* Is ESN enabled? */
	uint8_t icv_length;				/* ICV length in bytes {8, 12, 16} */
	uint8_t key_type;				/* AES key type {POLICY_KEY_TYPE_128, POLICY_KEY_TYPE_256} */
	uint32_t spi;					/* Security Parameter Index */
	uint32_t salt;					/* Cryptographic salt */
	uint8_t enc_key_data[MAX_KEY_LEN];		/* Encryption key (binary) */

	/* Policy inner and outer addresses */
	char src_ip_addr[MAX_IP_ADDR_LEN + 1];		/* Policy inner IP source address in string format */
	char dst_ip_addr[MAX_IP_ADDR_LEN + 1];		/* Policy inner IP destination address in string format */
	char outer_src_ip[MAX_IP_ADDR_LEN + 1];		/* Policy outer IP source address in string format */
	char outer_dst_ip[MAX_IP_ADDR_LEN + 1];		/* Policy outer IP destination address in string format */
};

/*
 * Print policy attributes
 *
 * @policy [in]: application IPSEC policy
 */
void print_policy_attrs(struct ipsec_security_gw_ipsec_policy *policy);

/*
 * Handle encrypt policy, function logic includes:
 * - parsing the new policy and create encrypt rule structure
 * - create suitable security association
 * - add DOCA flow entry which describes the encrypt rule
 *
 * @app_cfg [in]: application configuration structure
 * @ports [in]: DOCA flow ports array
 * @policy [in]: new policy
 * @rule [out]: encrypt rule structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_handle_encrypt_policy(struct ipsec_security_gw_config *app_cfg,
	struct ipsec_security_gw_ports_map *ports[], struct ipsec_security_gw_ipsec_policy *policy, struct encrypt_rule *rule);

/*
 * Handle decrypt policy, function logic includes:
 * - parsing the new policy and create decrypt rule structure
 * - create suitable security association
 * - add DOCA flow entry which describes the decrypt rule
 *
 * @app_cfg [in]: application configuration structure
 * @secured_port [in]: DOCA flow port for secured port
 * @policy [in]: new policy
 * @rule [out]: encrypt rule structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_handle_decrypt_policy(struct ipsec_security_gw_config *app_cfg, struct doca_flow_port *secured_port,
	struct ipsec_security_gw_ipsec_policy *policy, struct decrypt_rule *rule);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* POLICY_H_ */
