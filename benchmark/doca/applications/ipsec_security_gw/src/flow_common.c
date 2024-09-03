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
#include <stdlib.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW::flow_common);

/*
 * Entry processing callback
 *
 * @entry [in]: entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
static void
check_for_valid_entry(struct doca_flow_pipe_entry *entry, uint16_t pipe_queue,
		      enum doca_flow_entry_status status, enum doca_flow_entry_op op, void *user_ctx)
{
	(void)entry;
	(void)op;
	(void)pipe_queue;

	struct entries_status *entry_status = (struct entries_status *)user_ctx;

	if (entry_status == NULL || op != DOCA_FLOW_ENTRY_OP_ADD)
		return;
	if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
		entry_status->failure = true; /* set failure to true if processing failed */
	entry_status->nb_processed++;
	entry_status->entries_in_queue--;
}

/*
 * Process entries and check the returned status
 *
 * @port [in]: the port we want to process in
 * @status [in]: the entries status that was sent to the pipe
 * @timeout [in]: timeout for the entries process function
 */
doca_error_t
process_entries(struct doca_flow_port *port, struct entries_status *status, int timeout)
{
	doca_error_t result;

	result = doca_flow_entries_process(port, 0, timeout, status->entries_in_queue);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
		return result;
	}
	if (status->failure || status->entries_in_queue == QUEUE_DEPTH) {
		DOCA_LOG_ERR("Failed to process entries");
		return DOCA_ERROR_BAD_STATE;
	}
	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @port [out]: pointer to port handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_doca_flow_port(int port_id, struct doca_flow_port **port)
{
	const int max_port_str_len = 128;
	struct doca_flow_port_cfg port_cfg;
	char port_id_str[max_port_str_len];

	memset(&port_cfg, 0, sizeof(port_cfg));

	port_cfg.port_id = port_id;
	port_cfg.type = DOCA_FLOW_PORT_DPDK_BY_ID;
	snprintf(port_id_str, max_port_str_len, "%d", port_cfg.port_id);
	port_cfg.devargs = port_id_str;
	return doca_flow_port_start(&port_cfg, port);
}

doca_error_t
ipsec_security_gw_init_doca_flow(const struct ipsec_security_gw_config *app_cfg, int nb_queues, struct ipsec_security_gw_ports_map *ports[])
{
	int port_id;
	int port_idx = 0;
	int nb_ports = 0;
	struct doca_flow_cfg flow_cfg;
	doca_error_t result;

	memset(&flow_cfg, 0, sizeof(flow_cfg));

	/* init doca flow with crypto shared resources */
	flow_cfg.queues = nb_queues;
	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF)
		flow_cfg.mode_args = "vnf,hws,isolated";
	else
		flow_cfg.mode_args = "switch,hws,isolated";
	flow_cfg.queue_depth = QUEUE_DEPTH;
	flow_cfg.cb = check_for_valid_entry;
	/* DECRYPT_DUMMY_ID is the highest ID, adding one to be able to use it exactly */
	flow_cfg.nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_CRYPTO] = DECRYPT_DUMMY_ID + 1;
	flow_cfg.resource.nb_counters = MAX_NB_RULES * NUM_OF_SYNDROMES;
	result = doca_flow_init(&flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	for (port_id = 0; port_id < RTE_MAX_ETHPORTS; port_id++) {
		/* search for the probed devices */
		if (!rte_eth_dev_is_valid_port(port_id))
			continue;
		/* get device idx for ports array - secured or unsecured */
		if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF)
			result = find_port_action_type_vnf(app_cfg, port_id, &port_idx);
		else
			result = find_port_action_type_switch(port_id, &port_idx);
		if (result != DOCA_SUCCESS)
			return result;

		ports[port_idx] = malloc(sizeof(struct ipsec_security_gw_ports_map));
		if (ports[port_idx] == NULL) {
			DOCA_LOG_ERR("malloc() failed");
			doca_flow_cleanup(nb_ports, ports);
			return DOCA_ERROR_NO_MEMORY;
		}
		result = create_doca_flow_port(port_id, &ports[port_idx]->port);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to init DOCA Flow port: %s", doca_error_get_descr(result));
			free(ports[port_idx]);
			doca_flow_cleanup(nb_ports, ports);
			return result;
		}
		nb_ports++;
		ports[port_idx]->port_id = port_id;
	}
	if (ports[SECURED_IDX]->port == NULL || ports[UNSECURED_IDX]->port == NULL) {
		DOCA_LOG_ERR("Failed to init two DOCA Flow ports");
		doca_flow_cleanup(nb_ports, ports);
		return DOCA_ERROR_INITIALIZATION;
	}
	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = doca_flow_port_pair(ports[SECURED_IDX]->port, ports[UNSECURED_IDX]->port);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to pair ports");
			doca_flow_cleanup(nb_ports, ports);
			return DOCA_ERROR_INITIALIZATION;
		}
	}
	return DOCA_SUCCESS;
}

