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

#include "ip_frag_packet_parser.h"
#include "ip_frag_dp.h"
#include <flow_common.h>

#include <doca_log.h>
#include <doca_flow.h>
#include <dpdk_utils.h>

#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <rte_ip_frag.h>
#include <rte_cycles.h>
#include <rte_mempool.h>

#include <stdbool.h>

#define IP_FRAG_MAX_PKT_BURST 32
#define IP_FRAG_BURST_PREFETCH (IP_FRAG_MAX_PKT_BURST / 8)
#define IP_FRAG_FLUSH_THRESHOLD 16

#define IP_FRAG_TBL_BUCKET_SIZE 4

DOCA_LOG_REGISTER(IP_FRAG::DP);

struct ip_frag_sw_counters {
	uint64_t frags_rx;	/* Fragments received that need reassembly */
	uint64_t whole;		/* Whole packets (either reassembled or not fragmented) */
	uint64_t mtu_fits_rx;	/* Packets received that are within the MTU size and dont require fragmentation */
	uint64_t mtu_exceed_rx; /* Packets received that exceed the MTU size and require fragmentation */
	uint64_t frags_gen;	/* Fragments generated from packets that exceed the MTU size */
	uint64_t err;		/* Errors */
};

struct ip_frag_wt_data {
	const struct ip_frag_config *cfg;			  /* Application config */
	uint16_t queue_id;					  /* Queue id */
	struct ip_frag_sw_counters sw_counters[IP_FRAG_PORT_NUM]; /* SW counters */
	struct rte_eth_dev_tx_buffer *tx_buffer;		  /* TX buffer */
	uint64_t tx_buffer_err;					  /* TX buffer error counter */
	struct rte_ip_frag_tbl *frag_tbl;			  /* Fragmentation table */
	struct rte_mempool *indirect_pool;			  /* Indirect memory pool */
	struct rte_ip_frag_death_row death_row;			  /* Fragmentation table expired fragments death row */
} __rte_aligned(RTE_CACHE_LINE_SIZE);

bool force_stop = false;

/*
 * Drop the packet and increase error counters.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: incoming packet port id
 * @pkt [in]: packet to drop
 */
static void ip_frag_pkt_err_drop(struct ip_frag_wt_data *wt_data, uint16_t rx_port_id, struct rte_mbuf *pkt)
{
	wt_data->sw_counters[rx_port_id].err++;
	rte_pktmbuf_free(pkt);
}

/*
 * Parse the packet.
 *
 * @dir [out]: incoming packet direction
 * @pkt [in]: pointer to the pkt
 * @parse_ctx [out]: pointer to the parser context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_pkt_parse(enum ip_frag_pkt_dir *dir,
				      struct rte_mbuf *pkt,
				      struct ip_frag_tun_parser_ctx *parse_ctx)
{
	uint8_t *data_beg = rte_pktmbuf_mtod(pkt, uint8_t *);
	uint8_t *data_end = data_beg + rte_pktmbuf_data_len(pkt);

	switch (*dir) {
	case IP_FRAG_PKT_DIR_0:
		return ip_frag_ran_parse(data_beg, data_end, parse_ctx);
	case IP_FRAG_PKT_DIR_1:
		return ip_frag_wan_parse(data_beg, data_end, &parse_ctx->inner);
	case IP_FRAG_PKT_DIR_UNKNOWN:
		return ip_frag_dir_parse(data_beg, data_end, parse_ctx, dir);
	default:
		assert(0);
		return DOCA_ERROR_INVALID_VALUE;
	}
}

/*
 * Prepare packet mbuf for reassembly.
 *
 * @pkt [in]: packet
 * @l2_len [in]: L2 header length
 * @l3_len [in]: L3 header length
 * @flags [in]: mbuf flags to set
 */
static void ip_frag_pkt_reassemble_prepare(struct rte_mbuf *pkt, size_t l2_len, size_t l3_len, uint64_t flags)
{
	pkt->l2_len = l2_len;
	pkt->l3_len = l3_len;
	pkt->ol_flags |= flags;
}

/*
 * Calculate and set IPv4 header checksum
 *
 * @hdr [in]: IPv4 header
 */
static void ip_frag_ipv4_hdr_cksum(struct rte_ipv4_hdr *hdr)
{
	hdr->hdr_checksum = 0;
	hdr->hdr_checksum = rte_ipv4_cksum(hdr);
}

/*
 * Fixup the packet headers after reassembly.
 *
 * @wt_data [in]: worker thread data
 * @dir [in]: incoming packet direction
 * @pkt [in]: packet
 * @parse_ctx [in]: pointer to the parser context
 */
static void ip_frag_pkt_fixup(struct ip_frag_wt_data *wt_data,
			      enum ip_frag_pkt_dir dir,
			      struct rte_mbuf *pkt,
			      struct ip_frag_tun_parser_ctx *parse_ctx)
{
	assert(dir == IP_FRAG_PKT_DIR_0 || dir == IP_FRAG_PKT_DIR_1);

