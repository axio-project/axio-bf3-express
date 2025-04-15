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

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_gtp.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <doca_bitfield.h>
#include <doca_flow_net.h>
#include <doca_log.h>

#include "upf_accel.h"
#include "upf_accel_flow_processing.h"
#include "upf_accel_packet_parser.h"

#define UPF_ACCEL_MAX_PKT_BURST 32
/* Maximum DOCA Flow entries to age in an aging function call */
#define UPF_ACCEL_MAX_NUM_AGING (UPF_ACCEL_MAX_PKT_BURST * 2)
/* Maximum timeout for DOCA Flow handling and processing functions. 0 for no limit */
#define UPF_ACCEL_DOCA_FLOW_MAX_TIMEOUT_US (0)

struct upf_accel_fp_burst_ctx {
	struct rte_mbuf *rx_pkts[UPF_ACCEL_MAX_PKT_BURST];	     /* Rx packet burst */
	struct rte_mbuf *tx_pkts[UPF_ACCEL_MAX_PKT_BURST];	     /* Tx packet burst */
	struct upf_accel_match_8t *matches[UPF_ACCEL_MAX_PKT_BURST]; /* Packet n-tuple matches */
	struct upf_accel_entry_ctx *conns[UPF_ACCEL_MAX_PKT_BURST];  /* Connections matched to packet n-tuples */
	struct upf_accel_tun_parser_ctx parse_ctxs[UPF_ACCEL_MAX_PKT_BURST]; /* Packet n-tuple parser contexts */
	enum upf_accel_packet_direction pkts_dir[UPF_ACCEL_MAX_PKT_BURST];   /* Packet direction (ran (uplink) OR wan
									  (downlink)) */
	uint16_t rx_pkts_cnt;						     /* Rx packet burst count */
	uint16_t tx_pkts_cnt;						     /* Tx packet burst count */
	bool pkts_drop[UPF_ACCEL_MAX_PKT_BURST];			     /* Packet processing drop indicator */
} __rte_aligned(RTE_CACHE_LINE_SIZE);

static_assert(UPF_ACCEL_MAX_PKT_BURST <= RTE_HASH_LOOKUP_BULK_MAX,
	      "Can't process a burst larger than RTE bulk size limit");

DOCA_LOG_REGISTER(UPF_ACCEL::FLOW_PROCESSING);

/*
 * Get the opposite packet direction
 *
 * @pkt_dir [in]: packet direction
 * @return: the opposite direction of packet
 */
static enum upf_accel_packet_direction upf_accel_fp_get_opposite_pkt_dir(enum upf_accel_packet_direction pkt_dir)
{
	return (pkt_dir == PKT_DIR_RAN) ? PKT_DIR_WAN : PKT_DIR_RAN;
}

/*
 * Delete flow
 *
 * Assign its status to NONE, and delete its context from the ht if the flow
 * in the second direction is also NONE (or in error status)
 *
 * @fp_data [in]: flow processing data
 * @dyn_ctx [in]: dynamic entry context
 * @pkt_dir [in]: packet direction
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_fp_delete_flow(struct upf_accel_fp_data *fp_data,
					     struct upf_accel_dyn_entry_ctx *dyn_ctx,
					     enum upf_accel_packet_direction pkt_dir)
{
	enum upf_accel_packet_direction opposite_dir = upf_accel_fp_get_opposite_pkt_dir(pkt_dir);
	bool last_entry = dyn_ctx->flow_status[opposite_dir] == UPF_ACCEL_FLOW_STATUS_NONE ||
			  (dyn_ctx->flow_status[opposite_dir] == UPF_ACCEL_FLOW_STATUS_ACCELERATED &&
			   dyn_ctx->entries[opposite_dir].status == DOCA_FLOW_ENTRY_STATUS_ERROR);

	dyn_ctx->flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_NONE;

	if (!last_entry)
		return DOCA_SUCCESS;

	if (rte_hash_del_key_with_hash(fp_data->dyn_tbl, &dyn_ctx->match.inner, dyn_ctx->hash) < 0) {
		DOCA_LOG_WARN("Failed entry aging - hash free failed");
		return DOCA_ERROR_INVALID_VALUE;
	}

	DOCA_LOG_DBG("Entry removed from hash table");
	return DOCA_SUCCESS;
}

/*
 * Dynamic entry processing handler
 *
 * @dyn_ctx [in]: dynamic entry context
 * @entry [in]: DOCA Flow entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 */
static void upf_accel_dyn_entry_cb(struct upf_accel_dyn_entry_ctx *dyn_ctx,
				   struct doca_flow_pipe_entry *entry,
				   uint16_t pipe_queue,
				   enum doca_flow_entry_status status,
				   enum doca_flow_entry_op op)
{
	struct upf_accel_fp_accel_counters *accel_counters;
	struct upf_accel_fp_data *fp_data = dyn_ctx->fp_data;
	enum doca_flow_entry_status *entry_status;
	enum upf_accel_packet_direction pkt_dir;
	doca_error_t ret;

	if (entry == dyn_ctx->entries[PKT_DIR_RAN].entry) {
		pkt_dir = PKT_DIR_RAN;
	} else if (entry == dyn_ctx->entries[PKT_DIR_WAN].entry) {
		pkt_dir = PKT_DIR_WAN;
	} else {
		DOCA_LOG_ERR("Unexpected entry");
		return;
	}
	accel_counters = &fp_data->accel_counters[pkt_dir];
	entry_status = &dyn_ctx->entries[pkt_dir].status;

	switch (op) {
	case DOCA_FLOW_ENTRY_OP_AGED:
		if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
			DOCA_LOG_ERR("Aging status failed");
			accel_counters->aging_errors++;
		}

		ret = doca_flow_pipe_remove_entry(pipe_queue, DOCA_FLOW_WAIT_FOR_BATCH, entry);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Aging remove entry failed");
			accel_counters->aging_errors++;
		} else {
			DOCA_LOG_DBG("Entry aged out of pipe");
		}

		break;
	case DOCA_FLOW_ENTRY_OP_ADD:
		if (status == DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
			accel_counters->current++;
			accel_counters->total++;
		} else {
			DOCA_LOG_ERR("Failed to process dynamic entry add");
			accel_counters->errors++;
		}
		*entry_status = status;
		break;
	case DOCA_FLOW_ENTRY_OP_DEL:
		if (status == DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
			ret = upf_accel_fp_delete_flow(fp_data, dyn_ctx, pkt_dir);
			if (ret != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to delete accelerated flow");
				accel_counters->aging_errors++;
			}

			accel_counters->current--;
		} else {
			DOCA_LOG_ERR("Failed to process dynamic entry del");
			accel_counters->aging_errors++;
		}
		break;
	default:
		DOCA_LOG_WARN("UPF accel dynamic entry cb - bad op %u", op);
		return;
	}
}

/*
 * Static entry processing handler
 *
 * @static_ctx [in]: static entry context
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 */
static void upf_accel_static_entry_cb(struct upf_accel_static_entry_ctx *static_ctx,
				      enum doca_flow_entry_status status,
				      enum doca_flow_entry_op op)
{
	struct entries_status *entries_status = &static_ctx->ctrl_status;