void
doca_flow_cleanup(int nb_ports, struct ipsec_security_gw_ports_map *ports[])
{
	int port_id;

	for (port_id = 0; port_id < nb_ports; port_id++) {
		if (ports[port_id] != NULL) {
			doca_flow_port_stop(ports[port_id]->port);
			free(ports[port_id]);
		}
	}

	doca_flow_destroy();
}

doca_error_t
create_rss_pipe(struct doca_flow_port *port, uint16_t hairpin_queue_id,
		enum ipsec_security_gw_esp_offload offload, enum doca_ipsec_direction direction)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_pipe *pipe;
	struct entries_status status;
	int num_of_entries = 2;
	uint16_t *rss_queues = NULL;
	int i;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&fwd, 0, sizeof(fwd));
	memset(&status, 0, sizeof(status));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "RSS_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.port = port;
	pipe_cfg.attr.is_root = true;

	match_mask.meta.pkt_meta = (1U << 31) | (1 << 30);

	fwd.type = DOCA_FLOW_FWD_RSS;
	if (offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_NONE ||
	   (direction == DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT && offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP) ||
	   (direction == DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT && offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_ENCAP)) {
		rss_queues = (uint16_t *)calloc(hairpin_queue_id, sizeof(uint16_t));
		if (rss_queues == NULL) {
			DOCA_LOG_ERR("Failed to allocate memory for RSS queues");
			return DOCA_ERROR_NO_MEMORY;
		}

		for (i = 0; i < hairpin_queue_id - 1; i++)
			rss_queues[i] = i + 1;
		fwd.rss_queues = rss_queues;
		fwd.num_of_queues = hairpin_queue_id - 1;
	} else {
		fwd.rss_queues = &hairpin_queue_id;
		fwd.num_of_queues = 1;
	}

	result = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, &pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create RSS pipe: %s", doca_error_get_descr(result));
		if (rss_queues != NULL)
			free(rss_queues);
		return result;
	}

	if (rss_queues != NULL)
		free(rss_queues);

	match.meta.pkt_meta = 1 << 30;
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to RSS pipe: %s", doca_error_get_descr(result));
		return result;
	}

	match.meta.pkt_meta = 1U << 31;
	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, NULL, DOCA_FLOW_NO_WAIT, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to RSS pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process entry: %s", doca_error_get_descr(result));
		return result;
	}
	if (status.nb_processed != num_of_entries || status.failure) {
		DOCA_LOG_ERR("Failed to process entry");
		return DOCA_ERROR_BAD_STATE;
	}
	return DOCA_SUCCESS;
}

doca_error_t
create_empty_pipe(struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "EMPTY_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.port = doca_flow_port_switch_get(NULL);

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create empty pipe: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

void
create_hairpin_pipe_fwd(struct ipsec_security_gw_config *app_cfg, int port_id,
			struct doca_flow_pipe *empty_pipe, enum doca_ipsec_direction direction,
			uint16_t *rss_queues, uint32_t rss_flags, struct doca_flow_fwd *fwd)
{
	uint32_t nb_queues = app_cfg->dpdk_config->port_config.nb_queues;
	uint32_t i;

	memset(fwd, 0, sizeof(*fwd));

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_SWITCH) {
		fwd->type = DOCA_FLOW_FWD_PIPE;
		fwd->next_pipe = empty_pipe;
	} else {
		if ((app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_NONE) ||
		    (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_ENCAP && direction == DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT) ||
		    (app_cfg->offload == IPSEC_SECURITY_GW_ESP_OFFLOAD_DECAP && direction == DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT)) {
			/* for software handling the packets will be sent to the application by RSS queues */
			for (i = 0; i < nb_queues - 1; i++)
				rss_queues[i] = i + 1;

			fwd->type = DOCA_FLOW_FWD_RSS;
			if (direction == DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT && app_cfg->mode == IPSEC_SECURITY_GW_TUNNEL)
				fwd->rss_inner_flags = rss_flags;
			else
				fwd->rss_outer_flags = rss_flags;

			fwd->rss_queues = rss_queues;
			fwd->num_of_queues = nb_queues - 1;
		} else {
			fwd->type = DOCA_FLOW_FWD_PORT;
			fwd->port_id = port_id ^ 1;
		}
	}
}

/*
 * Create DOCA Flow pipe that match on port meta field.
 *
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_switch_port_meta_pipe(struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_pipe_cfg pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SWITCH_PORT_META_PIPE";
	pipe_cfg.match = &match;
	pipe_cfg.port = doca_flow_port_switch_get(NULL);

	match.parser_meta.port_meta = UINT32_MAX;

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create switch port meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Add entries to port meta pipe
 * Send packets to decrypt / encrypt path based on the port
 *
 * @ports [in]: array of struct ipsec_security_gw_ports_map
 * @encrypt_root [in]: pipe to send the packets that comes from unsecured port
 * @decrypt_root [in]: pipe to send the packets that comes from secured port
 * @pipe [in]: the pipe to add entries to
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_switch_port_meta_entries(struct ipsec_security_gw_ports_map *ports[], struct doca_flow_pipe *encrypt_root,
			     struct doca_flow_pipe *decrypt_root, struct doca_flow_pipe *pipe)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct entries_status status;
	int num_of_entries = 2;
	doca_error_t result;

	memset(&status, 0, sizeof(status));
	memset(&match, 0, sizeof(match));

	/* forward the packets from the unsecured port to encryption */
	match.parser_meta.port_meta = ports[UNSECURED_IDX]->port_id;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = encrypt_root;

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to syndrome pipe: %s", doca_error_get_descr(result));
		return result;
	}

	/* forward the packets from the secured port to decryption */
	match.parser_meta.port_meta = ports[SECURED_IDX]->port_id;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = decrypt_root;

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, DOCA_FLOW_NO_WAIT, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to syndrome pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(NULL), 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS)
		return result;
	if (status.nb_processed != num_of_entries || status.failure)
		return DOCA_ERROR_BAD_STATE;

	return DOCA_SUCCESS;
}