	if (pkt->ol_flags & wt_data->cfg->mbuf_flag_inner_modified) {
		ip_frag_ipv4_hdr_cksum(parse_ctx->inner.network_ctx.ipv4_hdr);
		if (dir == IP_FRAG_PKT_DIR_0) {
			/* Payload has been modified, need to fix the encapsulation accordingly going from inner to
			 * outer protocols since changing inner data may affect outer's checksums. Fix GTPU payload
			 * length first (which includes optional fields)... */
			parse_ctx->gtp_ctx.gtp_hdr->plen =
				rte_cpu_to_be_16(rte_pktmbuf_pkt_len(pkt) -
						 (parse_ctx->link_ctx.len + parse_ctx->network_ctx.len +
						  parse_ctx->transport_ctx.len + sizeof(*parse_ctx->gtp_ctx.gtp_hdr)));

			/* ...then fix UDP total length, either recalculating or zeroing-out encapsulation UDP
			 * checksum... */
			parse_ctx->transport_ctx.udp_hdr->dgram_len = rte_cpu_to_be_16(
				rte_pktmbuf_pkt_len(pkt) - (parse_ctx->link_ctx.len + parse_ctx->network_ctx.len));
			parse_ctx->transport_ctx.udp_hdr->dgram_cksum = 0;

			/* ...and fix the IP total length which requires recalculating header checksum */
			parse_ctx->network_ctx.ipv4_hdr->total_length =
				rte_cpu_to_be_16(rte_pktmbuf_pkt_len(pkt) - parse_ctx->link_ctx.len);
			ip_frag_ipv4_hdr_cksum(parse_ctx->network_ctx.ipv4_hdr);
		}
	} else if (pkt->ol_flags & wt_data->cfg->mbuf_flag_outer_modified) {
		ip_frag_ipv4_hdr_cksum(parse_ctx->network_ctx.ipv4_hdr);
	}
}

/*
 * Flatten chained mbuf to a single contiguous mbuf segment
 *
 * @pkt [in]: chained mbuf head
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_pkt_flatten(struct rte_mbuf *pkt)
{
	uint16_t tail_size = rte_pktmbuf_pkt_len(pkt) - rte_pktmbuf_data_len(pkt);
	struct rte_mbuf *tail = pkt->next;
	struct rte_mbuf *tmp;
	uint16_t seg_len;
	char *dst;

	if (tail_size > rte_pktmbuf_tailroom(pkt)) {
		DOCA_LOG_DBG("Resulting packet size %u doesn't fit into tailroom size %u",
			     tail_size,
			     rte_pktmbuf_tailroom(pkt));
		return DOCA_ERROR_TOO_BIG;
	}

	pkt->next = NULL;
	pkt->nb_segs = 1;
	pkt->pkt_len = pkt->data_len;

	while (tail) {
		seg_len = rte_pktmbuf_data_len(tail);
		dst = rte_pktmbuf_append(pkt, seg_len);
		assert(dst); /* already verified that tail fits */
		memcpy(dst, rte_pktmbuf_mtod(tail, void *), seg_len);

		tmp = tail->next;
		rte_pktmbuf_free_seg(tail);
		tail = tmp;
	}

	return DOCA_SUCCESS;
}

/*
 * Set necessary mbuf fields and push the packet to the frag table.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @dir [in]: incoming packet direction
 * @pkt [in]: packet
 * @parse_ctx [in]: pointer to the parser context
 * @rx_ts [in]: burst reception timestamp
 * @whole_pkt [out]: resulting reassembled packet
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_pkt_reassemble_push(struct ip_frag_wt_data *wt_data,
						uint16_t rx_port_id,
						enum ip_frag_pkt_dir dir,
						struct rte_mbuf *pkt,
						struct ip_frag_tun_parser_ctx *parse_ctx,
						uint64_t rx_ts,
						struct rte_mbuf **whole_pkt)
{
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_mbuf *res;
	doca_error_t ret;

	assert(dir == IP_FRAG_PKT_DIR_0 || dir == IP_FRAG_PKT_DIR_1);

	if (parse_ctx->network_ctx.frag) {
		ip_frag_pkt_reassemble_prepare(pkt,
					       parse_ctx->link_ctx.len,
					       parse_ctx->network_ctx.len,
					       wt_data->cfg->mbuf_flag_outer_modified);
		ipv4_hdr = parse_ctx->network_ctx.ipv4_hdr;
	} else if (parse_ctx->inner.network_ctx.frag) {
		ip_frag_pkt_reassemble_prepare(pkt,
					       dir == IP_FRAG_PKT_DIR_1	   ? parse_ctx->inner.link_ctx.len :
									     /* For tunneled packets we treat the
										whole encapsulation as  L2 for the
										purpose of reassembly library. */
									     parse_ctx->len - parse_ctx->inner.len,
					       parse_ctx->inner.network_ctx.len,
					       wt_data->cfg->mbuf_flag_inner_modified);
		ipv4_hdr = parse_ctx->inner.network_ctx.ipv4_hdr;
	} else {
		return DOCA_ERROR_INVALID_VALUE;
	}

	res = rte_ipv4_frag_reassemble_packet(wt_data->frag_tbl, &wt_data->death_row, pkt, rx_ts, ipv4_hdr);
	if (!res)
		return DOCA_ERROR_AGAIN;

	if (!wt_data->cfg->mbuf_chain && !rte_pktmbuf_is_contiguous(res)) {
		ret = ip_frag_pkt_flatten(res);
		if (ret != DOCA_SUCCESS) {
			ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
			return ret;
		}
	}

	*whole_pkt = res;
	return DOCA_SUCCESS;
}