	switch (op) {
	case DOCA_FLOW_ENTRY_OP_ADD:
	case DOCA_FLOW_ENTRY_OP_DEL:
		if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
			entries_status->failure = true; /* set failure to true if processing failed */
		entries_status->nb_processed++;
		break;
	default:
		DOCA_LOG_ERR("UPF accel static entry cb - bad op %u", op);
		return;
	}
}

void upf_accel_check_for_valid_entry_aging(struct doca_flow_pipe_entry *entry,
					   uint16_t pipe_queue,
					   enum doca_flow_entry_status status,
					   enum doca_flow_entry_op op,
					   void *user_ctx)
{
	struct upf_accel_entry_ctx *entry_ctx = (struct upf_accel_entry_ctx *)user_ctx;

	if (entry_ctx == NULL)
		return;

	if (likely(entry_ctx->type == UPF_ACCEL_RULE_DYNAMIC))
		upf_accel_dyn_entry_cb(&entry_ctx->dyn_ctx, entry, pipe_queue, status, op);
	else
		upf_accel_static_entry_cb(&entry_ctx->static_ctx, status, op);
}

void upf_accel_sw_aging_ll_init(struct upf_accel_fp_data *fp_data, enum upf_accel_packet_direction pkt_dir)
{
	struct upf_accel_sw_aging_ll *ll = &fp_data->sw_aging_ll[pkt_dir];

	ll->head = UPF_ACCEL_SW_AGING_LL_INVALID_NODE;
	ll->tail = UPF_ACCEL_SW_AGING_LL_INVALID_NODE;
}

/*
 * Init a SW Aging doubly linked list node
 *
 * @conn [in]: connection descriptor
 * @pkt_dir [in]: packet direction
 */
static void upf_accel_sw_aging_ll_node_init(struct upf_accel_entry_ctx *conn, enum upf_accel_packet_direction pkt_dir)
{
	struct upf_accel_sw_aging_ll_node *node = &conn->dyn_ctx.entries[pkt_dir].sw_aging_node;

	node->prev = UPF_ACCEL_SW_AGING_LL_INVALID_NODE;
	node->next = UPF_ACCEL_SW_AGING_LL_INVALID_NODE;
}

/*
 * Check if a SW aging linked list node is valid
 *
 * @node_idx [in]: Node index
 * @return: true if is valid, false otherwise
 */
static bool upf_accel_sw_aging_ll_node_is_valid(int32_t node_idx)
{
	return (node_idx != UPF_ACCEL_SW_AGING_LL_INVALID_NODE);
}

/*
 * Check if a flow exists as a node in the SW Aging linked list
 *
 * @fp_data [in]: flow processing data
 * @conn [in]: connection descriptor
 * @pkt_dir [in]: packet direction
 * @return: true if the node exist, false otherwise
 */
static bool upf_accel_sw_aging_ll_node_exist(struct upf_accel_fp_data *fp_data,
					     struct upf_accel_entry_ctx *conn,
					     enum upf_accel_packet_direction pkt_dir)
{
	struct upf_accel_sw_aging_ll_node *node = &conn->dyn_ctx.entries[pkt_dir].sw_aging_node;
	int32_t conn_idx = conn->dyn_ctx.conn_idx;

	return upf_accel_sw_aging_ll_node_is_valid(node->prev) || upf_accel_sw_aging_ll_node_is_valid(node->next) ||
	       conn_idx == fp_data->sw_aging_ll[pkt_dir].head || conn_idx == fp_data->sw_aging_ll[pkt_dir].tail;
}

/*
 * Remove node from the SW Aging linked list
 *
 * @fp_data [in]: flow processing data
 * @conn [in]: connection descriptor
 * @pkt_dir [in]: packet direction
 * @return: true if the node existed, false otherwise
 */
static bool upf_accel_sw_aging_ll_node_remove(struct upf_accel_fp_data *fp_data,
					      struct upf_accel_entry_ctx *conn,
					      enum upf_accel_packet_direction pkt_dir)
{
	int32_t prev = conn->dyn_ctx.entries[pkt_dir].sw_aging_node.prev;
	int32_t next = conn->dyn_ctx.entries[pkt_dir].sw_aging_node.next;
	int32_t conn_idx = conn->dyn_ctx.conn_idx;
	struct upf_accel_entry_ctx *tmp_conn;

	if (!upf_accel_sw_aging_ll_node_exist(fp_data, conn, pkt_dir))
		return false;

	if (upf_accel_sw_aging_ll_node_is_valid(prev)) {
		tmp_conn = &fp_data->dyn_tbl_data[prev];
		tmp_conn->dyn_ctx.entries[pkt_dir].sw_aging_node.next = next;
	}
	if (upf_accel_sw_aging_ll_node_is_valid(next)) {
		tmp_conn = &fp_data->dyn_tbl_data[next];
		tmp_conn->dyn_ctx.entries[pkt_dir].sw_aging_node.prev = prev;
	}
	if (fp_data->sw_aging_ll[pkt_dir].tail == conn_idx)
		fp_data->sw_aging_ll[pkt_dir].tail = prev;
	if (fp_data->sw_aging_ll[pkt_dir].head == conn_idx)
		fp_data->sw_aging_ll[pkt_dir].head = next;

	fp_data->unaccel_counters[pkt_dir].current--;
	return true;
}

/*
 * Insert node to the head of the SW Aging linked list and update its timestamp
 *
 * @fp_data [in]: flow processing data
 * @conn [in]: connection descriptor
 * @pkt_dir [in]: packet direction
 */
static void upf_accel_sw_aging_ll_node_insert(struct upf_accel_fp_data *fp_data,
					      struct upf_accel_entry_ctx *conn,
					      enum upf_accel_packet_direction pkt_dir)
{
	struct upf_accel_sw_aging_ll_node *node = &conn->dyn_ctx.entries[pkt_dir].sw_aging_node;
	int32_t old_head = fp_data->sw_aging_ll[pkt_dir].head;
	int32_t conn_idx = conn->dyn_ctx.conn_idx;
	struct upf_accel_entry_ctx *tmp_conn;

	if (upf_accel_sw_aging_ll_node_is_valid(old_head)) {
		tmp_conn = &fp_data->dyn_tbl_data[old_head];
		tmp_conn->dyn_ctx.entries[pkt_dir].sw_aging_node.prev = conn_idx;
	}

	node->prev = UPF_ACCEL_SW_AGING_LL_INVALID_NODE;
	node->next = old_head;
	node->timestamp = rte_rdtsc();

	fp_data->sw_aging_ll[pkt_dir].head = conn_idx;
	if (!upf_accel_sw_aging_ll_node_is_valid(fp_data->sw_aging_ll[pkt_dir].tail))
		fp_data->sw_aging_ll[pkt_dir].tail = conn_idx;

	fp_data->unaccel_counters[pkt_dir].current++;
}

/*
 * Move node to the head of the SW Aging linked list
 *
 * First, remove it from its current position (if exist)
 * and then insert it to the head.
 * If the node was not yet in the list, the 'total' counter is increased.
 *
 * @fp_data [in]: flow processing data
 * @conn [in]: connection descriptor
 * @pkt_dir [in]: packet direction
 */
static void upf_accel_sw_aging_ll_node_move_to_head(struct upf_accel_fp_data *fp_data,
						    struct upf_accel_entry_ctx *conn,
						    enum upf_accel_packet_direction pkt_dir)
{
	if (!upf_accel_sw_aging_ll_node_remove(fp_data, conn, pkt_dir))
		fp_data->unaccel_counters[pkt_dir].total++;

