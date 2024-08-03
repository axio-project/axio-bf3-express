/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <pthread.h>
#include <stdlib.h>

#include <rte_malloc.h>
#include <rte_sft.h>
#include <rte_net.h>

#include <doca_buf_inventory.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_dpdk.h>

#include "dpi_worker.h"
#include "dpdk_utils.h"
#include "offload_rules.h"

DOCA_LOG_REGISTER(DWRKR);

#define BURST_SIZE 1024			/* Receive burst size */
#define NETFLOW_UPDATE_RATE 2000	/* Send netflow update every 2000 packet */
#define RECEIVER (0x0)			/* Receiver index */
#define INITIATOR (0x1)			/* Initiator index */
#define SFT_ZONE 0xCAFE			/* zone for sft (arbitrary value) */
#define SFT_FLOW_INFO_CLIENT_ID 0xF	/* Flow info client id (arbitrary value) */
#define MEMPOOL_NAME_MAX_LEN 100	/* The mempool name maximum byte length */

/* Buffers packet for future transmission */
#define TX_BUFFER_PKT(m, ctx) rte_eth_tx_buffer(m->port ^ 1, ctx->queue_id, ctx->tx_buffer[m->port ^ 1], m)

#define DPI_WORKER_ALWAYS_INLINE inline __attribute__((always_inline))

pthread_mutex_t log_lock;
volatile bool force_quit;
volatile bool kill_workers;

struct flow_info {
	uint32_t	sig_id;				/* Signature id that was matched for this flow */
	uint64_t	scanned_bytes[2];		/* Scanned bytes for each direction */
	uint8_t		state;				/* State of the flow (SFT state) */
	struct doca_dpi_flow_ctx *dpi_flow_ctx;		/* DPI flow context */
	struct doca_telemetry_netflow_record record[2];	/* 1 - initiator, 0 - Receiver */
};

/* Per worker context */
struct worker_ctx {
	uint8_t		queue_id;				/* Queue id */
	uint16_t	ingress_port;				/* Current ingress port */
	uint64_t	dropped_packets;			/* Packets that failed to transmit */
	uint64_t	processed_packets;			/* Packets that were processed by this worker */
	struct		dpi_worker_attr attr;			/* DPI attributes */
	struct rte_eth_dev_tx_buffer *tx_buffer[SFT_PORTS_NUM];	/* Transmit buffers */
	struct doca_workq *workq;				/* The workq attached to dpi_ctx */
	struct rte_mempool *meta_mempool;			/* The pre-allocated job meta data buffer */
	struct doca_buf_inventory *buf_inventory;		/* DOCA buf_inventory for dpdk_bridge */
	struct doca_dpdk_mempool *doca_dpdk_pool;		/* DOCA doca_buf pool for dpdk_bridge */
};

/* A helper struct for sending a job */
struct dpi_job_meta_data {
	struct flow_info *flow;		/* The flow context for a DPI job */
	struct rte_mbuf *mbuf;		/* The rte_mbuf intended to send by a DPI job */
	struct doca_dpi_result result;	/* The result for a DPI job */
};

doca_error_t
dpi_job_is_supported(struct doca_devinfo *devinfo)
{
	return doca_dpi_job_get_supported(devinfo, DOCA_DPI_JOB);
}