/*
 * Reassemble the packet.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: outgoing packet port id
 * @dir [in]: incoming packet direction
 * @pkt [in]: packet
 * @rx_ts [in]: burst reception timestamp
 */
static void ip_frag_pkt_reassemble(struct ip_frag_wt_data *wt_data,
				   uint16_t rx_port_id,
				   uint16_t tx_port_id,
				   enum ip_frag_pkt_dir dir,
				   struct rte_mbuf *pkt,
				   uint64_t rx_ts)
{
	struct ip_frag_tun_parser_ctx parse_ctx;
	enum ip_frag_pkt_dir dir_curr;
	doca_error_t ret;
	bool reparse;

	do {
		reparse = false;
		/* For fragmented packet parser can't correctly deduce rx direction on the first pass so reset it on
		 * each reparse iteration. */
		dir_curr = dir;
		memset(&parse_ctx, 0, sizeof(parse_ctx));

		ret = ip_frag_pkt_parse(&dir_curr, pkt, &parse_ctx);
		switch (ret) {
		case DOCA_SUCCESS:
			wt_data->sw_counters[rx_port_id].whole++;
			ip_frag_pkt_fixup(wt_data, dir_curr, pkt, &parse_ctx);
			rte_eth_tx_buffer(tx_port_id, wt_data->queue_id, wt_data->tx_buffer, pkt);
			break;

		case DOCA_ERROR_AGAIN:
			wt_data->sw_counters[rx_port_id].frags_rx++;
			ret = ip_frag_pkt_reassemble_push(wt_data, rx_port_id, dir_curr, pkt, &parse_ctx, rx_ts, &pkt);
			if (ret == DOCA_SUCCESS) {
				reparse = true;
			} else if (ret != DOCA_ERROR_AGAIN) {
				DOCA_LOG_ERR("Unexpected packet fragmentation");
				ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
			}
			break;

		default:
			ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
			DOCA_LOG_DBG("Failed to parse packet status %u", ret);
			break;
		}
	} while (reparse);
}

/*
 * Reassemble the packet burst buffering any resulting packets.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: send port id
 * @dir [in]: incoming packet direction
 * @pkts [in]: packet burst
 * @pkts_cnt [in]: number of packets in the burst
 * @rx_ts [in]: burst reception timestamp
 */
static void ip_frag_pkts_reassemble(struct ip_frag_wt_data *wt_data,
				    uint16_t rx_port_id,
				    uint16_t tx_port_id,
				    enum ip_frag_pkt_dir dir,
				    struct rte_mbuf *pkts[],
				    int pkts_cnt,
				    uint64_t rx_ts)
{
	int i; /* prefetch calculation can yield negative values */

	for (i = 0; i < IP_FRAG_BURST_PREFETCH && i < pkts_cnt; i++)
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));

	for (i = 0; i < (pkts_cnt - IP_FRAG_BURST_PREFETCH); i++) {
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i + IP_FRAG_BURST_PREFETCH], void *));
		ip_frag_pkt_reassemble(wt_data, rx_port_id, tx_port_id, dir, pkts[i], rx_ts);
	}

	for (; i < pkts_cnt; i++)
		ip_frag_pkt_reassemble(wt_data, rx_port_id, tx_port_id, dir, pkts[i], rx_ts);
}

/*
 * Receive a burst of packets on rx port, reassemble any fragments and send resulting packets on the tx port.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: send port id
 * @dir [in]: incoming packet direction
 */
static void ip_frag_wt_reassemble(struct ip_frag_wt_data *wt_data,
				  uint16_t rx_port_id,
				  uint16_t tx_port_id,
				  enum ip_frag_pkt_dir dir)
{
	struct rte_mbuf *pkts[IP_FRAG_MAX_PKT_BURST];
	uint16_t pkts_cnt;

	pkts_cnt = rte_eth_rx_burst(rx_port_id, wt_data->queue_id, pkts, IP_FRAG_MAX_PKT_BURST);
	if (likely(pkts_cnt)) {
		ip_frag_pkts_reassemble(wt_data, rx_port_id, tx_port_id, dir, pkts, pkts_cnt, rte_rdtsc());
		rte_eth_tx_buffer_flush(tx_port_id, wt_data->queue_id, wt_data->tx_buffer);
	} else {
		rte_ip_frag_table_del_expired_entries(wt_data->frag_tbl, &wt_data->death_row, rte_rdtsc());
	}
	rte_ip_frag_free_death_row(&wt_data->death_row, IP_FRAG_BURST_PREFETCH);
}

/*
 * Fragment the packet, if necessary, and buffer resulting packets.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: outgoing packet port id
 * @pkt [in]: packet
 */