/*
 * Create the switch root pipe, which match the first 2 MSB in pkt meta
 *
 * @pipe [out]: the created pipe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_switch_pkt_meta_pipe(struct doca_flow_pipe **pipe)
{
	struct doca_flow_pipe_cfg pipe_cfg;
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	doca_error_t result;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));
	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));

	pipe_cfg.attr.name = "PKT_META_PIPE";
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = doca_flow_port_switch_get(NULL);

	match_mask.meta.pkt_meta = (1U << 31) | (1 << 30);

	result = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pkt meta pipe: %s", doca_error_get_descr(result));
		return result;
	}
	return DOCA_SUCCESS;
}

/*
 * Add entries to pkt meta pipe
 *
 * @ports [in]: array of struct ipsec_security_gw_ports_map
 * @encrypt_pipe [in]: pipe to forward the packets for encryption if pkt meta second bit is one
 * @match_port_pipe [in]: pipe to forward the packets if pkt meta is zero
 * @pipe [in]: pipe to add the entries
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
add_switch_pkt_meta_entries(struct ipsec_security_gw_ports_map *ports[], struct doca_flow_pipe *encrypt_pipe, struct doca_flow_pipe *match_port_pipe, struct doca_flow_pipe *pipe)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct entries_status status;
	int num_of_entries = 3;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&status, 0, sizeof(status));

	match.meta.pkt_meta = 0;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = match_port_pipe;

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to pkt meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	match.meta.pkt_meta = 1 << 30; /* second bit is one */
	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = encrypt_pipe;

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, DOCA_FLOW_WAIT_FOR_BATCH, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to pkt meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	match.meta.pkt_meta = 1U << 31; /* first bit is one */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = ports[UNSECURED_IDX]->port_id;

	result = doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, DOCA_FLOW_NO_WAIT, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry to pkt meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(doca_flow_port_switch_get(NULL), 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != DOCA_SUCCESS)
		return result;
	if (status.nb_processed != num_of_entries || status.failure)
		return DOCA_ERROR_BAD_STATE;

	return DOCA_SUCCESS;
}

doca_error_t
create_switch_root_pipes(struct ipsec_security_gw_ports_map *ports[], struct doca_flow_pipe *encrypt_root,
			struct doca_flow_pipe *decrypt_root, struct doca_flow_pipe *encrypt_pipe)
{
	struct doca_flow_pipe *match_port_pipe;
	struct doca_flow_pipe *match_meta_pipe;
	doca_error_t result;

	result = create_switch_port_meta_pipe(&match_port_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create port meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = add_switch_port_meta_entries(ports, encrypt_root, decrypt_root, match_port_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add port meta pipe entries: %s", doca_error_get_descr(result));
		return result;
	}

	result = create_switch_pkt_meta_pipe(&match_meta_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create pkt meta pipe: %s", doca_error_get_descr(result));
		return result;
	}

	result = add_switch_pkt_meta_entries(ports, encrypt_pipe, match_port_pipe, match_meta_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add pkt meta pipe entries: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

void
remove_trailing_zeros(struct rte_mbuf **m)
{
	struct rte_ether_hdr *oh;
	struct rte_ipv4_hdr *ipv4;
	struct rte_ipv6_hdr *ipv6;
	uint32_t payload_len, payload_len_l3, l2_l3_len;

	oh = rte_pktmbuf_mtod(*m, struct rte_ether_hdr *);

	if (RTE_ETH_IS_IPV4_HDR((*m)->packet_type)) {
		ipv4 = (void *)(oh + 1);
		l2_l3_len = rte_ipv4_hdr_len(ipv4) + sizeof(struct rte_ether_hdr);
		payload_len_l3 = rte_be_to_cpu_16(ipv4->total_length) - rte_ipv4_hdr_len(ipv4);
	} else {
		ipv6 = (void *)(oh + 1);
		l2_l3_len = sizeof(struct rte_ipv6_hdr) + sizeof(struct rte_ether_hdr);
		payload_len_l3 = rte_be_to_cpu_16(ipv6->payload_len);
	}

	payload_len = (*m)->pkt_len - l2_l3_len;

	/* check if need to remove trailing l2 zeros - occurs when packet_len < eth_minimum_len=64 */
	if (payload_len - payload_len_l3 > 0) {
		/* need to remove the extra zeros */
		rte_pktmbuf_trim(*m, payload_len - payload_len_l3);
	}
}