doca_error_t
doca_dpi_worker_ctx_create(struct doca_dpi_worker_ctx **dpi_ctx,
			   uint16_t max_sig_match_len,
			   uint32_t per_workq_packet_pool_size,
			   struct doca_dev *dev,
			   const char *sig_file_path)
{
	doca_error_t result;
	struct doca_dpi_worker_ctx *ctx = NULL;

	if (dpi_ctx == NULL || dev == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	ctx = (struct doca_dpi_worker_ctx *)calloc(1, sizeof(struct doca_dpi_worker_ctx));
	if (ctx == NULL) {
		DOCA_LOG_ERR("Failure to allocate doca_dpi_worker!");
		*dpi_ctx = NULL;
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Create doca_dpi instance */
	result = doca_dpi_create(&ctx->dpi);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create doca_dpi instance. err = [%s]",
				doca_get_error_string(result));
		goto dpi_create_failure;
	}

	/* Add doca_dev into doca_dpi instance */
	ctx->dev = dev;
	result = doca_ctx_dev_add(doca_dpi_as_ctx(ctx->dpi), ctx->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register device with dpi. err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Load signatures into doca_dpi's backend device */
	if (sig_file_path != NULL) {
		result = doca_dpi_set_signatures(ctx->dpi, sig_file_path);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Loading DPI signatures failed, err = [%s]",
					doca_get_error_string(result));
			goto dpi_destroy;
		}
	}

	/* Set workq packet pool size of each workq */
	result = doca_dpi_set_per_workq_packet_pool_size(ctx->dpi, per_workq_packet_pool_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Set per_workq_packet_pool_size failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Set workq max num of flows for each workq
	 * this num is set according to max num of packets so each flow can have enough packets
	 * for sig identification
	 */
	result = doca_dpi_set_per_workq_max_flows(ctx->dpi, per_workq_packet_pool_size/MAX_MBUFS_FOR_FLOW);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Set per_workq_packet_pool_size failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Set max_sig_match_len */
	result = doca_dpi_set_max_sig_match_len(ctx->dpi, max_sig_match_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Set max_sig_match_len failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Start doca_dpi */
	result = doca_ctx_start(doca_dpi_as_ctx(ctx->dpi));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start doca_dpi, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	*dpi_ctx = ctx;
	return result;

dpi_destroy:
	doca_ctx_stop(doca_dpi_as_ctx(ctx->dpi));
	doca_dpi_destroy(ctx->dpi);
	ctx->dpi = NULL;
	doca_dev_close(ctx->dev);
	ctx->dev = NULL;
dpi_create_failure:
	free(ctx);
	*dpi_ctx = NULL;
	return result;
}

void
doca_dpi_worker_ctx_destroy(struct doca_dpi_worker_ctx *ctx)
{
	if (ctx == NULL)
		return;

	if (ctx->dpi != NULL) {
		doca_ctx_stop(doca_dpi_as_ctx(ctx->dpi));
		doca_dpi_destroy(ctx->dpi);
		ctx->dpi = NULL;
	}
	if (ctx->dev != NULL) {
		doca_dev_close(ctx->dev);
		ctx->dev = NULL;
	}
	free(ctx);
}

/*
 * Calculates the L7 offset for the given packet.
 *
 * @packet [in]: Packet to calculate the L7 offset for.
 * @return: L7 offset.
 */
static DPI_WORKER_ALWAYS_INLINE uint32_t
get_payload_offset(const struct rte_mbuf *packet)
{
	struct rte_net_hdr_lens headers = {0};

	rte_net_get_ptype(packet, &headers, RTE_PTYPE_ALL_MASK);
	return headers.l2_len + headers.l3_len + headers.l4_len;
}

/*
 * Try to send netflow record to the netflow buffer
 *
 * @flow [in]: Flow to send
 * @ctx [in]: Worker context
 * @initiator [in]: 0x1 if the flow is initiator, 0x0 otherwise
 */
static DPI_WORKER_ALWAYS_INLINE void
set_netflow_record(struct flow_info *flow, const struct worker_ctx *ctx, const uint8_t initiator)
{
	struct doca_telemetry_netflow_record *record_to_send;

	if (ctx->attr.send_netflow_record == NULL)
		return;

	record_to_send = &flow->record[!!initiator];
	record_to_send->last = time(0);
	ctx->attr.send_netflow_record(record_to_send);
	/* Only the difference is relevant between Netflow interactions */
	record_to_send->d_pkts = 0;
	record_to_send->d_octets = 0;
}

/*
 * The reverse_stpl takes a 7 tuple as an input and reverses it.
 * 5-tuple reversal is ordinary while the zone stays the same for both
 * directions. The last piece of the 7-tuple is the port which is also reversed.
 *
 * @stpl [in]: 7-tuple to reverse
 * @rstpl [out]: Reversed 7-tuple
 */
static DPI_WORKER_ALWAYS_INLINE void
reverse_stpl(const struct rte_sft_7tuple *stpl, struct rte_sft_7tuple *rstpl)
{
	memset(rstpl, 0, sizeof(*rstpl));
	rstpl->flow_5tuple.is_ipv6 = stpl->flow_5tuple.is_ipv6;
	rstpl->flow_5tuple.proto = stpl->flow_5tuple.proto;
	if (rstpl->flow_5tuple.is_ipv6) {
		memcpy(&rstpl->flow_5tuple.ipv6.src_addr[0], &stpl->flow_5tuple.ipv6.dst_addr[0], 16);
		memcpy(&rstpl->flow_5tuple.ipv6.dst_addr[0], &stpl->flow_5tuple.ipv6.src_addr[0], 16);
	} else {
		rstpl->flow_5tuple.ipv4.src_addr = stpl->flow_5tuple.ipv4.dst_addr;
		rstpl->flow_5tuple.ipv4.dst_addr = stpl->flow_5tuple.ipv4.src_addr;
	}
	rstpl->flow_5tuple.src_port = stpl->flow_5tuple.dst_port;
	rstpl->flow_5tuple.dst_port = stpl->flow_5tuple.src_port;
	rstpl->zone = stpl->zone;
	rstpl->port_id = stpl->port_id ^ 1;
}

/*
 * Set L4 fields needed by DPI
 *
 * @mbuf_info [in]: mbuf info to parse from
 * @parsing_info [in]: Parsing info to set L4 fields for
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static DPI_WORKER_ALWAYS_INLINE doca_error_t
set_l4_parsing_info(const struct rte_sft_mbuf_info *mbuf_info, struct doca_dpi_parsing_info *parsing_info)
{
	parsing_info->ethertype = rte_cpu_to_be_16(mbuf_info->eth_type);
	parsing_info->l4_protocol = mbuf_info->l4_protocol;

	if (!mbuf_info->is_ipv6)
		parsing_info->dst_ip.ipv4.s_addr = mbuf_info->ip4->dst_addr;
	else
		memcpy(&parsing_info->dst_ip.ipv6, &mbuf_info->ip6->dst_addr[0], 16);
	if (parsing_info->l4_protocol == IPPROTO_UDP) {
		parsing_info->l4_sport = mbuf_info->udp->src_port;
		parsing_info->l4_dport = mbuf_info->udp->dst_port;
	} else if (parsing_info->l4_protocol == IPPROTO_TCP) {
		parsing_info->l4_sport = mbuf_info->tcp->src_port;
		parsing_info->l4_dport = mbuf_info->tcp->dst_port;
	} else {
		DOCA_DLOG_DBG("Unsupported L4 protocol!");
		return DOCA_ERROR_NOT_SUPPORTED;
	}
	return DOCA_SUCCESS;
}

/*
 * Initialize the flow_info structure to be associated with the flow
 *
 * @fid [in]: Flow ID to initialize
 * @dpi_flow_ctx [in]: DPI flow context to initialize the flow with
 * @ctx [in]: Worker context
 * @five_tuple [in]: 5-tuple of the flow
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
client_obj_flow_info_create(uint32_t fid,
			struct doca_dpi_flow_ctx *dpi_flow_ctx,
			const struct worker_ctx *ctx,
			const struct rte_sft_5tuple *five_tuple)
{
	struct flow_info *flow = (struct flow_info *)rte_zmalloc(NULL, sizeof(*flow), 0);
	struct rte_sft_error error;

	if (flow == NULL)
		return DOCA_ERROR_NO_MEMORY;

	strcpy(flow->record[INITIATOR].application_name, "NO_MATCH");
	flow->record[INITIATOR].flow_id = fid;
	if (!five_tuple->is_ipv6) {
		flow->record[INITIATOR].src_addr_v4 = five_tuple->ipv4.src_addr;
		flow->record[INITIATOR].dst_addr_v4 = five_tuple->ipv4.dst_addr;
	} else {
		memcpy(&flow->record[INITIATOR].src_addr_v6, five_tuple->ipv6.src_addr, 16);
		memcpy(&flow->record[INITIATOR].dst_addr_v6, five_tuple->ipv6.dst_addr, 16);
	}
	flow->record[INITIATOR].src_port = five_tuple->src_port;
	flow->record[INITIATOR].dst_port = five_tuple->dst_port;
	flow->record[INITIATOR].protocol = five_tuple->proto;
	flow->record[INITIATOR].input = ctx->ingress_port;
	flow->record[INITIATOR].output = ctx->ingress_port ^ 1;
	flow->record[INITIATOR].first = time(0);
	flow->record[INITIATOR].last = time(0);

	strcpy(flow->record[RECEIVER].application_name, "NO_MATCH");
	flow->record[RECEIVER].flow_id = fid;
	if (!five_tuple->is_ipv6) {
		flow->record[RECEIVER].src_addr_v4 = five_tuple->ipv4.dst_addr;
		flow->record[RECEIVER].dst_addr_v4 = five_tuple->ipv4.src_addr;
	} else {
		memcpy(&flow->record[RECEIVER].src_addr_v6, five_tuple->ipv6.dst_addr, 16);
		memcpy(&flow->record[RECEIVER].dst_addr_v6, five_tuple->ipv6.src_addr, 16);
	}
	flow->record[RECEIVER].src_port = five_tuple->dst_port;
	flow->record[RECEIVER].dst_port = five_tuple->src_port;
	flow->record[RECEIVER].protocol = five_tuple->proto;
	flow->record[RECEIVER].input = ctx->ingress_port ^ 1;
	flow->record[RECEIVER].output = ctx->ingress_port;
	flow->record[RECEIVER].first = time(0);
	flow->record[RECEIVER].last = time(0);

	if (rte_sft_flow_set_client_obj(ctx->queue_id, fid, SFT_FLOW_INFO_CLIENT_ID, flow, &error) != 0) {
		rte_free(flow);
		return DOCA_ERROR_DRIVER;
	}
	flow->dpi_flow_ctx = dpi_flow_ctx;
	set_netflow_record(flow, ctx, INITIATOR); /* First packet is initiator */
	return DOCA_SUCCESS;
}