static void ip_frag_pkt_fragment(struct ip_frag_wt_data *wt_data,
				 uint16_t rx_port_id,
				 uint16_t tx_port_id,
				 struct rte_mbuf *pkt)
{
	struct rte_eth_dev_tx_buffer *tx_buffer = wt_data->tx_buffer;
	uint8_t eth_hdr_copy[RTE_PKTMBUF_HEADROOM];
	struct ip_frag_conn_parser_ctx parse_ctx;
	size_t eth_hdr_len;
	void *eth_hdr_new;
	doca_error_t ret;
	int num_frags;
	int i;

	memset(&parse_ctx, 0, sizeof(parse_ctx));
	/* We only fragment the outer header and don't care about parsing encapsulation, so always treat the packet as
	 * coming from the WAN port. */
	ret = ip_frag_wan_parse(rte_pktmbuf_mtod(pkt, uint8_t *),
				rte_pktmbuf_mtod(pkt, uint8_t *) + rte_pktmbuf_data_len(pkt),
				&parse_ctx);
	if (ret != DOCA_SUCCESS) {
		ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
		DOCA_LOG_DBG("Failed to parse packet status %u", ret);
		return;
	}

	if (rte_pktmbuf_pkt_len(pkt) <= wt_data->cfg->mtu) {
		wt_data->sw_counters[rx_port_id].mtu_fits_rx++;
		rte_eth_tx_buffer(tx_port_id, wt_data->queue_id, tx_buffer, pkt);
		return;
	}

	wt_data->sw_counters[rx_port_id].mtu_exceed_rx++;
	eth_hdr_len = parse_ctx.link_ctx.len;
	if (sizeof(eth_hdr_copy) < eth_hdr_len) {
		ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
		DOCA_LOG_ERR("Ethernet header size %lu too big", eth_hdr_len);
		return;
	}
	memcpy(eth_hdr_copy, parse_ctx.link_ctx.eth, eth_hdr_len);
	rte_pktmbuf_adj(pkt, eth_hdr_len);

	num_frags = wt_data->cfg->mbuf_chain ? rte_ipv4_fragment_packet(pkt,
									&tx_buffer->pkts[tx_buffer->length],
									tx_buffer->size - tx_buffer->length,
									wt_data->cfg->mtu - eth_hdr_len,
									pkt->pool,
									wt_data->indirect_pool) :
					       rte_ipv4_fragment_copy_nonseg_packet(pkt,
										    &tx_buffer->pkts[tx_buffer->length],
										    tx_buffer->size - tx_buffer->length,
										    wt_data->cfg->mtu - eth_hdr_len,
										    pkt->pool);
	if (num_frags < 0) {
		ip_frag_pkt_err_drop(wt_data, rx_port_id, pkt);
		DOCA_LOG_ERR("RTE fragmentation failed with code: %d", -num_frags);
		return;
	}
	rte_pktmbuf_free(pkt);

	for (i = tx_buffer->length; i < tx_buffer->length + num_frags; i++) {
		pkt = tx_buffer->pkts[i];
		ip_frag_ipv4_hdr_cksum(rte_pktmbuf_mtod(pkt, struct rte_ipv4_hdr *));

		eth_hdr_new = rte_pktmbuf_prepend(pkt, eth_hdr_len);
		assert(eth_hdr_new);
		memcpy(eth_hdr_new, eth_hdr_copy, eth_hdr_len);
	}

	tx_buffer->length += num_frags;
	wt_data->sw_counters[rx_port_id].frags_gen += num_frags;
}

/*
 * Fragment the packet burst buffering any resulting packets. Flush the tx buffer when it gets path threshold packets.
 * Flush the tx buffer when it gets path threshold packets.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: outgoing packet port id
 * @pkts [in]: packet burst
 * @pkts_cnt [in]: number of packets in the burst
 */
static void ip_frag_pkts_fragment(struct ip_frag_wt_data *wt_data,
				  uint16_t rx_port_id,
				  uint16_t tx_port_id,
				  struct rte_mbuf *pkts[],
				  int pkts_cnt)
{
	struct rte_eth_dev_tx_buffer *tx_buffer = wt_data->tx_buffer;
	int i; /* prefetch calculation can yield negative values */

	for (i = 0; i < IP_FRAG_BURST_PREFETCH && i < pkts_cnt; i++)
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));

	for (i = 0; i < (pkts_cnt - IP_FRAG_BURST_PREFETCH); i++) {
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i + IP_FRAG_BURST_PREFETCH], void *));
		ip_frag_pkt_fragment(wt_data, rx_port_id, tx_port_id, pkts[i]);
		if (tx_buffer->size - tx_buffer->length < IP_FRAG_FLUSH_THRESHOLD)
			rte_eth_tx_buffer_flush(tx_port_id, wt_data->queue_id, tx_buffer);
	}

	for (; i < pkts_cnt; i++) {
		ip_frag_pkt_fragment(wt_data, rx_port_id, tx_port_id, pkts[i]);
		if (tx_buffer->size - tx_buffer->length < IP_FRAG_FLUSH_THRESHOLD)
			rte_eth_tx_buffer_flush(tx_port_id, wt_data->queue_id, tx_buffer);
	}
}

