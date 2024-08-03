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

#ifndef FLOW_COMMON_H_
#define FLOW_COMMON_H_

#include <arpa/inet.h>

#include <doca_flow.h>

#include "ipsec_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUEUE_DEPTH (512)	   /* DOCA Flow queue depth */
#define SECURED_IDX (0)		   /* Index for secured network port in ports array */
#define UNSECURED_IDX (1)	   /* Index for unsecured network port in ports array */
#define DEFAULT_TIMEOUT_US (10000) /* default timeout for processing entries */
#define SET_L4_PORT(layer, port, value) \
	do { \
		if (match.layer.l4_type_ext == DOCA_FLOW_L4_TYPE_EXT_TCP) \
			match.layer.tcp.l4_port.port = (value); \
		else if (match.layer.l4_type_ext == DOCA_FLOW_L4_TYPE_EXT_UDP) \
			match.layer.udp.l4_port.port = (value); \
	} while (0) /* Set match l4 port */

#define SET_IP6_ADDR(addr, a, b, c, d) \
	do { \
		addr[0] = a; \
		addr[1] = b; \
		addr[2] = c; \
		addr[3] = d; \
	} while (0)

/* IPsec Security Gateway mapping between dpdk and doca flow port */
struct ipsec_security_gw_ports_map {
	struct doca_flow_port *port;	/* doca flow port pointer */
	int port_id;			/* dpdk port ID */
};

/* user context struct that will be used in entries process callback */
struct entries_status {
	bool failure;	      /* will be set to true if some entry status will not be success */
	int nb_processed;     /* number of entries that was already processed */
	int entries_in_queue; /* number of entries in queue that is waiting to process */
};

/* core context struct */
struct ipsec_security_gw_core_ctx {
	uint16_t queue_id;				/* core queue ID */
	struct ipsec_security_gw_config *config;	/* application configuration struct */
	struct encrypt_rule *encrypt_rules;		/* encryption rules */
	struct decrypt_rule *decrypt_rules;		/* decryption rules */
	int *nb_encrypt_rules;				/* number of encryption rules */
	struct ipsec_security_gw_ports_map **ports;	/* application ports */
	struct antireplay_state *antireplay_states;	/* antireplay state */
};

/*
 * Initalized DOCA Flow library and start DOCA Flow ports
 *
 * @app_cfg [in]: application configuration structure
 * @nb_queues [in]: number of queues
 * @ports [out]: initalized DOCA Flow ports
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ipsec_security_gw_init_doca_flow(const struct ipsec_security_gw_config *app_cfg, int nb_queues,
					     struct ipsec_security_gw_ports_map *ports[]);

/*
 * Destroy DOCA Flow resources
 *
 * @nb_ports [in]: number of ports to destroy
 * @ports [in]: initalized DOCA Flow ports
 */
void doca_flow_cleanup(int nb_ports, struct ipsec_security_gw_ports_map *ports[]);

/*
 * Process the added entries and check the status
 *
 * @port [in]: DOCA Flow port
 * @status [in]: the entries status struct that monitor the entries in this specific port
 * @timeout [in]: timeout for process entries
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t process_entries(struct doca_flow_port *port, struct entries_status *status, int timeout);

/*
 * create root pipe for switch mode that forward the packets based on the port_meta
 *
 * @ports [in]: array of struct ipsec_security_gw_ports_map
 * @encrypt_root [in]: root pipe for encryption pipeline
 * @decrypt_root [in]: root pipe for decryption pipeline
 * @encrypt_pipe [in]: pipe to forward the packets for encryption
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_switch_root_pipes(struct ipsec_security_gw_ports_map *ports[], struct doca_flow_pipe *encrypt_root,
				      struct doca_flow_pipe *decrypt_root, struct doca_flow_pipe *encrypt_pipe);

/*
 * Create empty pipe in order the packets will get to rss pipe
 *
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_empty_pipe(struct doca_flow_pipe **pipe);

/*
 * Create RSS pipe that fwd the packets to hairpin queue
 *
 * @port [in]: port of the pipe
 * @queue_id [in]: hairpin queue ID
 * @offload [in]: ESP offload
 * @direction [in]: DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT / DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_rss_pipe(struct doca_flow_port *port, uint16_t queue_id, enum ipsec_security_gw_esp_offload offload,
			     enum doca_ipsec_direction direction);

/*
 * Create the DOCA Flow forward struct based on the running mode
 *
 * @app_cfg [in]: application configuration struct
 * @port_id [in]: port ID of the pipe
 * @empty_pipe [in]: pipe to forward the packets in switch mode
 * @direction [in]: DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT / DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT
 * @rss_queues [in]: rss queues array to fill in case of sw forward
 * @rss_flags [in]: rss flags
 * @fwd [out]: the created forward struct
 */
void create_hairpin_pipe_fwd(struct ipsec_security_gw_config *app_cfg, int port_id,
			     struct doca_flow_pipe *empty_pipe, enum doca_ipsec_direction direction,
			     uint16_t *rss_queues, uint32_t rss_flags, struct doca_flow_fwd *fwd);

/*
 * Remove trailing zeros from ipv4/ipv6 payload.
 * Trailing zeros are added to ipv4/ipv6 payload so that it's larger than the minimal ethernet frame size.
 *
 * @m [in]: the mbuf to update
 */
void remove_trailing_zeros(struct rte_mbuf **m);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLOW_COMMON_H_ */