/*
 * Update the flow_info structure with signature name that was matched
 *
 * @app_name [in]: Signature name that was matched
 * @sig_id [in]: Signature ID
 * @data [in]: Flow info structure to update
 */
static void
client_obj_flow_info_set(const char *app_name, uint32_t sig_id, struct flow_info *data)
{
	assert(data != NULL);
	data->sig_id = sig_id;
	memcpy(data->record[0].application_name, app_name, 64);
	memcpy(data->record[1].application_name, app_name, 64);
}

/*
 * Get SFT state according to the user-defined function
 *
 * @ctx [in]: Worker context
 * @dpi_result [in]: DPI result containing match information
 * @fid [in]: Flow ID of the matched flow
 * @return: SFT state
 */
static enum SFT_USER_STATE
get_sft_state_from_match(const struct worker_ctx *ctx, const struct doca_dpi_result *dpi_result, uint32_t fid)
{
	enum dpi_worker_action dpi_action;

	if (ctx->attr.dpi_on_match == NULL)
		return RSS_FLOW;

	if (ctx->attr.dpi_on_match(dpi_result, fid, ctx->attr.user_data, &dpi_action) != 0)
		return RSS_FLOW;
	switch (dpi_action) {
	case DPI_WORKER_ALLOW:
		return HAIRPIN_MATCHED_FLOW;
	case DPI_WORKER_DROP:
		return DROP_FLOW;
	default:
		return RSS_FLOW;
	}
}

/*
 * Update Netflow record counters
 *
 * @ctx [in]: Worker context
 * @flow [in]: Flow info structure to update
 * @packet [in]: Packet that processed by the worker
 * @initiator [in]: 0x1 for initiator, 0x0 for RECEIVER
 */
static void
update_record_counters(const struct worker_ctx *ctx, struct flow_info *flow,
			const struct rte_mbuf *packet, const uint8_t initiator)
{
	flow->record[!!initiator].d_pkts++;
	flow->record[!!initiator].d_octets += rte_pktmbuf_pkt_len(packet);

	/* Every predefined number of packets, we send a Netflow record */
	if (flow->record[initiator].d_pkts % NETFLOW_UPDATE_RATE == 0)
		set_netflow_record(flow, ctx, initiator);
}

/*
 * Called on DPI match, relevant flow's state is updated and Netflow records are sent
 *
 * @flow [in]: Flow info structure associated with the matched flow
 * @result [in]: DPI result containing match information
 * @ctx [in]: Worker context
 */
static void
resolve_dpi_match(struct flow_info *flow, const struct doca_dpi_result *result, const struct worker_ctx *ctx)
{
	uint32_t fid = flow->record[RECEIVER].flow_id;
	struct rte_sft_error error;
	struct doca_dpi_sig_data sig_data = {0};

	DOCA_DLOG_DBG("FID %u matches sig_id %d", fid, result->info.sig_id);

	flow->state =  get_sft_state_from_match(ctx, result, fid);

	if (rte_sft_flow_set_state(ctx->queue_id, fid, flow->state, &error) != 0)
		return;

	if (doca_dpi_get_signature(ctx->attr.dpi_ctx->dpi, result->info.sig_id, &sig_data) != DOCA_SUCCESS)
		return;

	client_obj_flow_info_set(sig_data.name, result->info.sig_id, flow);
	/* Update match for both Netflow directions */
	set_netflow_record(flow, ctx, INITIATOR);
	set_netflow_record(flow, ctx, RECEIVER);
}

