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

#ifndef FLOW_ENCRYPT_H_
#define FLOW_ENCRYPT_H_

#include <rte_hash.h>

#include "flow_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Add encryption entry to the encrypt pipes:
 * - 5 tuple rule in the TCP / UDP pipe with specific set meta data value (shared obj ID)
 * - specific meta data match on encryption pipe (shared obj ID) with shared object ID in actions
 *
 * @rule [in]: rule to insert for encryption
 * @rule_id [in]: rule id for shared obj ID
 * @ports [in]: array of ports
 * @app_cfg [in]: application configuration struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t add_encrypt_entry(struct encrypt_rule *rule, int rule_id, struct ipsec_security_gw_ports_map **ports,
			struct ipsec_security_gw_config *app_cfg);

/*
 * Create encrypt pipe and entries according to the parsed rules
 *
 * @ports [in]: array of struct ipsec_security_gw_ports_map
 * @app_cfg [in]: application configuration structure
 * @hairpin_queue_id [in]: queue idx to forward the packets to in RSS pipe
 * @encrypt_root [out]: the root pipe for encryption
 * @encrypt_pipe [out]: the encryption pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_insert_encrypt_rules(struct ipsec_security_gw_ports_map *ports[],
					struct ipsec_security_gw_config *app_cfg, uint16_t hairpin_queue_id,
					struct doca_flow_pipe **encrypt_root, struct doca_flow_pipe **encrypt_pipe);

/*
 * Handling the new received packets - print packet source IP and send them to tx queues of second port
 *
 * @nb_packets [in]: size of mbufs array
 * @packets [in]: array of packets
 * @ctx [in]: core context struct
 * @nb_processed_packets [out]: number of processed packets
 * @processed_packets [out]: array of processed packets
 * @unprocessed_packets [out]: array of unprocessed packets
 */
void
handle_unsecured_packets_received(uint16_t nb_packets, struct rte_mbuf **packets, struct ipsec_security_gw_core_ctx *ctx,
				 uint16_t *nb_processed_packets, struct rte_mbuf **processed_packets, struct rte_mbuf **unprocessed_packets);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLOW_ENCRYPT_H_ */