	upf_accel_sw_aging_ll_node_insert(fp_data, conn, pkt_dir);
}

/*
 * Scans all the flows in the list, starting from the oldest (tail), and ages out
 * all those that have exceeded the aging period until it encounters a flow that
 * has not yet aged.
 *
 * @fp_data [in]: flow processing data
 * @pkt_dir [in]: packet direction
 */
static void upf_accel_sw_aging_ll_scan(struct upf_accel_fp_data *fp_data, enum upf_accel_packet_direction pkt_dir)
{
	uint64_t aging_period_tsc = fp_data->ctx->upf_accel_cfg->sw_aging_time_sec * rte_get_tsc_hz();
	int32_t curr = fp_data->sw_aging_ll[pkt_dir].tail;
	struct upf_accel_sw_aging_ll_node *node;
	uint64_t tsc = rte_rdtsc();
	struct upf_accel_entry_ctx *conn;
	doca_error_t ret;

	while (upf_accel_sw_aging_ll_node_is_valid(curr)) {
		conn = &fp_data->dyn_tbl_data[curr];
		node = &conn->dyn_ctx.entries[pkt_dir].sw_aging_node;

		if (tsc - node->timestamp < aging_period_tsc)
			break;

		ret = upf_accel_fp_delete_flow(fp_data, &conn->dyn_ctx, pkt_dir);
		if (ret != DOCA_SUCCESS) {
			fp_data->unaccel_counters[pkt_dir].aging_errors++;
			DOCA_LOG_ERR("Failed to delete unaccelerated flow");
		}

		fp_data->unaccel_counters[pkt_dir].current--;
		curr = node->prev;
	}

	fp_data->sw_aging_ll[pkt_dir].tail = curr;
}

/*
 * Indicate a flow exists
 *
 * @flow_status [in]: Status of the connection
 * @return: true if the connection was initialized, false otherwise
 */
static inline bool upf_accel_flow_is_alive(enum upf_accel_flow_status flow_status)
{
	return flow_status != UPF_ACCEL_FLOW_STATUS_NONE;
}

/*
 * Returns a mask with `mask` number of set MSBs
 *
 * @mask [in]: number of MSbits to set.
 * @return: netmask
 */
static inline uint32_t ipv4_netmask_get(uint8_t mask)
{
	return ~((1ul << (32 - mask)) - 1);
}

/*
 * Check if masked IPV4 address is matching
 *
 * @masked [in]: struct of a masked IPV4 address.
 * @ipv4 [in]: ipv4 address to match
 * @return: true if the address matches, otherwise false.
 */
static inline bool ipv4_masked_is_matching(const struct upf_accel_ip_addr *masked, uint32_t ipv4)
{
	return masked->v4 == (ipv4 & ipv4_netmask_get(masked->netmask));
}

/*
 * Parse 5 tuple data from a raw packet, without ethernet header.
 *
 * @parse_ctx [in]: pointer to the parser context
 * @src_ip [out]: pointer to store the source IP
 * @dst_ip [out]: pointer to store the destination IP
 * @src_port [out]: pointer to store the source port
 * @dst_port [out]: pointer to store the destination port
 * @ip_proto [out]: pointer to store the IP protocol
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_5t_match(struct upf_accel_conn_parser_ctx *parse_ctx,
				       uint32_t *src_ip,
				       uint32_t *dst_ip,
				       uint16_t *src_port,
				       uint16_t *dst_port,
				       uint8_t *ip_proto)
{
	switch (parse_ctx->transport_ctx.proto) {
	case DOCA_FLOW_PROTO_TCP: {
		*src_port = rte_be_to_cpu_16(parse_ctx->transport_ctx.tcp_hdr->src_port);
		*dst_port = rte_be_to_cpu_16(parse_ctx->transport_ctx.tcp_hdr->dst_port);
		break;
	}
	case DOCA_FLOW_PROTO_UDP: {
		*src_port = rte_be_to_cpu_16(parse_ctx->transport_ctx.udp_hdr->src_port);
		*dst_port = rte_be_to_cpu_16(parse_ctx->transport_ctx.udp_hdr->dst_port);
		break;
	}
	default:
		DOCA_LOG_WARN("Unsupported L4 %d", parse_ctx->transport_ctx.proto);
		return DOCA_ERROR_NOT_SUPPORTED;
	}

	*src_ip = rte_be_to_cpu_32(parse_ctx->network_ctx.ipv4_hdr->src_addr);
	*dst_ip = rte_be_to_cpu_32(parse_ctx->network_ctx.ipv4_hdr->dst_addr);
	*ip_proto = parse_ctx->transport_ctx.proto;

	return DOCA_SUCCESS;
}

/*
 * Create a GTPU match from a raw packet
 *
 * @tun_parse_ctx [in]: pointer to the parser context
 * @match [out]: pointer to store the result at
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_gtpu_match(struct upf_accel_tun_parser_ctx *tun_parse_ctx,
					 struct upf_accel_match_8t *match)
{
	doca_error_t ret;

	if (tun_parse_ctx->link_ctx.next_proto != RTE_ETHER_TYPE_IPV4 ||
	    tun_parse_ctx->network_ctx.next_proto != DOCA_FLOW_PROTO_UDP ||
	    tun_parse_ctx->transport_ctx.proto != DOCA_FLOW_PROTO_UDP ||
	    rte_be_to_cpu_16(tun_parse_ctx->transport_ctx.udp_hdr->dst_port) != DOCA_FLOW_GTPU_DEFAULT_PORT) {
		DOCA_LOG_DBG("Only support GTPU encapsulation with UDP over IPv4 and GTPU-reserved destination port");
		return DOCA_ERROR_INVALID_VALUE;
	}

	ret = upf_accel_5t_match(&tun_parse_ctx->inner,
				 &match->inner.ue_ip,
				 &match->inner.extern_ip,
				 &match->inner.ue_port,
				 &match->inner.extern_port,
				 &match->inner.ip_proto);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed parsing GTPU PDU");
		return ret;
	}

	match->outer.te_ip = rte_be_to_cpu_32(tun_parse_ctx->network_ctx.ipv4_hdr->src_addr);
	match->outer.te_id = rte_be_to_cpu_32(tun_parse_ctx->gtp_ctx.gtp_hdr->teid);
	if (tun_parse_ctx->gtp_ctx.ext_hdr)
		match->outer.qfi = tun_parse_ctx->gtp_ctx.ext_hdr->qfi;
	return DOCA_SUCCESS;
}

/*
 * Create a match from a raw packet, coming from the RAN side.
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @parse_ctx [out]: pointer to the parser context
 * @match [out]: pointer to store the result at
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_ran_match(uint8_t *data,
					uint8_t *data_end,
					struct upf_accel_tun_parser_ctx *parse_ctx,
					struct upf_accel_match_8t *match)
{
	doca_error_t ret;

	ret = upf_accel_ran_parse(data, data_end, parse_ctx);
	if (ret != DOCA_SUCCESS)
		return ret;

	return upf_accel_gtpu_match(parse_ctx, match);
}

/*
 * Create a match from a raw packet, coming from the WAN side.
 *
 * @data [in]: pointer to the start of the data
 * @data_end [in]: pointer to the end of the data
 * @parse_ctx [out]: pointer to the parser context
 * @match [out]: pointer to store the result at
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_wan_match(uint8_t *data,
					uint8_t *data_end,
					struct upf_accel_conn_parser_ctx *parse_ctx,
					struct upf_accel_match_5t *match)
{
	doca_error_t ret;

	ret = upf_accel_wan_parse(data, data_end, parse_ctx);
	if (ret != DOCA_SUCCESS)
		return ret;

	return upf_accel_5t_match(parse_ctx,
				  &match->extern_ip,
				  &match->ue_ip,
				  &match->extern_port,
				  &match->ue_port,
				  &match->ip_proto);
}

/*
 * Check if a given tunnel header matches the PDR properties.
 *
 * @pdr [in]: the PDR describing the match criteria
 * @match [in]: header to check
 * @return: true if the header matches, otherwise false.
 */