/*
 * Destroys DPI flow context
 *
 * @flow [in]: Flow info structure to destroy
 * @ctx [in]: Worker context
 */
static void
resolve_dpi_destroy(struct flow_info *flow, const struct worker_ctx *ctx)
{
	set_netflow_record(flow, ctx, INITIATOR);
	set_netflow_record(flow, ctx, RECEIVER);
	/* In some scenarios it is possible to have a SFT flow without DPI flow */
	if (flow->dpi_flow_ctx == NULL)
		return;
	doca_dpi_flow_destroy(flow->dpi_flow_ctx);
	DOCA_DLOG_DBG("DPI FID %llu was destroyed", flow->record[0].flow_id);
	flow->dpi_flow_ctx = NULL;
	rte_free(flow);
}

/*
 * Retrieve and destroy aged flows
 *
 * @ctx [in]: Worker context
 */
static void
clear_aged_flows(const struct worker_ctx *ctx)
{
	int aged_flows, fid_index;
	uint32_t fid;
	uint32_t *fid_list = NULL;
	struct flow_info *flow = NULL;
	struct rte_sft_error error;
	/* if nb_fids is 0, return the number of all aged out SFT flows */
	aged_flows = rte_sft_flow_get_aged_flows(ctx->queue_id, fid_list, /* nb_fids */ 0, &error);
	if (aged_flows <= 0)
		return;
	fid_list = (uint32_t *)rte_zmalloc(NULL, sizeof(uint32_t) * aged_flows, 0);
	if (unlikely(fid_list == NULL))
		return;
	/* if nb_fids is not 0 , return the number of aged out flows - IT HAS TO BE EQUAL */
	if (rte_sft_flow_get_aged_flows(ctx->queue_id, fid_list, aged_flows, &error) < 0)
		return;
	for (fid_index = 0; fid_index < aged_flows; fid_index++) {
		fid = fid_list[fid_index];
		DOCA_DLOG_DBG("FID %u will be removed due to aging", fid);
		flow = (struct flow_info *)rte_sft_flow_get_client_obj(ctx->queue_id, fid, SFT_FLOW_INFO_CLIENT_ID, &error);
		assert(flow != NULL);
		resolve_dpi_destroy(flow, ctx);
		if (rte_sft_flow_destroy(ctx->queue_id, fid, &error) != 0)
			DOCA_LOG_ERR("FID %u destroy failed", fid);
	}
	rte_free(fid_list);
}

/*
 * Phase 2 of flow creation, this function should be called only after rte_sft_process_mbuf()
 *
 * @packet [in]: Packet to process
 * @ctx [in]: Worker context
 * @sft_status [in/out]: SFT state of the flow
 * @sft_packet [out]: Packet received by the SFT
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
activate_new_connection(struct rte_mbuf *packet,
			const struct worker_ctx *ctx,
			struct rte_sft_flow_status *sft_status,
			struct rte_mbuf **sft_packet)
{
	int ret;
	struct doca_dpi_flow_ctx *dpi_flow_ctx;
	struct rte_sft_7tuple stpl, rstpl;
	struct rte_sft_error error;
	struct rte_sft_mbuf_info mbuf_info = {0};
	struct doca_dpi_parsing_info parsing_info = {0};
	uint32_t data = 0xdeadbeef;
	const uint8_t queue_id = ctx->queue_id;
	const uint16_t port_id = ctx->ingress_port;
	const uint32_t default_state = 0;
	const uint8_t device_id = 0;
	const uint8_t enable_proto_state = 1;
	const struct rte_sft_actions_specs sft_action = {
						.actions = RTE_SFT_ACTION_AGE | RTE_SFT_ACTION_COUNT,
						.initiator_nat = NULL,
						.reverse_nat = NULL,
						.aging = 0};
	doca_error_t result;
	struct doca_workq *workq = ctx->workq;

	if (unlikely(!sft_status->zone_valid)) {
		ret = rte_sft_process_mbuf_with_zone(queue_id, packet, SFT_ZONE, sft_packet, sft_status, &error);
		if (unlikely(ret < 0))
			return DOCA_ERROR_DRIVER;
	}

	if (!sft_status->activated) {
		ret = rte_sft_parse_mbuf(packet, &mbuf_info, NULL, &error);
		if (unlikely(ret < 0))
			return DOCA_ERROR_DRIVER;
		rte_sft_mbuf_stpl(packet, &mbuf_info, sft_status->zone, &stpl, &error);
		reverse_stpl(&stpl, &rstpl);
		ret = rte_sft_flow_activate(queue_id,
					    SFT_ZONE,		/* Fixed zone */
					    packet,
					    &rstpl,
					    default_state,	/* Default state = 0 */
					    &data,
					    enable_proto_state, /* always maintain protocol state */
					    &sft_action,
					    device_id,
					    port_id,
					    sft_packet,
					    sft_status,
					    &error);
		if (unlikely(ret < 0)) {
			rte_pktmbuf_free(packet);
			return DOCA_ERROR_DRIVER;
		}

		/* Flow activated at this point */
		assert(sft_status->activated);
		if (unlikely(*sft_packet == NULL)) {
			result = DOCA_ERROR_DRIVER;
			goto flow_destroy;
		}
		DOCA_DLOG_DBG("Flow activated");

		if (unlikely(sft_status->proto_state == SFT_CT_STATE_ERROR)) {
			result = DOCA_ERROR_DRIVER;
			goto flow_destroy;
		}
		if (rte_sft_flow_get_client_obj(queue_id, sft_status->fid, SFT_FLOW_INFO_CLIENT_ID, &error) == NULL) {
			result = set_l4_parsing_info(&mbuf_info, &parsing_info);
			if (unlikely(result != DOCA_SUCCESS))
				goto flow_destroy;

			result = doca_dpi_flow_create(ctx->attr.dpi_ctx->dpi, workq,
						&parsing_info, &dpi_flow_ctx);
			if (unlikely(result != DOCA_SUCCESS))
				goto flow_destroy;

			result = client_obj_flow_info_create(sft_status->fid, dpi_flow_ctx, ctx, &stpl.flow_5tuple);

			if (unlikely(result != DOCA_SUCCESS)) {
				doca_dpi_flow_destroy(dpi_flow_ctx);
				goto flow_destroy;
			}
		}
		DOCA_DLOG_DBG("New flow activated (fid=%u)", sft_status->fid);
	}
	return DOCA_SUCCESS;