/*
 * Receive a burst of packets on rx port, fragment any larger than MTU, and send resulting packets on the tx port.
 *
 * @wt_data [in]: worker thread data
 * @rx_port_id [in]: receive port id
 * @tx_port_id [in]: send port id
 */
static void ip_frag_wt_fragment(struct ip_frag_wt_data *wt_data, uint16_t rx_port_id, uint16_t tx_port_id)
{
	struct rte_mbuf *pkts[IP_FRAG_MAX_PKT_BURST];
	uint16_t pkts_cnt;

	pkts_cnt = rte_eth_rx_burst(rx_port_id, wt_data->queue_id, pkts, IP_FRAG_MAX_PKT_BURST);
	ip_frag_pkts_fragment(wt_data, rx_port_id, tx_port_id, pkts, pkts_cnt);
	rte_eth_tx_buffer_flush(tx_port_id, wt_data->queue_id, wt_data->tx_buffer);
}

/*
 * Worker thread main run loop
 *
 * @param [in]: Array of thread data structures
 * @return: 0 on success and system error code otherwise
 */
static int ip_frag_wt_thread_main(void *param)
{
	struct ip_frag_wt_data *wt_data_arr = param;
	struct ip_frag_wt_data *wt_data = &wt_data_arr[rte_lcore_id()];

	while (!force_stop) {
		switch (wt_data->cfg->mode) {
		case IP_FRAG_MODE_BIDIR:
			ip_frag_wt_reassemble(wt_data,
					      IP_FRAG_PORT_REASSEMBLE_0,
					      IP_FRAG_PORT_FRAGMENT_0,
					      IP_FRAG_PKT_DIR_UNKNOWN);
			ip_frag_wt_fragment(wt_data, IP_FRAG_PORT_FRAGMENT_0, IP_FRAG_PORT_REASSEMBLE_0);
			break;
		case IP_FRAG_MODE_MULTIPORT:
			ip_frag_wt_reassemble(wt_data,
					      IP_FRAG_PORT_REASSEMBLE_0,
					      IP_FRAG_PORT_REASSEMBLE_1,
					      IP_FRAG_PKT_DIR_0);
			ip_frag_wt_reassemble(wt_data,
					      IP_FRAG_PORT_FRAGMENT_0,
					      IP_FRAG_PORT_FRAGMENT_1,
					      IP_FRAG_PKT_DIR_1);
			ip_frag_wt_fragment(wt_data, IP_FRAG_PORT_REASSEMBLE_1, IP_FRAG_PORT_REASSEMBLE_0);
			ip_frag_wt_fragment(wt_data, IP_FRAG_PORT_FRAGMENT_1, IP_FRAG_PORT_FRAGMENT_0);
			break;
		default:
			DOCA_LOG_ERR("Unsupported application mode: %u", wt_data->cfg->mode);
			return EINVAL;
		};
	}

	return 0;
}

/*
 * Allocate and initialize ip_frag mbuf fragmentation flags
 *
 * @cfg [in]: application config
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_mbuf_flags_init(struct ip_frag_config *cfg)
{
	static const struct rte_mbuf_dynflag flag_outer_desc = {
		.name = "ip_frag outer",
	};
	static const struct rte_mbuf_dynflag flag_inner_desc = {
		.name = "ip_frag inner",
	};
	int flag_outer;
	int flag_inner;

	flag_outer = rte_mbuf_dynflag_register(&flag_outer_desc);
	if (flag_outer < 0) {
		DOCA_LOG_ERR("Failed to register mbuf outer fragmentation flag with code: %d", -flag_outer);
		return DOCA_ERROR_NO_MEMORY;
	}

	flag_inner = rte_mbuf_dynflag_register(&flag_inner_desc);
	if (flag_inner < 0) {
		DOCA_LOG_ERR("Failed to register mbuf inner fragmentation flag with code: %d", -flag_inner);
		return DOCA_ERROR_NO_MEMORY;
	}

	cfg->mbuf_flag_outer_modified = RTE_BIT64(flag_outer);
	cfg->mbuf_flag_inner_modified = RTE_BIT64(flag_inner);
	return DOCA_SUCCESS;
}

/*
 * Initialize indirect fragmentation mempools
 *
 * @nb_queues [in]: number of device queues
 * @indirect_pools [out]: Per-socket array of indirect fragmentation mempools
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_indirect_pool_init(uint16_t nb_queues, struct rte_mempool *indirect_pools[])
{
	char mempool_name[RTE_MEMPOOL_NAMESIZE];
	unsigned socket;
	unsigned lcore;

	RTE_LCORE_FOREACH_WORKER(lcore)
	{
		socket = rte_lcore_to_socket_id(lcore);

		if (!indirect_pools[socket]) {
			snprintf(mempool_name, sizeof(mempool_name), "Indirect mempool %u", socket);
			indirect_pools[socket] = rte_pktmbuf_pool_create(mempool_name,
									 NUM_MBUFS * nb_queues,
									 MBUF_CACHE_SIZE,
									 0,
									 0,
									 socket);
			if (!indirect_pools[socket]) {
				DOCA_LOG_ERR("Failed to allocate indirect mempool for socket %u", socket);
				return DOCA_ERROR_NO_MEMORY;
			}

			DOCA_LOG_DBG("Indirect mempool for socket %u initialized", socket);
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Cleanup and free FRAG fastpath data
 *
 * @wt_data_arr [in]: worker thread data array
 */