static bool upf_accel_pdr_tunnel_is_matching(const struct upf_accel_pdr *pdr, const struct upf_accel_match_tun *match)
{
	if (match->te_id < pdr->pdi_local_teid_start || match->te_id > pdr->pdi_local_teid_end)
		return false;

	if ((match->qfi || pdr->pdi_qfi) && pdr->pdi_qfi != match->qfi)
		return false;

	return ipv4_masked_is_matching(&pdr->pdi_local_teid_ip, match->te_ip);
}

/*
 * Check if a given 5 tuple header matches the PDR properties.
 *
 * @pdr [in]: the PDR describing the match criteria
 * @match [in]: header to check
 * @return: true if the header matches, otherwise false.
 */
static bool upf_accel_pdr_tuple_is_matching(const struct upf_accel_pdr *pdr, const struct upf_accel_match_5t *match)
{
	if (pdr->pdi_sdf_proto && match->ip_proto != pdr->pdi_sdf_proto)
		return false;

	if (match->ue_port < pdr->pdi_sdf_from_port_range.from || match->ue_port > pdr->pdi_sdf_from_port_range.to)
		return false;

	if (match->extern_port < pdr->pdi_sdf_to_port_range.from || match->extern_port > pdr->pdi_sdf_to_port_range.to)
		return false;

	if (pdr->pdi_sdf_from_ip.v4 && !ipv4_masked_is_matching(&pdr->pdi_sdf_from_ip, match->ue_ip))
		return false;

	if (pdr->pdi_sdf_to_ip.v4 && !ipv4_masked_is_matching(&pdr->pdi_sdf_to_ip, match->extern_ip))
		return false;

	return ipv4_masked_is_matching(&pdr->pdi_ueip, match->ue_ip);
}

/*
 * Lookup a PDR that matches a given 8 tuple, from RAN side
 *
 * @pdrs [in]: list of PDRs
 * @match [in]: header to check
 * @return: pointer to a matching PDR, or NULL if non found
 */
static const struct upf_accel_pdr *upf_accel_ran_pdr_lookup(const struct upf_accel_pdrs *pdrs,
							    const struct upf_accel_match_8t *match)
{
	const struct upf_accel_pdr *pdr;
	size_t i;

	for (i = 0; i < pdrs->num_pdrs; i++) {
		pdr = &pdrs->arr_pdrs[i];
		if (pdr->pdi_si != UPF_ACCEL_PDR_PDI_SI_UL)
			continue;

		if (!upf_accel_pdr_tunnel_is_matching(pdr, &match->outer))
			continue;
		if (!upf_accel_pdr_tuple_is_matching(pdr, &match->inner))
			continue;

		return pdr;
	}

	return NULL;
}

/*
 * Decap GTPU header of a packet inplace
 *
 * @pkt [in]: pointer to the pkt
 * @parse_ctx [in]: pointer to the parser context
 */
static void upf_accel_decap(struct rte_mbuf *pkt, struct upf_accel_tun_parser_ctx *parse_ctx)
{
	const uint8_t src_mac[] = UPF_ACCEL_SRC_MAC;
	const uint8_t dst_mac[] = UPF_ACCEL_DST_MAC;
	struct rte_ether_hdr *eth = NULL;

	rte_pktmbuf_adj(pkt, parse_ctx->len - parse_ctx->inner.len - sizeof(*eth));

	eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
	eth->ether_type = RTE_BE16(RTE_ETHER_TYPE_IPV4);
	SET_MAC_ADDR(eth->src_addr.addr_bytes, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
	SET_MAC_ADDR(eth->dst_addr.addr_bytes, dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5]);
}

/*
 * Lookup a PDR that matches a given 5 tuple, from WAN side
 *
 * @pdrs [in]: list of PDRs
 * @match [in]: header to check
 * @return: pointer to a matching PDR, or NULL if non found
 */
static const struct upf_accel_pdr *upf_accel_wan_pdr_lookup(const struct upf_accel_pdrs *pdrs,
							    const struct upf_accel_match_5t *match)
{
	const struct upf_accel_pdr *pdr;
	size_t i;

	for (i = 0; i < pdrs->num_pdrs; i++) {
		pdr = &pdrs->arr_pdrs[i];
		if (pdr->pdi_si != UPF_ACCEL_PDR_PDI_SI_DL)
			continue;

		if (!upf_accel_pdr_tuple_is_matching(pdr, match))
			continue;

		return pdr;
	}

	return NULL;
}