flow_destroy:
	if (rte_sft_flow_destroy(queue_id, sft_status->fid, &error) != 0)
		DOCA_LOG_ERR("FID %u destroy failed", sft_status->fid);
	return result;
}

/*
 * Dequeue packets from the DPI engine till the queue is empty
 * Each packet dequeued is checked for signature matches, if any, the flow's state is updated
 * Packets which match 'DROP' signature are dropped
 * All other packets are buffered for later transmission
 *
 * @ctx [in]: Worker context
 */
static void
dequeue_from_dpi(const struct worker_ctx *ctx)
{
	doca_error_t result;
	struct doca_dpi_result *res = NULL;
	struct doca_event event = {0};
	struct doca_workq *workq = ctx->workq;
	struct rte_mempool *mempool = ctx->meta_mempool;
	struct rte_mbuf *pkt;
	struct dpi_job_meta_data *job_meta;

	result = doca_workq_progress_retrieve(workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);

	while (result == DOCA_SUCCESS) {
		res = (struct doca_dpi_result *)(event.result.ptr);
		job_meta = (struct dpi_job_meta_data *)(event.user_data.ptr);
		pkt = job_meta->mbuf;
		if (unlikely(res->status_flags & DOCA_DPI_STATUS_DESTROYED || event.user_data.ptr == NULL))
			goto skip_packet;
		if (likely(!res->matched))
			TX_BUFFER_PKT(pkt, ctx);
		else {
			if (res->info.action != DOCA_DPI_SIG_ACTION_DROP)
				TX_BUFFER_PKT(pkt, ctx);
			if (res->status_flags & DOCA_DPI_STATUS_NEW_MATCH)
				resolve_dpi_match(job_meta->flow, res, ctx);
		}
skip_packet:
		doca_buf_refcount_rm((struct doca_buf *)(res->pkt), NULL);
		rte_mempool_put(mempool, (void *)job_meta);
		result = doca_workq_progress_retrieve(workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
	}
}

/*
 * Enqueue a packet to the DPI engine
 * Each enqueued packet is checked for signature matches and results are retrieved in dequeue_from_dpi()
 * Empty packets are not enqueued and buffered for later transmission
 * If the DPI signatures are not loaded to DPI engine, no packets are enqueued and buffered for later transmission
 *
 * @sft_packet [in]: Packet to be enqueued
 * @flow [in]: Flow info associated with the packet's flow
 * @sft_status [in]: SFT status associated with the flow
 * @ctx [in]: Worker context
 */
static void
enqueue_packet_to_dpi(struct rte_mbuf *sft_packet,
		      struct flow_info *flow,
		      const struct rte_sft_flow_status *sft_status,
		      const struct worker_ctx *ctx)
{
	doca_error_t result;
	uint32_t payload_offset;
	const uint64_t max_dpi_depth = ctx->attr.max_dpi_depth;
	struct rte_sft_error sft_error;
	struct rte_mempool *mempool = ctx->meta_mempool;
	struct doca_workq *workq = ctx->workq;
	struct dpi_job_meta_data *job_meta = NULL;
	struct doca_buf *pkt_doca_buf = NULL;

	if (unlikely(flow->dpi_flow_ctx == NULL)) {
		rte_pktmbuf_free(sft_packet);
		return;
	}

	payload_offset = get_payload_offset(sft_packet);

	/*
	 * Try to get a meta buffer.
	 * If there is no meta buffer, call dequeue_from_dpi(), until we can get one.
	 */
	while (rte_mempool_get(mempool, (void **)&job_meta) != 0)
		dequeue_from_dpi(ctx);

	/* Convert rte_mbuf to doca_buf */
	result = doca_dpdk_mempool_mbuf_to_buf(ctx->doca_dpdk_pool, ctx->buf_inventory, sft_packet, &pkt_doca_buf);
	if (result != DOCA_SUCCESS) {
		rte_mempool_put(mempool, job_meta);
		return;
	}

	/* Record meta data */
	job_meta->flow = flow;
	job_meta->mbuf = sft_packet;

	/* Create a DPI job */
	struct doca_dpi_job job = (struct doca_dpi_job) {
		.base.type = DOCA_DPI_JOB,
		.base.flags = DOCA_JOB_FLAGS_NONE,
		.base.ctx = doca_dpi_as_ctx(ctx->attr.dpi_ctx->dpi),
		.base.user_data.ptr = (void *)job_meta,
		.pkt = pkt_doca_buf,
		.initiator = sft_status->initiator,
		.payload_offset = payload_offset,
		.flow_ctx = flow->dpi_flow_ctx,
		.result = &job_meta->result
	};

	result = doca_workq_submit(workq, &(job.base));
	if (result == DOCA_ERROR_INVALID_VALUE) {
		/* No signatures loaded */
		doca_buf_refcount_rm(pkt_doca_buf, NULL);
		rte_mempool_put(mempool, job_meta);
		goto forward_packet;
	}

	while (result == DOCA_ERROR_NO_MEMORY) { /* If the DPI is busy, dequeue until we successfully enqueue the packet */
		dequeue_from_dpi(ctx);
		result = doca_workq_submit(workq, &(job.base));
	}

	/* Netflow statistics */
	update_record_counters(ctx, flow, sft_packet, sft_status->initiator);
	if (result == DOCA_SUCCESS) {
		/* Update bytes counters */
		flow->scanned_bytes[sft_status->initiator] += (rte_pktmbuf_pkt_len(sft_packet) - payload_offset);
		/* When reaching max_dpi_depth, offload flow to HW */
		if (max_dpi_depth > 0 && flow->scanned_bytes[sft_status->initiator] > max_dpi_depth)
			if (rte_sft_flow_set_state(ctx->queue_id, sft_status->fid, HAIRPIN_SKIPPED_FLOW, &sft_error))
				DOCA_DLOG_DBG("Failed to set flow state (fid=%u)", sft_status->fid);

		return;
	}

	/* Empty packets will be forwarded */
forward_packet:
	TX_BUFFER_PKT(sft_packet, ctx);
}

/*
 * Drains fragmented packets from SFT according to the first packet in the fragmented packet list
 *
 * @sft_status [in]: SFT status associated with the flow
 * @ctx [in]: Worker context
 * @first_packet [in]: First packet in the fragmented packet list, returned by rte_sft_process_mbuf()
 */
static void
handle_fragmented_flow(struct rte_sft_flow_status *sft_status,
			struct worker_ctx *ctx,
			struct rte_mbuf *first_packet)
{
	int ret;
	struct rte_mbuf *drain_buff[BURST_SIZE];
	struct flow_info *flow;
	struct rte_sft_error error;
	struct rte_sft_mbuf_info mbuf_info = {0};
	struct rte_mbuf *packet = NULL;
	struct doca_dpi_parsing_info parsing_info = {0};
	int nb_packets_to_drain = sft_status->nb_ip_fragments, drained_packets;
	bool first_packet_enqueued = false;
	uint8_t packet_index;

	assert(first_packet != NULL);
	do {
		DOCA_DLOG_DBG("Draining %d fragmented packets, queue_id %d", nb_packets_to_drain, ctx->queue_id);
		ret = rte_sft_drain_fragment_mbuf(ctx->queue_id,
						SFT_ZONE,
						sft_status->ipfrag_ctx,
						nb_packets_to_drain,
						drain_buff,
						sft_status,
						&error);
		if (unlikely(ret != 0)) {
			DOCA_LOG_DBG("Failed to drain fragmented packets, error=%s", error.message);
			return;
		}

		drained_packets = sft_status->nb_ip_fragments;
		nb_packets_to_drain -= drained_packets;
		/* Program does not support fragments when they are the first ones in the flow */
		if (!sft_status->activated) {
			DOCA_LOG_DBG("No flow activated, dropping packet");
			return;
		}
		if (!first_packet_enqueued) {
			/*
			 * Only after draining the frags, we get FID, so now we can enqueue first packet.
			 * First packet contains the L4 information and flow is determined.
			 */
			DOCA_DLOG_DBG("Enqueueing first packet");
			if (rte_sft_parse_mbuf(first_packet, &mbuf_info, NULL, &error) != 0) {
				DOCA_LOG_DBG("SFT parse MBUF failed, error=%s", error.message);
				return;
			}
			/* Verify that the l4 protocol is UDP or TCP */
			if (set_l4_parsing_info(&mbuf_info, &parsing_info) != DOCA_SUCCESS)
				return;
			flow = (struct flow_info *)rte_sft_flow_get_client_obj(ctx->queue_id,
									       sft_status->fid,
									       SFT_FLOW_INFO_CLIENT_ID,
									       &error);
			if (flow == NULL) {
				DOCA_LOG_ERR("SFT flow get client obj failed, error=%s", error.message);
				return;
			}
			enqueue_packet_to_dpi(first_packet, flow, sft_status, ctx);
			first_packet_enqueued = true;
		}
		DOCA_DLOG_DBG("Drained %d packets. Enqueueing them", drained_packets);

		for (packet_index = 0; packet_index < drained_packets; packet_index++) {
			packet = drain_buff[packet_index];
			enqueue_packet_to_dpi(packet, flow, sft_status, ctx);
		}
	} while (nb_packets_to_drain > 0);
}

/*
 * Handles OOO packets once they are received from SFT
 *
 * @sft_status [in]: SFT status associated with the flow
 * @flow [in]: Flow info associated with the flow
 * @ctx [in]: Worker context
 */
static void
handle_and_forward_ooo(struct rte_sft_flow_status *sft_status, struct flow_info *flow, struct worker_ctx *ctx)
{
	int drained_packets;
	int  packets_to_drain = sft_status->nb_in_order_mbufs;
	uint16_t packet_idx;
	struct rte_sft_error error;
	struct rte_mbuf *drain_buff[BURST_SIZE];
	struct rte_mbuf *packet = NULL;

	do {
		DOCA_DLOG_DBG("Draining %d OOO packets", packets_to_drain);
		drained_packets = rte_sft_drain_mbuf(ctx->queue_id,
						sft_status->fid,
						drain_buff,
						BURST_SIZE,
						sft_status->initiator,
						sft_status,
						&error);
		DOCA_DLOG_DBG("Drained %d packets", drained_packets);

		packets_to_drain -= drained_packets;
		if (drained_packets < 0) {
			DOCA_DLOG_DBG("Failed to drain packets, error=%s", error.message);
			return;
		}

		for (packet_idx = 0; packet_idx < drained_packets; packet_idx++) {
			packet = drain_buff[packet_idx];
			enqueue_packet_to_dpi(packet, flow, sft_status, ctx);
		}
	} while (packets_to_drain > 0);
}

/*
 * The main function which polls burst of packets on the corresponding port on this lcore's queue
 * Each packet is processed by the SFT and then enqueued to the DPI engine
 *
 * @ctx [in]: Worker context
 */
static void
process_packet(struct worker_ctx *ctx)
{
	int ret = 0, packet_idx = 0;

	const uint8_t queue_id = ctx->queue_id;
	const uint16_t ingress_port = ctx->ingress_port;
	const uint16_t egress_port = ingress_port ^ 1;

	struct rte_sft_flow_status sft_status;
	struct rte_sft_error sft_error;

	struct rte_mbuf *buf_in[BURST_SIZE];
	struct rte_mbuf *packet;
	struct rte_mbuf *sft_packet;	/* Packet returned by SFT */

	struct flow_info *flow;
	const uint16_t nb_rx = rte_eth_rx_burst(ingress_port, queue_id, buf_in, BURST_SIZE);
	doca_error_t result;

	ctx->processed_packets += nb_rx;

	/* Inspect each packet in the buffer */
	for (packet_idx = 0; packet_idx < nb_rx; packet_idx++) {
		DOCA_DLOG_DBG("================================ port = %d =============================================", ingress_port);
		packet = buf_in[packet_idx];
		memset(&sft_status, 0, sizeof(sft_status));	/* reset sft_status */
		ret = rte_sft_process_mbuf(queue_id, packet, &sft_packet, &sft_status, &sft_error);

		if (unlikely(ret != 0)) {
			DOCA_LOG_ERR("SFT process mbuf failed, error=%s", sft_error.message);
			rte_pktmbuf_free(packet);
			continue;
		}

		if (unlikely(sft_status.proto_state == SFT_CT_STATE_ERROR)) {
			rte_pktmbuf_free(packet);
			continue;
		}

		/* Fragmented packets are treated as new CT if not skipped */
		if (unlikely(sft_status.fragmented))
			continue;

		if (unlikely(sft_status.ipfrag_ctx != 0x0)) {
			/* Ready to handle fragmented flow */
			handle_fragmented_flow(&sft_status, ctx, sft_packet);
			continue;
		}

		if (unlikely(sft_status.proto_state == SFT_CT_STATE_NEW)) {
			/* Phase 2 only on new connections */
			result = activate_new_connection(packet, ctx, &sft_status, &sft_packet);
			if (unlikely(result != DOCA_SUCCESS))
				continue;

		}

		if (sft_packet == NULL)
			continue;

		flow = rte_sft_flow_get_client_obj(queue_id, sft_status.fid, SFT_FLOW_INFO_CLIENT_ID, &sft_error);
		if (unlikely(flow == NULL)) {
			if (rte_sft_flow_destroy(queue_id, sft_status.fid, &sft_error) != 0)
				DOCA_LOG_ERR("Failed to destroy flow, error=%s", sft_error.message);
			continue;
		}


		if (unlikely(sft_status.nb_in_order_mbufs > 0)) {
			enqueue_packet_to_dpi(sft_packet, flow, &sft_status, ctx);
			handle_and_forward_ooo(&sft_status, flow, ctx);
			continue;
		}

		/* Enqueue to DPI */
		enqueue_packet_to_dpi(sft_packet, flow, &sft_status, ctx);
		DOCA_DLOG_DBG("================================================================================");
		/* Add additional new lines for output readability */
		DOCA_DLOG_DBG("\n");
	}

	dequeue_from_dpi(ctx);
	/* Flush ready to send packets */
	rte_eth_tx_buffer_flush(egress_port, queue_id, ctx->tx_buffer[egress_port]);
	clear_aged_flows(ctx);
}

/*
 * Cleanup worker context
 *
 * @ctx [in]: Worker context
 */
static void
dpi_worker_cleanup(struct worker_ctx *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->workq != NULL) {
		doca_ctx_workq_rm(doca_dpi_as_ctx(ctx->attr.dpi_ctx->dpi), ctx->workq);
		doca_workq_destroy(ctx->workq);
		ctx->workq = NULL;
	}
	if (ctx->doca_dpdk_pool != NULL) {
		doca_dpdk_mempool_destroy(ctx->doca_dpdk_pool);
		ctx->doca_dpdk_pool = NULL;
	}
	if (ctx->buf_inventory != NULL) {
		doca_buf_inventory_destroy(ctx->buf_inventory);
		ctx->buf_inventory = NULL;
	}
	if (ctx->meta_mempool != NULL) {
		rte_mempool_free(ctx->meta_mempool);
		ctx->meta_mempool = NULL;
	}
	rte_free(ctx);
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 *
 * @worker [in]: Worker context
 */
static void
dpi_worker(void *worker)
{
	uint8_t nb_ports = rte_eth_dev_count_avail();
	uint8_t port;
	struct worker_ctx *ctx = (struct worker_ctx *)worker;
	const uint16_t buff_size = BURST_SIZE;

	ctx->dropped_packets = 0;
	for (port = 0; port < nb_ports; port++) {
		ctx->tx_buffer[port] = rte_zmalloc_socket(NULL, RTE_ETH_TX_BUFFER_SIZE(buff_size), 0, rte_socket_id());
		if (rte_eth_tx_buffer_init(ctx->tx_buffer[port], buff_size) != 0) {
			force_quit = true;
			DOCA_LOG_ERR("Failed to init TX buffer");
			goto thread_out;
		}
		rte_eth_tx_buffer_set_err_callback(ctx->tx_buffer[port], rte_eth_tx_buffer_count_callback, &ctx->dropped_packets);
	}

	DOCA_DLOG_DBG("Core %u is forwarding packets", rte_lcore_id());
	/* Run until the application is quit or killed */
	while (!kill_workers) {
		for (port = 0; port < nb_ports; port++) {
			ctx->ingress_port = port;
			process_packet(ctx);
		}
	}

thread_out:
	for (port = 0; port < nb_ports; port++)
		if (ctx->tx_buffer[port] != NULL) {
			rte_eth_tx_buffer_flush(port, 0, ctx->tx_buffer[port]);
			rte_free(ctx->tx_buffer[port]);
		}
	pthread_mutex_lock(&log_lock);
	DOCA_LOG_INFO("Core %u has processed %lu packets, dropped %lu packets", rte_lcore_id(),
									ctx->processed_packets, ctx->dropped_packets);
	pthread_mutex_unlock(&log_lock);
	dpi_worker_cleanup(ctx);
}

void
dpi_worker_lcores_stop(struct doca_dpi_worker_ctx *dpi_ctx)
{
	struct doca_dpi_stat_info doca_stat = {0};

	force_quit = true;
	/* Gather stats before killing workers */
	doca_dpi_get_stats(dpi_ctx->dpi, true, &doca_stat);
	kill_workers = true;
	rte_eal_mp_wait_lcore();
	/* Print DPI statistics */
	DOCA_LOG_INFO("------------- DPI STATISTICS --------------");
	DOCA_LOG_INFO("Packets scanned:%d", doca_stat.nb_scanned_pkts);
	DOCA_LOG_INFO("Matched signatures:%d", doca_stat.nb_matches);
	DOCA_LOG_INFO("TCP matches:%d", doca_stat.nb_tcp_based);
	DOCA_LOG_INFO("UDP matches:%d", doca_stat.nb_udp_based);
	DOCA_LOG_INFO("HTTP matches:%d", doca_stat.nb_http_parser_based);
	DOCA_LOG_INFO("SSL matches:%d", doca_stat.nb_ssl_parser_based);
	DOCA_LOG_INFO("Miscellaneous L4:%d, L7:%d", doca_stat.nb_other_l4, doca_stat.nb_other_l7);
}

void
printf_signature(struct doca_dpi_worker_ctx *dpi_ctx, uint32_t sig_id, uint32_t fid, bool blocked)
{
	doca_error_t result;
	struct doca_dpi_sig_data sig_data;

	result = doca_dpi_get_signature(dpi_ctx->dpi, sig_id, &sig_data);
	if (likely(result == DOCA_SUCCESS))
		DOCA_LOG_INFO("SIG ID: %u, APP Name: %s, SFT_FID: %u, Blocked: %u", sig_id, sig_data.name, fid, blocked);
	else
		DOCA_LOG_ERR("Failed to get signatures: %s", doca_get_error_string(result));
}

doca_error_t
dpi_worker_lcores_run(int nb_queues, struct dpi_worker_attr attr)
{
	int lcore = 0;
	uint16_t lcore_index = 0;
	struct worker_ctx *ctx = NULL; /* To be freed by the worker */
	char name[MEMPOOL_NAME_MAX_LEN];
	doca_error_t result;
	uint8_t nb_ports = rte_eth_dev_count_avail();

	if (nb_ports != SFT_PORTS_NUM) {
		DOCA_LOG_ERR("Application only function with [%d] ports, but num_of_ports = [%d]",
				SFT_PORTS_NUM, nb_ports);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* Make sure the value is initialized */
	force_quit = false;
	kill_workers = false;

	/* Main thread is reserved */
	RTE_LCORE_FOREACH_WORKER(lcore) {
		DOCA_DLOG_DBG("Creating worker on core %u", lcore);
		ctx = (struct worker_ctx *)rte_zmalloc(NULL, sizeof(struct worker_ctx), 0);
		if (ctx == NULL) {
			DOCA_LOG_ERR("Failed to allocate memory for worker context on core %d", lcore);
			return DOCA_ERROR_NO_MEMORY;
		}
		ctx->queue_id = lcore_index;
		ctx->attr = attr;

		/* To prepare the job result buffer for the DPI engine */
		sprintf(name, "meta_mempool_%d", ctx->queue_id);
		ctx->meta_mempool = rte_mempool_create(name,
							NUM_MBUFS,
							sizeof(struct dpi_job_meta_data),
							0, 0,
							NULL, NULL,
							NULL, NULL,
							rte_socket_id(),
							RTE_MEMPOOL_F_SC_GET | RTE_MEMPOOL_F_SP_PUT);
		if (ctx->meta_mempool == NULL) {
			DOCA_LOG_ERR("Failure to allocate results mempool!");
			dpi_worker_cleanup(ctx);
			return DOCA_ERROR_NO_MEMORY;
		}

		/* DOCA buf_inventory create and start for DPDK bridge */
		result = doca_buf_inventory_create(NULL, NUM_MBUFS, DOCA_BUF_EXTENSION_NONE, &ctx->buf_inventory);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Buf_inventory create failure, %s", doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}
		result = doca_buf_inventory_start(ctx->buf_inventory);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Buf_inventory start failure, %s", doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}

		/*
		 * DOCA DPDK bridge create and start.
		 * So the rte_mbuf can be converted into DOCA buf.
		 */
		result = doca_dpdk_mempool_create(attr.dpdk_config->mbuf_pool, &ctx->doca_dpdk_pool);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("DPDK bridge_mempool create failure, %s", doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}
		result = doca_dpdk_mempool_dev_add(ctx->doca_dpdk_pool, attr.dpi_ctx->dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("DOCA DPDK mempool dev add failure, %s", doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}
		result = doca_dpdk_mempool_start(ctx->doca_dpdk_pool);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("DOCA DPDK mempool start failure, %s", doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}

		/* Workq create and add */
		result = doca_workq_create(NUM_MBUFS, &ctx->workq);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to create DOCA workq, err = [%s]",
				doca_get_error_string(result));
			rte_free(ctx);
			return result;
		}
		result = doca_ctx_workq_add(doca_dpi_as_ctx(attr.dpi_ctx->dpi), ctx->workq);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to register workq with doca_dpi, err = [%s]",
					doca_get_error_string(result));
			dpi_worker_cleanup(ctx);
			return result;
		}

		if (rte_eal_remote_launch((void *)dpi_worker, (void *)ctx, lcore) != 0) {
			DOCA_LOG_ERR("Failed to launch DPI worker on core %d", lcore);
			dpi_worker_cleanup(ctx);
			return DOCA_ERROR_DRIVER;
		}
		lcore_index++;
	}

	if (lcore_index != nb_queues) {
		DOCA_LOG_ERR("%d cores are used as DPI workers, but %d queues are configured", lcore_index, nb_queues);
		return DOCA_ERROR_INVALID_VALUE;
	}
	DOCA_LOG_INFO("%d cores are used as DPI workers", lcore_index);
	return DOCA_SUCCESS;
}
