/*
 * Copyright (c) 2025 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef IP_FRAG_PACKET_PARSER_H_
#define IP_FRAG_PACKET_PARSER_H_

#include <doca_error.h>

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

enum ip_frag_pkt_dir {
	IP_FRAG_PKT_DIR_0,
	IP_FRAG_PKT_DIR_1,
	IP_FRAG_PKT_DIR_UNKNOWN,
	IP_FRAG_PKT_DIR_NUM,
};

struct ip_frag_link_parser_ctx {
	size_t len;		   /* Total length of headers parsed */
	uint16_t next_proto;	   /* Next protocol */
	struct rte_ether_hdr *eth; /* Ethernet header */
};

struct ip_frag_network_parser_ctx {
	size_t len;		       /* Total length of headers parsed */
	uint8_t next_proto;	       /* Next protocol */
	bool frag;		       /* Flag indicating fragmented packet */
	struct rte_ipv4_hdr *ipv4_hdr; /* IPv4 header */
};

struct ip_frag_transport_parser_ctx {
	uint8_t proto; /* Protocol ID */
	size_t len;    /* Total length of headers parsed */
	union {
		struct rte_udp_hdr *udp_hdr; /* UDP header, when proto==UDP */
		struct rte_tcp_hdr *tcp_hdr; /* TCP header, when proto==TCP */
	};
};

struct ip_frag_gtp_parser_ctx {
	size_t len;			       /* Total length of headers parsed */
	struct rte_gtp_hdr *gtp_hdr;	       /* GTP protocol header */
	struct rte_gtp_hdr_ext_word *opt_hdr;  /* GTP option header */
	struct rte_gtp_psc_type0_hdr *ext_hdr; /* GTP extension header */
};

struct ip_frag_conn_parser_ctx {
	size_t len;					   /* Total length of headers parsed */
	struct ip_frag_link_parser_ctx link_ctx;	   /* Link-layer parser context */
	struct ip_frag_network_parser_ctx network_ctx;	   /* Network-layer parser context */
	struct ip_frag_transport_parser_ctx transport_ctx; /* Transport-layer parser context */
};

struct ip_frag_tun_parser_ctx {
	size_t len;					   /* Total length of headers parsed */
	struct ip_frag_link_parser_ctx link_ctx;	   /* Link-layer parser context */
	struct ip_frag_network_parser_ctx network_ctx;	   /* Network-layer parser context */
	struct ip_frag_transport_parser_ctx transport_ctx; /* Transport-layer parser context */
	struct ip_frag_gtp_parser_ctx gtp_ctx;		   /* GTP tunnel parser context */
	struct ip_frag_conn_parser_ctx inner;		   /* Tunnel-encapsulated connection parser context */
};

/*
 * Parse link-layer protocol headers
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_link_parse(uint8_t *data, uint8_t *data_end, struct ip_frag_link_parser_ctx *ctx);

/*
 * Parse network-layer protocol headers
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @expected_proto [in]: expected network-layer protocol as indicated by upper layer
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_network_parse(uint8_t *data,
				   uint8_t *data_end,
				   uint16_t expected_proto,
				   struct ip_frag_network_parser_ctx *ctx);

/*
 * Parse transport-layer protocol headers
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @proto [in]: transport-layer protocol as indicated by upper layer
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_transport_parse(uint8_t *data,
				     uint8_t *data_end,
				     uint8_t proto,
				     struct ip_frag_transport_parser_ctx *ctx);

/*
 * Parse GPTU tunnel headers
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_gtpu_parse(uint8_t *data, uint8_t *data_end, struct ip_frag_gtp_parser_ctx *ctx);

/*
 * Parse the payload headers that identify the connection 5T
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_conn_parse(uint8_t *data, uint8_t *data_end, struct ip_frag_conn_parser_ctx *ctx);

/*
 * Parse a packet incoming from the WAN side
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_wan_parse(uint8_t *data, uint8_t *data_end, struct ip_frag_conn_parser_ctx *ctx);

/*
 * Parse a packet incoming from the RAN side
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_ran_parse(uint8_t *data, uint8_t *data_end, struct ip_frag_tun_parser_ctx *ctx);

/*
 * Parse a packet and establish its direction
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @ctx [out]: pointer to the parser context
 * @dir [out]: pointer to the incoming packet direction
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t ip_frag_dir_parse(uint8_t *data,
			       uint8_t *data_end,
			       struct ip_frag_tun_parser_ctx *ctx,
			       enum ip_frag_pkt_dir *dir);

#endif /* IP_FRAG_PACKET_PARSER_H_ */