/*
 * Accelerating 8T flow to the applicable DOCA flow pipe.
 *
 * @ctx [in]: UPF Acceleration context.
 * @port_id [in]: port id to accelerate the flow on
 * @queue_id [in]: queue ID
 * @match [in]: software flow match
 * @pdr_id [in]: matching PDR id
 * @entry_ctx [in]: resulting DOCA flow entry accelerate context
 * @entry [out]: resulting DOCA flow entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_pipe_8t_accel(struct upf_accel_ctx *ctx,
					    enum upf_accel_port port_id,
					    uint16_t queue_id,
					    struct upf_accel_match_8t *match,
					    uint32_t pdr_id,
					    struct upf_accel_entry_ctx *entry_ctx,
					    struct doca_flow_pipe_entry **entry)
{
	struct doca_flow_match hw_match = {0};
	struct doca_flow_actions actions = {
		.action_idx = 0,
		.meta.pkt_meta = DOCA_HTOBE32(pdr_id),
	};
	struct doca_flow_pipe *pipe;
	doca_error_t res;

	hw_match.outer.ip4.src_ip = rte_cpu_to_be_32(match->outer.te_ip);

	hw_match.tun.gtp_teid = rte_cpu_to_be_32(match->outer.te_id);
	hw_match.tun.gtp_ext_psc_qfi = match->outer.qfi & 0x3f;
	pipe = match->outer.qfi ? ctx->pipes[port_id][UPF_ACCEL_PIPE_8T] : ctx->pipes[port_id][UPF_ACCEL_PIPE_7T];

	hw_match.inner.ip4.dst_ip = rte_cpu_to_be_32(match->inner.extern_ip);
	hw_match.inner.ip4.src_ip = rte_cpu_to_be_32(match->inner.ue_ip);
	hw_match.inner.ip4.next_proto = match->inner.ip_proto;
	switch (match->inner.ip_proto) {
	case DOCA_FLOW_PROTO_TCP:
		hw_match.inner.tcp.l4_port.dst_port = rte_cpu_to_be_16(match->inner.extern_port);
		hw_match.inner.tcp.l4_port.src_port = rte_cpu_to_be_16(match->inner.ue_port);
		break;
	case DOCA_FLOW_PROTO_UDP:
		hw_match.inner.udp.l4_port.dst_port = rte_cpu_to_be_16(match->inner.extern_port);
		hw_match.inner.udp.l4_port.src_port = rte_cpu_to_be_16(match->inner.ue_port);
		break;
	default:
		assert(0);
	}

	res = doca_flow_pipe_add_entry(queue_id,
				       pipe,
				       &hw_match,
				       &actions,
				       NULL,
				       NULL,
				       DOCA_FLOW_WAIT_FOR_BATCH,
				       entry_ctx,
				       entry);
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Failed to add dynamic 8T entry: %s", doca_error_get_descr(res));
		return res;
	}

	return res;
}

/*
 * Accelerating 5T flow to the applicable DOCA flow pipe.
 *
 * @ctx [in]: UPF Acceleration context.
 * @port_id [in]: port id to accelerate the flow on
 * @queue_id [in]: queue ID
 * @match [in]: software flow match
 * @pdr_id [in]: matching PDR id
 * @entry_ctx [in]: resulting DOCA flow entry accelerate context
 * @entry [out]: resulting DOCA flow entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_pipe_5t_accel(struct upf_accel_ctx *ctx,
					    enum upf_accel_port port_id,
					    uint16_t queue_id,
					    struct upf_accel_match_5t *match,
					    uint32_t pdr_id,
					    struct upf_accel_entry_ctx *entry_ctx,
					    struct doca_flow_pipe_entry **entry)
{
	struct doca_flow_pipe *pipe = ctx->pipes[port_id][UPF_ACCEL_PIPE_5T];
	struct doca_flow_match hw_match = {0};
	struct doca_flow_actions actions = {
		.action_idx = 0,
		.meta.pkt_meta = DOCA_HTOBE32(pdr_id),
	};
	doca_error_t res;

	hw_match.outer.ip4.dst_ip = rte_cpu_to_be_32(match->ue_ip);
	hw_match.outer.ip4.src_ip = rte_cpu_to_be_32(match->extern_ip);
	hw_match.outer.ip4.next_proto = match->ip_proto;
	switch (match->ip_proto) {
	case DOCA_FLOW_PROTO_TCP:
		hw_match.outer.tcp.l4_port.dst_port = rte_cpu_to_be_16(match->ue_port);
		hw_match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(match->extern_port);
		break;
	case DOCA_FLOW_PROTO_UDP:
		hw_match.outer.udp.l4_port.dst_port = rte_cpu_to_be_16(match->ue_port);
		hw_match.outer.udp.l4_port.src_port = rte_cpu_to_be_16(match->extern_port);
		break;
	default:
		assert(0);
	}

	res = doca_flow_pipe_add_entry(queue_id, pipe, &hw_match, &actions, NULL, NULL, 0, entry_ctx, entry);
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_WARN("Failed to add dynamic 5T entry: %s", doca_error_get_descr(res));
		return res;
	}

	return res;
}

/*
 * Parse the packet and fill in the match structure.
 *
 * @pkt_dir [in]: packet direction
 * @pkt [in]: pointer to the pkt
 * @match [in]: software flow match
 * @parse_ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_fp_pkt_match(enum upf_accel_packet_direction pkt_dir,
					   struct rte_mbuf *pkt,
					   struct upf_accel_match_8t *match,
					   struct upf_accel_tun_parser_ctx *parse_ctx)
{
	uint8_t *data_beg = rte_pktmbuf_mtod(pkt, uint8_t *);
	uint8_t *data_end = data_beg + rte_pktmbuf_data_len(pkt);
	doca_error_t ret;

	memset(match, 0, sizeof(*match));

	if (pkt_dir == PKT_DIR_RAN) {
		ret = upf_accel_ran_match(data_beg, data_end, parse_ctx, match);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_DBG("Failed to parse RAN packet status %u", ret);
			return ret;
		}
	} else {
		ret = upf_accel_wan_match(data_beg, data_end, &parse_ctx->inner, &match->inner);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_DBG("Failed to parse WAN packet status %u", ret);
			return ret;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Fetch packet direction from metadata.
 *
 * @pkt [in]: pointer to the pkt
 * @return: packet direction (ran / wan)
 */
static enum upf_accel_packet_direction upf_accel_fp_fetch_pkt_dir(const struct rte_mbuf *pkt)
{
	const uint32_t md = *RTE_FLOW_DYNF_METADATA(pkt);
	const uint32_t dir = md & UPF_ACCEL_META_PKT_DIR_MASK;

	assert(dir == UPF_ACCEL_META_PKT_DIR_UL || dir == UPF_ACCEL_META_PKT_DIR_DL);

	return (dir == UPF_ACCEL_META_PKT_DIR_UL) ? PKT_DIR_RAN : PKT_DIR_WAN;
}

/*
 * Parse the packet burst and fill in the match structures.
 *
 * @burst_ctx [in]: packet burst context
 * @match_mem [in]: array of match structure used to init burst_ctx->matches
 */
static void upf_accel_fp_pkts_match(struct upf_accel_fp_burst_ctx *burst_ctx, struct upf_accel_match_8t match_mem[])
{
	doca_error_t ret;
	uint16_t i;

	for (i = 0; i < burst_ctx->rx_pkts_cnt; i++) {
		burst_ctx->pkts_dir[i] = upf_accel_fp_fetch_pkt_dir(burst_ctx->rx_pkts[i]);
		burst_ctx->matches[i] = &match_mem[i];

		ret = upf_accel_fp_pkt_match(burst_ctx->pkts_dir[i],
					     burst_ctx->rx_pkts[i],
					     burst_ctx->matches[i],
					     &burst_ctx->parse_ctxs[i]);
		if (ret != DOCA_SUCCESS)
			burst_ctx->pkts_drop[i] = true;
	}
}

/*
 * Compare outer headers (the part of 8/7tuple that is not included in 5tuple)
 *
 * @tunnel1 [in]: first tunnel match
 * @tunnel2 [in]: second tunnel match
 * @return: true if tunnels are equal, false otherwise
 */
static bool upf_accel_fp_tunnel_eq(struct upf_accel_match_tun *tunnel1, struct upf_accel_match_tun *tunnel2)
{
	return tunnel1->te_ip == tunnel2->te_ip && tunnel1->te_id == tunnel2->te_id && tunnel1->qfi == tunnel2->qfi;
}