static void ip_frag_wt_data_cleanup(struct ip_frag_wt_data *wt_data_arr)
{
	struct ip_frag_wt_data *wt_data;
	unsigned lcore;

	RTE_LCORE_FOREACH_WORKER(lcore)
	{
		wt_data = &wt_data_arr[lcore];

		if (wt_data->frag_tbl)
			rte_ip_frag_table_destroy(wt_data->frag_tbl);
		rte_free(wt_data->tx_buffer);
	}

	rte_free(wt_data_arr);
}

/*
 * Allocate and initialize ip_frag worker thread data
 *
 * @cfg [in]: application config
 * @indirect_pools [in]: Per-socket array of indirect fragmentation mempools
 * @wt_data_arr_out [out]: worker thread data array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_wt_data_init(const struct ip_frag_config *cfg,
					 struct rte_mempool *indirect_pools[],
					 struct ip_frag_wt_data **wt_data_arr_out)
{
	uint16_t num_cores = rte_lcore_count() - 1;
	struct ip_frag_wt_data *wt_data_arr;
	struct ip_frag_wt_data *wt_data;
	uint16_t queue_id = 0;
	doca_error_t ret;
	unsigned lcore;

	assert(num_cores > 0);
	UNUSED(num_cores);

	wt_data_arr = rte_calloc("Worker data", RTE_MAX_LCORE, sizeof(*wt_data_arr), _Alignof(typeof(*wt_data_arr)));
	if (!wt_data_arr) {
		DOCA_LOG_ERR("Failed to allocate worker thread data array");
		return DOCA_ERROR_NO_MEMORY;
	}

	RTE_LCORE_FOREACH(lcore)
	{
		wt_data = &wt_data_arr[lcore];
		wt_data->cfg = cfg;
		wt_data->indirect_pool = indirect_pools[rte_lcore_to_socket_id(lcore)];
		wt_data->queue_id = queue_id++;

		wt_data->tx_buffer = rte_zmalloc_socket("TX buffer",
							RTE_ETH_TX_BUFFER_SIZE(IP_FRAG_MAX_PKT_BURST),
							RTE_CACHE_LINE_SIZE,
							rte_lcore_to_socket_id(lcore));
		if (!wt_data->tx_buffer) {
			DOCA_LOG_ERR("Failed to allocate worker thread tx buffer");
			ret = DOCA_ERROR_NO_MEMORY;
			goto cleanup;
		}
		rte_eth_tx_buffer_init(wt_data->tx_buffer, IP_FRAG_MAX_PKT_BURST);
		rte_eth_tx_buffer_set_err_callback(wt_data->tx_buffer,
						   rte_eth_tx_buffer_count_callback,
						   &wt_data->tx_buffer_err);

		wt_data->frag_tbl =
			rte_ip_frag_table_create(cfg->frag_tbl_size,
						 IP_FRAG_TBL_BUCKET_SIZE,
						 cfg->frag_tbl_size,
						 ((rte_get_tsc_hz() + MS_PER_S - 1) / MS_PER_S) * cfg->frag_tbl_timeout,
						 rte_lcore_to_socket_id(lcore));
		if (!wt_data->frag_tbl) {
			DOCA_LOG_ERR("Failed to allocate worker thread fragmentation table");
			ret = DOCA_ERROR_NO_MEMORY;
			goto cleanup;
		}

		DOCA_LOG_DBG("Worker thread %u data initialized", lcore);
	}

	*wt_data_arr_out = wt_data_arr;
	return DOCA_SUCCESS;

cleanup:
	ip_frag_wt_data_cleanup(wt_data_arr);
	return ret;
}

/*
 * Print SW debug counters of each worker
 *
 * @ctx [in]: Ip_frag context
 * @wt_data_arr [in]: worker thread data array
 */
