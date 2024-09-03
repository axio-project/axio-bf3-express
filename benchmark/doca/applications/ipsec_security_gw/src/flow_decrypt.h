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

#ifndef FLOW_DECRYPT_H_
#define FLOW_DECRYPT_H_

#include "flow_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Add decryption entry to the decrypt pipe
 *
 * @rule [in]: rule to insert for decryption
 * @rule_id [in]: rule id for crypto shared index
 * @port [in]: port of the entries
 * @app_cfg [in]: application configuration struct
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t add_decrypt_entry(struct decrypt_rule *rule, int rule_id, struct doca_flow_port *port,
	struct ipsec_security_gw_config *app_cfg);

/* struct to hold antireplay state */
struct antireplay_state {
	uint32_t window_size;		/* antireplay window size */
	uint32_t end_win_sn;		/* end of window sequence number */
	uint64_t bitmap;		/* antireplay bitmap - LSB is with lowest sequence number */
};

/*
 * Create decrypt pipe and entries according to the parsed rules
 *
 * @port [in]: secured network port pointer
 * @app_cfg [in]: application configuration structure
 * @hairpin_queue_id [in]: queue idx to forward the packets to in RSS pipe
 * @decrypt_root [out]: the root pipe for decryption
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_insert_decrypt_rules(struct ipsec_security_gw_ports_map *port, struct ipsec_security_gw_config *app_cfg,
						    uint16_t hairpin_queue_id, struct doca_flow_pipe **decrypt_root);

/*
 * Handling the new received packets - decap packet and send them to tx queues of second port
 *
 * @nb_packets [in]: size of mbufs array
 * @packets [in]: array of packets
 * @ctx [in]: core context struct
 * @nb_processed_packets [out]: number of processed packets
 * @processed_packets [out]: array of processed packets
 * @unprocessed_packets [out]: array of unprocessed packets
 */
void handle_secured_packets_received(uint16_t nb_packets, struct rte_mbuf **packets, struct ipsec_security_gw_core_ctx *ctx,
						uint16_t *nb_processed_packets, struct rte_mbuf **processed_packets, struct rte_mbuf **unprocessed_packets);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLOW_DECRYPT_H_ */