/*
 * PDR lookup according to 5T match
 *
 * @fp_data [in]: flow processing data
 * @pkt_dir [in]: packet direction
 * @match [in]: software flow match
 * @pdr_out [out]: pdr
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_fp_pdr_lookup(struct upf_accel_fp_data *fp_data,
					    enum upf_accel_packet_direction pkt_dir,
					    struct upf_accel_match_8t *match,
					    const struct upf_accel_pdr **pdr_out)
{
	const struct upf_accel_pdr *pdr;

	pdr = pkt_dir == PKT_DIR_RAN ? upf_accel_ran_pdr_lookup(fp_data->ctx->upf_accel_cfg->pdrs, match) :
				       upf_accel_wan_pdr_lookup(fp_data->ctx->upf_accel_cfg->pdrs, &match->inner);
	if (!pdr) {
		DOCA_LOG_DBG("Failed to lookup PDR for packet direction %u", pkt_dir);
		return DOCA_ERROR_NOT_FOUND;
	}
	*pdr_out = pdr;

	return DOCA_SUCCESS;
}

/*
 * Get existing or initialize a new instance of dynamic connection.
 *
 * @fp_data [in]: flow processing data
 * @pkt_dir [in]: packet direction
 * @match [in]: software flow match
 * @conn_idx [in]: existing connection position in dynamic table or a negative value
 * @conn_out [out]: resulting connection descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_fp_conn_lookup(struct upf_accel_fp_data *fp_data,
					     enum upf_accel_packet_direction pkt_dir,
					     struct upf_accel_match_8t *match,
					     int32_t conn_idx,
					     struct upf_accel_entry_ctx **conn_out)
{
	struct upf_accel_entry_ctx *conn;
	const struct upf_accel_pdr *pdr;
	doca_error_t result;
	hash_sig_t hash;

	if (conn_idx < 0) {
		result = upf_accel_fp_pdr_lookup(fp_data, pkt_dir, match, &pdr);
		if (result != DOCA_SUCCESS)
			return result;

		hash = rte_hash_hash(fp_data->dyn_tbl, &match->inner);
		conn_idx = rte_hash_add_key_with_hash(fp_data->dyn_tbl, &match->inner, hash);
		if (conn_idx < 0) {
			fp_data->accel_failed_counters[pkt_dir].errors++;
			DOCA_LOG_DBG("Couldn't create flow, No space available in the ht, err %d", conn_idx);
			return DOCA_ERROR_FULL;
		}

		conn = &fp_data->dyn_tbl_data[conn_idx];
		memset(conn, 0, sizeof(*conn));
		conn->dyn_ctx.match = *match;
		conn->dyn_ctx.hash = hash;
		conn->dyn_ctx.pdr_id[pkt_dir] = pdr->id;
		conn->dyn_ctx.fp_data = fp_data;
		conn->dyn_ctx.conn_idx = conn_idx;
		conn->dyn_ctx.flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_PENDING;

		upf_accel_sw_aging_ll_node_init(conn, pkt_dir);
	} else {
		conn = &fp_data->dyn_tbl_data[conn_idx];

		if (pkt_dir == PKT_DIR_RAN) {
			if (upf_accel_flow_is_alive(conn->dyn_ctx.flow_status[pkt_dir]) &&
			    !upf_accel_fp_tunnel_eq(&match->outer, &conn->dyn_ctx.match.outer)) {
				DOCA_LOG_DBG("Detected packet with same 5tuple as connection with different tunnel");
				return DOCA_ERROR_ALREADY_EXIST;
			}
			conn->dyn_ctx.match.outer = match->outer;
		}

		/*
		 * The connection was already initialized by a packet arriving
		 * from the opposite direction; now, we need to complete the
		 * initialization for the other direction.
		 */
		if (conn->dyn_ctx.flow_status[pkt_dir] == UPF_ACCEL_FLOW_STATUS_NONE) {
			result = upf_accel_fp_pdr_lookup(fp_data, pkt_dir, match, &pdr);
			if (result != DOCA_SUCCESS)
				return result;

			conn->dyn_ctx.flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_PENDING;
			conn->dyn_ctx.pdr_id[pkt_dir] = pdr->id;
			upf_accel_sw_aging_ll_node_init(conn, pkt_dir);
		}
	}

	*conn_out = conn;
	return DOCA_SUCCESS;
}

/*
 * Get existing or initialize a new instance of dynamic connection for every packet in the burst.
 *
 * @fp_data [in]: flow processing data
 * @burst_ctx [in]: packet burst context
 */
static void upf_accel_fp_conns_lookup(struct upf_accel_fp_data *fp_data, struct upf_accel_fp_burst_ctx *burst_ctx)
{
	int32_t conn_idxs[RTE_HASH_LOOKUP_BULK_MAX];
	doca_error_t ret;
	uint16_t i;
	int err;

	if (!burst_ctx->rx_pkts_cnt)
		return;

	err = rte_hash_lookup_bulk(fp_data->dyn_tbl,
				   (const void **)burst_ctx->matches,
				   burst_ctx->rx_pkts_cnt,
				   conn_idxs);
	assert(!err);
	UNUSED(err);

	for (i = 0; i < burst_ctx->rx_pkts_cnt; i++) {
		if (burst_ctx->pkts_drop[i])
			continue;

		ret = upf_accel_fp_conn_lookup(fp_data,
					       burst_ctx->pkts_dir[i],
					       burst_ctx->matches[i],
					       conn_idxs[i],
					       &burst_ctx->conns[i]);
		if (ret != DOCA_SUCCESS)
			burst_ctx->pkts_drop[i] = true;
	}
}

/*
 * Accelerate a unidirectional flow on a connection
 *
 * @fp_data [in]: flow processing data
 * @port_id [in]: port id
 * @pkt_dir [in]: packet direction
 * @match [in]: software flow match
 * @conn [in]: connection descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t upf_accel_fp_flow_accel(struct upf_accel_fp_data *fp_data,
					    enum upf_accel_port port_id,
					    enum upf_accel_packet_direction pkt_dir,
					    struct upf_accel_match_8t *match,
					    struct upf_accel_entry_ctx *conn)
{
	doca_error_t ret;

	if (conn->dyn_ctx.flow_status[pkt_dir] == UPF_ACCEL_FLOW_STATUS_ACCELERATED)
		return DOCA_ERROR_ALREADY_EXIST;
	if (unlikely(conn->dyn_ctx.flow_status[pkt_dir] == UPF_ACCEL_FLOW_STATUS_FAILED_ACCELERATION))
		return DOCA_SUCCESS;

	if (upf_accel_flow_is_alive(conn->dyn_ctx.flow_status[pkt_dir]) &&
	    (conn->dyn_ctx.cnt_pkts[pkt_dir] + 1) < fp_data->ctx->upf_accel_cfg->dpi_threshold) {
		conn->dyn_ctx.flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_UNACCELERATED;
		return DOCA_SUCCESS;
	}

	ret = pkt_dir == PKT_DIR_RAN ? upf_accel_pipe_8t_accel(fp_data->ctx,
							       port_id,
							       fp_data->queue_id,
							       match,
							       conn->dyn_ctx.pdr_id[pkt_dir],
							       conn,
							       &conn->dyn_ctx.entries[pkt_dir].entry) :
				       upf_accel_pipe_5t_accel(fp_data->ctx,
							       port_id,
							       fp_data->queue_id,
							       &match->inner,
							       conn->dyn_ctx.pdr_id[pkt_dir],
							       conn,
							       &conn->dyn_ctx.entries[pkt_dir].entry);
	switch (ret) {
	case DOCA_SUCCESS:
		conn->dyn_ctx.flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_ACCELERATED;
		upf_accel_sw_aging_ll_node_remove(fp_data, conn, pkt_dir);
		break;
	default:
		conn->dyn_ctx.flow_status[pkt_dir] = UPF_ACCEL_FLOW_STATUS_FAILED_ACCELERATION;
		fp_data->accel_failed_counters[pkt_dir].current++;
		fp_data->accel_failed_counters[pkt_dir].total++;
		DOCA_LOG_DBG("Failed to accelerate connection on port %u, direction %u.", port_id, pkt_dir);
	}

	return DOCA_SUCCESS;
}

/*
 * Increase packet counter (bytes and number).
 *
 * @ctr [in]: counter to increase.
 * @pkt [in]: pointer to the pkt mbuf.
 */