static void ip_frag_sw_counters_print(struct ip_frag_ctx *ctx, struct ip_frag_wt_data *wt_data_arr)
{
	struct ip_frag_sw_counters sw_sum_port = {0};
	struct ip_frag_sw_counters sw_sum = {0};
	struct ip_frag_sw_counters *sw_counters;
	struct ip_frag_wt_data *wt_data;
	unsigned lcore;
	int port_id;

	DOCA_LOG_INFO("//////////////////// SW COUNTERS ////////////////////");

	for (port_id = 0; port_id < ctx->num_ports; port_id++) {
		DOCA_LOG_INFO("== Port %d:", port_id);
		memset(&sw_sum_port, 0, sizeof(sw_sum_port));

		RTE_LCORE_FOREACH(lcore)
		{
			wt_data = &wt_data_arr[lcore];
			sw_counters = &wt_data->sw_counters[port_id];

			DOCA_LOG_INFO(
				"Core sw %3u     frags_rx=%-8lu whole=%-8lu mtu_fits_rx=%-8lu mtu_exceed_rx=%-8lu frags_gen=%-8lu err=%-8lu",
				lcore,
				sw_counters->frags_rx,
				sw_counters->whole,
				sw_counters->mtu_fits_rx,
				sw_counters->mtu_exceed_rx,
				sw_counters->frags_gen,
				sw_counters->err);

			sw_sum_port.frags_rx += sw_counters->frags_rx;
			sw_sum_port.whole += sw_counters->whole;
			sw_sum_port.mtu_fits_rx += sw_counters->mtu_fits_rx;
			sw_sum_port.mtu_exceed_rx += sw_counters->mtu_exceed_rx;
			sw_sum_port.frags_gen += sw_counters->frags_gen;
			sw_sum_port.err += sw_counters->err;
		}

		DOCA_LOG_INFO(
			"TOTAL sw port %d frags_rx=%-8lu whole=%-8lu mtu_fits_rx=%-8lu mtu_exceed_rx=%-8lu frags_gen=%-8lu err=%-8lu",
			port_id,
			sw_sum_port.frags_rx,
			sw_sum_port.whole,
			sw_sum_port.mtu_fits_rx,
			sw_sum_port.mtu_exceed_rx,
			sw_sum_port.frags_gen,
			sw_sum_port.err);

		sw_sum.frags_rx += sw_sum_port.frags_rx;
		sw_sum.whole += sw_sum_port.whole;
		sw_sum.mtu_fits_rx += sw_sum_port.mtu_fits_rx;
		sw_sum.mtu_exceed_rx += sw_sum_port.mtu_exceed_rx;
		sw_sum.frags_gen += sw_sum_port.frags_gen;
		sw_sum.err += sw_sum_port.err;
	}

	DOCA_LOG_INFO("== Total:");
	DOCA_LOG_INFO(
		"TOTAL sw        frags_rx=%-8lu whole=%-8lu mtu_fits_rx=%-8lu mtu_exceed_rx=%-8lu frags_gen=%-8lu err=%-8lu",
		sw_sum.frags_rx,
		sw_sum.whole,
		sw_sum.mtu_fits_rx,
		sw_sum.mtu_exceed_rx,
		sw_sum.frags_gen,
		sw_sum.err);
}

/*
 * Print TX buffer errors counter of each worker
 *
 * @wt_data_arr [in]: worker thread data array
 */
static void ip_frag_tx_buffer_error_print(struct ip_frag_wt_data *wt_data_arr)
{
	struct ip_frag_wt_data *wt_data;
	uint64_t sum = 0;
	unsigned lcore;

	DOCA_LOG_INFO("//////////////////// TX BUFFER ERROR ////////////////////");

	RTE_LCORE_FOREACH(lcore)
	{
		wt_data = &wt_data_arr[lcore];

		DOCA_LOG_INFO("Core tx_buffer %3u err=%lu", lcore, wt_data->tx_buffer_err);

		sum += wt_data->tx_buffer_err;
	}

	DOCA_LOG_INFO("TOTAL tx_buffer    err=%lu", sum);
}

/*
 * Print SW debug counters of each worker
 *
 * @wt_data_arr [in]: worker thread data array
 */
static void ip_frag_tbl_stats_print(struct ip_frag_wt_data *wt_data_arr)
{
	struct ip_frag_wt_data *wt_data;
	unsigned lcore;

	DOCA_LOG_INFO("//////////////////// FRAG TABLE STATS ////////////////////");

	RTE_LCORE_FOREACH(lcore)
	{
		wt_data = &wt_data_arr[lcore];

		DOCA_LOG_INFO("Core %3u:", lcore);
		rte_ip_frag_table_statistics_dump(stdout, wt_data->frag_tbl);
	}
}
/*
 * Print debug counters of each worker thread
 *
 * @ctx [in]: Ip_frag context
 * @wt_data_arr [in]: worker thread data array
 */
static void ip_frag_debug_counters_print(struct ip_frag_ctx *ctx, struct ip_frag_wt_data *wt_data_arr)
{
	DOCA_LOG_INFO("");
	rte_mbuf_dyn_dump(stdout);
	DOCA_LOG_INFO("");
	ip_frag_tbl_stats_print(wt_data_arr);
	DOCA_LOG_INFO("");
	ip_frag_tx_buffer_error_print(wt_data_arr);
	DOCA_LOG_INFO("");
	ip_frag_sw_counters_print(ctx, wt_data_arr);
	DOCA_LOG_INFO("");
}

/*
 * Create a flow pipe
 *
 * @pipe_cfg [in]: Ip_Frag pipe configuration
 * @pipe [out]: pointer to store the created pipe at
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_pipe_create(struct ip_frag_pipe_cfg *pipe_cfg, struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg *cfg;
	doca_error_t result;

	result = doca_flow_pipe_cfg_create(&cfg, pipe_cfg->port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(cfg, pipe_cfg->name, DOCA_FLOW_PIPE_BASIC, pipe_cfg->is_root);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_cfg_set_domain(cfg, pipe_cfg->domain);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg domain: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_cfg_set_nr_entries(cfg, pipe_cfg->num_entries);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg num_entries: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	if (pipe_cfg->match != NULL) {
		result = doca_flow_pipe_cfg_set_match(cfg, pipe_cfg->match, pipe_cfg->match_mask);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
			goto destroy_pipe_cfg;
		}
	}

	result = doca_flow_pipe_create(cfg, pipe_cfg->fwd, pipe_cfg->fwd_miss, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create IP_FRAG pipe: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(cfg);
	return result;
}

/*
 * Create RSS pipe for each port
 *
 * @ctx [in] Ip_Frag context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_rss_pipes_create(struct ip_frag_ctx *ctx)
{
	uint16_t rss_queues[RTE_MAX_LCORE];
	struct doca_flow_match match = {0};
	char *pipe_name = "RSS_PIPE";
	const int num_of_entries = 1;
	struct doca_flow_fwd fwd = {.type = DOCA_FLOW_FWD_RSS,
				    .rss.queues_array = rss_queues,
				    .rss.nr_queues = ctx->num_queues,
				    .rss.outer_flags = DOCA_FLOW_RSS_IPV4,
				    .rss.rss_hash_func = DOCA_FLOW_RSS_HASH_FUNCTION_SYMMETRIC_TOEPLITZ};
	struct ip_frag_pipe_cfg pipe_cfg = {.domain = DOCA_FLOW_PIPE_DOMAIN_DEFAULT,
					    .name = pipe_name,
					    .is_root = true,
					    .num_entries = num_of_entries,
					    .match = &match,
					    .match_mask = NULL,
					    .fwd = &fwd,
					    .fwd_miss = NULL};
	struct entries_status status;
	doca_error_t ret;
	int port_id;
	int i;

	for (i = 0; i < ctx->num_queues; ++i)
		rss_queues[i] = i;

	for (port_id = 0; port_id < ctx->num_ports; port_id++) {
		pipe_cfg.port_id = port_id;
		pipe_cfg.port = ctx->ports[port_id];
		memset(&status, 0, sizeof(status));

		ret = ip_frag_pipe_create(&pipe_cfg, &ctx->pipes[port_id]);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create rss pipe: %s", doca_error_get_descr(ret));
			return ret;
		}

		ret = doca_flow_pipe_add_entry(0, ctx->pipes[port_id], NULL, NULL, NULL, NULL, 0, &status, NULL);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add rss entry: %s", doca_error_get_descr(ret));
			return ret;
		}

		ret = doca_flow_entries_process(ctx->ports[port_id], 0, DEFAULT_TIMEOUT_US, num_of_entries);
		if (ret != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries on port %u: %s", port_id, doca_error_get_descr(ret));
			return ret;
		}

		if (status.nb_processed != num_of_entries || status.failure) {
			DOCA_LOG_ERR("Failed to process port %u entries", port_id);
			return DOCA_ERROR_BAD_STATE;
		}
	}

	return DOCA_SUCCESS;
}

doca_error_t ip_frag(struct ip_frag_config *cfg, struct application_dpdk_config *dpdk_cfg)
{
	struct rte_mempool *indirect_pools[RTE_MAX_NUMA_NODES] = {NULL};
	uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
	struct ip_frag_ctx ctx = {
		.num_ports = dpdk_cfg->port_config.nb_ports,
		.num_queues = dpdk_cfg->port_config.nb_queues,
	};
	uint32_t actions_mem_size[RTE_MAX_ETHPORTS];
	struct ip_frag_wt_data *wt_data_arr;
	struct flow_resources resource = {0};
	doca_error_t ret;

	ret = init_doca_flow(ctx.num_queues, "vnf,hws", &resource, nr_shared_resources);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(ret));
		return ret;
	}

	ret = ip_frag_mbuf_flags_init(cfg);
	if (ret != DOCA_SUCCESS)
		goto cleanup_doca_flow;

	ret = ip_frag_indirect_pool_init(ctx.num_queues, indirect_pools);
	if (ret != DOCA_SUCCESS)
		goto cleanup_doca_flow;

	ret = ip_frag_wt_data_init(cfg, indirect_pools, &wt_data_arr);
	if (ret != DOCA_SUCCESS)
		goto cleanup_doca_flow;

	ret = init_doca_flow_ports(ctx.num_ports, ctx.ports, false, ctx.dev_arr, actions_mem_size);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(ret));
		goto cleanup_wt_data;
	}

	ret = ip_frag_rss_pipes_create(&ctx);
	if (ret != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to pipes: %s", doca_error_get_descr(ret));
		goto cleanup_ports;
	}

	DOCA_LOG_INFO("Initialization finished, starting data path");
	if (rte_eal_mp_remote_launch(ip_frag_wt_thread_main, wt_data_arr, CALL_MAIN)) {
		DOCA_LOG_ERR("Failed to launch worker threads");
		goto cleanup_ports;
	}
	rte_eal_mp_wait_lcore();

	ip_frag_debug_counters_print(&ctx, wt_data_arr);
cleanup_ports:
	ret = stop_doca_flow_ports(ctx.num_ports, ctx.ports);
	if (ret != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to stop doca flow ports: %s", doca_error_get_descr(ret));
cleanup_wt_data:
	ip_frag_wt_data_cleanup(wt_data_arr);
cleanup_doca_flow:
	doca_flow_destroy();
	return ret;
}