static inline void upf_accel_packet_byte_counter_inc(struct upf_accel_packet_byte_counter *ctr, struct rte_mbuf *pkt)
{
	ctr->pkts++;
	ctr->bytes += rte_pktmbuf_pkt_len(pkt);
}

/*
 * Accelerate a unidirectional flow on every connection in the burst.
 *
 * @fp_data [in]: flow processing data
 * @rx_port_id [in]: incoming packet port id
 * @burst_ctx [in]: packet burst context
 */
static void upf_accel_fp_flows_accel(struct upf_accel_fp_data *fp_data,
				     enum upf_accel_port rx_port_id,
				     struct upf_accel_fp_burst_ctx *burst_ctx)
{
	enum upf_accel_packet_direction pkt_dir;
	struct upf_accel_entry_ctx *conn;
	struct rte_mbuf *pkt;
	doca_error_t ret;
	uint16_t i;

	for (i = 0; i < burst_ctx->rx_pkts_cnt; i++) {
		if (burst_ctx->pkts_drop[i])
			continue;

		conn = burst_ctx->conns[i];
		pkt = burst_ctx->rx_pkts[i];
		pkt_dir = burst_ctx->pkts_dir[i];

		ret = upf_accel_fp_flow_accel(fp_data, rx_port_id, pkt_dir, burst_ctx->matches[i], conn);
		if (ret == DOCA_ERROR_ALREADY_EXIST)
			DOCA_LOG_DBG("Got a sneaker packet on core %u, port %u, direction %u.",
				     fp_data->queue_id,
				     rx_port_id,
				     pkt_dir);

		if (conn->dyn_ctx.cnt_pkts[pkt_dir])
			upf_accel_packet_byte_counter_inc(&fp_data->sw_counters.ex_conn, pkt);
		else
			upf_accel_packet_byte_counter_inc(&fp_data->sw_counters.new_conn, pkt);

		conn->dyn_ctx.cnt_pkts[pkt_dir]++;
		conn->dyn_ctx.cnt_bytes[pkt_dir] += rte_pktmbuf_pkt_len(pkt);
	}
}

/*
 * Drop the packet and increase error counters.
 *
 * @fp_data [in]: flow processing data
 * @pkt [in]: packet to drop
 */
static void upf_accel_fp_pkt_err_drop(struct upf_accel_fp_data *fp_data, struct rte_mbuf *pkt)
{
	upf_accel_packet_byte_counter_inc(&fp_data->sw_counters.err, pkt);
	rte_pktmbuf_free(pkt);
}

/*
 * Set metadata to a packet, in place
 *
 * @pkt [in]: pointer to the pkt
 * @md [in]: metadata to set
 */
static void upf_accel_md_set(struct rte_mbuf *pkt, uint32_t md)
{
	assert(md < UPF_ACCEL_MAX_NUM_PDR);
	*RTE_FLOW_DYNF_METADATA(pkt) = UPF_ACCEL_META_FROM_SW | md;
	pkt->ol_flags |= RTE_MBUF_DYNFLAG_TX_METADATA;
}

/*
 * Check if the flow is not in an accelerated status
 *
 * @pkt_dir [in]: packet direction
 * @conn [in]: connection descriptor
 * @return: true if it`s unaccelerated, false otherwise
 */
static bool is_flow_unaccelerated(enum upf_accel_packet_direction pkt_dir, struct upf_accel_entry_ctx *conn)
{
	return (conn->dyn_ctx.flow_status[pkt_dir] == UPF_ACCEL_FLOW_STATUS_FAILED_ACCELERATION) ||
	       (conn->dyn_ctx.flow_status[pkt_dir] == UPF_ACCEL_FLOW_STATUS_UNACCELERATED);
}

/*
 * Perform miscellaneous post processing operations on the burst. (packet metadata, decap, send)
 *
 * @fp_data [in]: flow processing data
 * @burst_ctx [in]: packet burst context
 */
static void upf_accel_fp_burst_postprocess(struct upf_accel_fp_data *fp_data, struct upf_accel_fp_burst_ctx *burst_ctx)
{
	enum upf_accel_packet_direction pkt_dir;
	struct upf_accel_entry_ctx *conn;
	struct upf_accel_match_8t *match;
	struct rte_mbuf *pkt;
	uint16_t i;

	for (i = 0; i < burst_ctx->rx_pkts_cnt; i++) {
		pkt = burst_ctx->rx_pkts[i];
		conn = burst_ctx->conns[i];
		match = burst_ctx->matches[i];
		pkt_dir = burst_ctx->pkts_dir[i];

		if (burst_ctx->pkts_drop[i]) {
			upf_accel_fp_pkt_err_drop(fp_data, pkt);
			continue;
		}

		DOCA_LOG_DBG(
			"Core %u, dir %u parsed 8t tun_ip=%x teid=%u qfi=%hhu ue_ip=%x extern_ip=%x ue_port=%hu extern_port=%hu ip_proto=%hhu pdr=%u ran_pkts=%lu ran_bytes=%lu, wan_pkts=%lu, wan_bytes=%lu",
			rte_lcore_id(),
			pkt_dir,
			match->outer.te_ip,
			match->outer.te_id,
			match->outer.qfi,
			match->inner.ue_ip,
			match->inner.extern_ip,
			match->inner.ue_port,
			match->inner.extern_port,
			match->inner.ip_proto,
			conn->dyn_ctx.pdr_id[pkt_dir],
			conn->dyn_ctx.cnt_pkts[PKT_DIR_RAN],
			conn->dyn_ctx.cnt_bytes[PKT_DIR_RAN],
			conn->dyn_ctx.cnt_pkts[PKT_DIR_WAN],
			conn->dyn_ctx.cnt_bytes[PKT_DIR_WAN]);

		if (pkt_dir == PKT_DIR_RAN)
			upf_accel_decap(pkt, &burst_ctx->parse_ctxs[i]);

		if (is_flow_unaccelerated(pkt_dir, conn))
			upf_accel_sw_aging_ll_node_move_to_head(fp_data, conn, pkt_dir);

		upf_accel_md_set(pkt, conn->dyn_ctx.pdr_id[pkt_dir]);
		burst_ctx->tx_pkts[burst_ctx->tx_pkts_cnt++] = burst_ctx->rx_pkts[i];
	}
}

/*
 * Initialize aging infrastructure.
 *
 * @fp_data [in]: flow processing data
 */
static void upf_accel_aging_init(struct upf_accel_fp_data *fp_data)
{
	enum upf_accel_port port_id;

	for (port_id = 0; port_id < fp_data->ctx->num_ports; port_id++)
		fp_data->last_hw_aging_tsc[port_id] = rte_rdtsc();
}

/*
 * If aging is in progress or the aging period timeout has expired, poll for up to UPF_ACCEL_MAX_NUM_AGING entries. Note
 * that this function doesn't process any potential resulting flow removals and relies on the caller to do it
 * afterwards, potentially combining it with entry additions/removal scheduled by other sources.
 *
 * @fp_data [in]: flow processing data
 * @port_id [in]: port_id of the port to remove the aged flows from
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t upf_accel_hw_aging_poll(struct upf_accel_fp_data *fp_data, enum upf_accel_port port_id)
{
	uint64_t aging_interval_tsc = UPF_ACCEL_HW_AGING_POLL_INTERVAL_SEC * rte_get_tsc_hz();
	struct upf_accel_ctx *ctx = fp_data->ctx;
	int num_aged_entries;

	if (rte_rdtsc() - fp_data->last_hw_aging_tsc[port_id] >= aging_interval_tsc) {
		if (fp_data->hw_aging_in_progress[port_id])
			DOCA_LOG_ERR("In core %d port %d: couldn't finish HW aging, continuing...",
				     fp_data->queue_id,
				     port_id);
		fp_data->hw_aging_in_progress[port_id] = true;
		fp_data->last_hw_aging_tsc[port_id] = rte_rdtsc();
	}
	if (!fp_data->hw_aging_in_progress[port_id])
		return DOCA_SUCCESS;

	num_aged_entries = doca_flow_aging_handle(ctx->ports[port_id],
						  fp_data->queue_id,
						  UPF_ACCEL_DOCA_FLOW_MAX_TIMEOUT_US,
						  UPF_ACCEL_MAX_NUM_AGING);
	if (num_aged_entries == -1)
		fp_data->hw_aging_in_progress[port_id] = false;
	else
		DOCA_LOG_DBG("In core %d port %d: num of aged entries: %d",
			     fp_data->queue_id,
			     port_id,
			     num_aged_entries);

	return DOCA_SUCCESS;
}

/*
 * Run one iteration of flow processing for specific port
 *
 * Receive burst of packets on rx port, process them accelerating any rules, if
 * necessary, and send the packets out to tx port.
 *
 * @fp_data [in]: flow processing data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: send port id
 */
static void upf_accel_fp_run_port(struct upf_accel_fp_data *fp_data,
				  enum upf_accel_port rx_port_id,
				  enum upf_accel_port tx_port_id)
{
	struct upf_accel_match_8t match_mem[UPF_ACCEL_MAX_PKT_BURST];
	struct upf_accel_fp_burst_ctx burst_ctx = {
		.pkts_drop = {0},
	};
	doca_error_t ret;
	uint16_t sent;

	burst_ctx.rx_pkts_cnt =
		rte_eth_rx_burst(rx_port_id, fp_data->queue_id, burst_ctx.rx_pkts, UPF_ACCEL_MAX_PKT_BURST);

	upf_accel_fp_pkts_match(&burst_ctx, match_mem);
	upf_accel_fp_conns_lookup(fp_data, &burst_ctx);
	upf_accel_fp_flows_accel(fp_data, rx_port_id, &burst_ctx);
	upf_accel_fp_burst_postprocess(fp_data, &burst_ctx);

	ret = upf_accel_hw_aging_poll(fp_data, rx_port_id);
	if (ret != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to execute HW aging poll on port %hu: %s", rx_port_id, doca_error_get_descr(ret));

	ret = doca_flow_entries_process(fp_data->ctx->ports[rx_port_id],
					fp_data->queue_id,
					UPF_ACCEL_DOCA_FLOW_MAX_TIMEOUT_US,
					0);
	if (ret != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to process flow entries on port %hu: %s", rx_port_id, doca_error_get_descr(ret));

	sent = rte_eth_tx_burst(tx_port_id, fp_data->queue_id, burst_ctx.tx_pkts, burst_ctx.tx_pkts_cnt);
	if (unlikely(sent < burst_ctx.tx_pkts_cnt)) {
		DOCA_LOG_WARN("Failed to send packets to port id %hu queue %hu", tx_port_id, fp_data->queue_id);

		do {
			upf_accel_fp_pkt_err_drop(fp_data, burst_ctx.tx_pkts[sent]);
		} while (++sent < burst_ctx.tx_pkts_cnt);
	}
}

/*
 * Run one iteration of flow processing.
 *
 * @fp_data [in]: flow processing data
 */
static void upf_accel_fp_run(struct upf_accel_fp_data *fp_data)
{
	enum upf_accel_port port_id;

	for (port_id = 0; port_id < fp_data->ctx->num_ports; port_id++)
		upf_accel_fp_run_port(fp_data, port_id, fp_data->ctx->get_fwd_port(port_id));
}

/*
 * Check and handles (if exceeds) a quota for pdr
 *
 * @ctx [in]: UPF Acceleration context
 * @pdr_id [in]: pdr ID
 * @query [in]: quota counter query
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t handle_exceeds_quota_for_pdr(struct upf_accel_ctx *ctx,
						 uint16_t pdr_id,
						 struct doca_flow_resource_query *query)
{
	struct upf_accel_pdr *pdr = &ctx->upf_accel_cfg->pdrs->arr_pdrs[pdr_id];
	struct upf_accel_urr *urr;
	uint32_t i;

	for (i = 0; i < pdr->urrids_num; ++i) {
		urr = &ctx->upf_accel_cfg->urrs->arr_urrs[pdr->urrids[i]];
		if (query->counter.total_bytes >= urr->volume_quota_total_volume) {
			/*
			 * Quota exceeded.
			 * This code part should be implemented once a definition
			 * of how to react is complete.
			 */
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Iterates through all quota counters and handles if exceeds
 *
 * @fp_data [in]: flow processing data
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t handle_exceeds_quotas(struct upf_accel_fp_data *fp_data)
{
	struct app_shared_counter_ids *shared_counter_ids = &fp_data->quota_cntrs;
	struct doca_flow_resource_query query_results_array[UPF_ACCEL_MAX_NUM_PDR];
	size_t cntrs_num = shared_counter_ids->cntrs_num;
	enum upf_accel_port port_id;
	doca_error_t result;
	uint16_t pdr_id;
	uint32_t i;

	if (!cntrs_num)
		return DOCA_SUCCESS;

	for (port_id = 0; port_id < fp_data->ctx->num_ports; ++port_id) {
		result = doca_flow_shared_resources_query(DOCA_FLOW_SHARED_RESOURCE_COUNTER,
							  shared_counter_ids->ids[port_id],
							  query_results_array,
							  cntrs_num);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to query entries: %s", doca_error_get_descr(result));
			return result;
		}

		for (i = 0; i < cntrs_num; ++i) {
			pdr_id = shared_counter_ids->cntr_0 + i;

			result = handle_exceeds_quota_for_pdr(fp_data->ctx, pdr_id, &query_results_array[i]);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to handle quota for pdr %d: %s",
					     pdr_id,
					     doca_error_get_descr(result));
				return result;
			}
		}
	}

	return DOCA_SUCCESS;
}

void upf_accel_fp_loop(struct upf_accel_fp_data *fp_data)
{
	doca_error_t result;

	upf_accel_aging_init(fp_data);

	while (!force_quit) {
		upf_accel_fp_run(fp_data);

		upf_accel_sw_aging_ll_scan(fp_data, PKT_DIR_RAN);
		upf_accel_sw_aging_ll_scan(fp_data, PKT_DIR_WAN);

		result = handle_exceeds_quotas(fp_data);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to handle expired quotas: %s", doca_error_get_descr(result));
			return;
		}
	}
}
