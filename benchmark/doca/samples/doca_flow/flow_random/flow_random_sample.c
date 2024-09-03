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

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "doca_error.h"
#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_RANDOM);

/* The number of packets in the rx queue */
#define PACKET_BURST 256

/* The number of bits in random field */
#define RANDOM_WIDTH 16

/* Get the percentage according to part and total */
#define GET_PERSENTAGE(part, total) (((double)part / (double)total) * 100)

/*
 * Create DOCA Flow pipe with changeable 5 tuple match as root
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_root_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	/* 5 tuple match */
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	/* Add counter to see how many packet arrived before sampling/distribution */
	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	pipe_cfg.attr.name = "ROOT_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.attr.nb_flows = 2;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.port = port;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = NULL;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry to the root pipe that forwards the traffic to specific random pipe.
 *
 * @pipe [in]: pipe of the entries.
 * @next_pipe [in]: random pipe to forward the matched traffic.
 * @src_ip_addr [in]: the source IP address to match on in this entry.
 * @status [in]: user context for adding entry.
 * @entry [out]: created entry pointer.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_root_pipe_entry(struct doca_flow_pipe *pipe, struct doca_flow_pipe *next_pipe, doca_be32_t src_ip_addr,
		    struct entries_status *status, struct doca_flow_pipe_entry **entry)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.outer.ip4.dst_ip = BE_IPV4_ADDR(8, 8, 8, 8);
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = rte_cpu_to_be_16(80);
	match.outer.tcp.l4_port.src_port = rte_cpu_to_be_16(1234);

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = next_pipe;

	return doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, 0, status, entry);
}

/*
 * Add DOCA Flow pipe entries to the root pipe that forwards the traffic to random pipes.
 *
 * @pipe [in]: pipe of the entries.
 * @status [in]: user context for adding entry
 * @distribution_pipe [in]: distribution random pipe to forward the matched traffic.
 * @sampling_pipe [in]: sampling random pipe to forward the matched traffic.
 * @distribution_entry [out]: created distribution entry pointer.
 * @sampling_entry [out]: created sampling entry pointer.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_root_pipe_entries(struct doca_flow_pipe *pipe, struct entries_status *status,
		      struct doca_flow_pipe *distribution_pipe, struct doca_flow_pipe *sampling_pipe,
		      struct doca_flow_pipe_entry **distribution_entry,
		      struct doca_flow_pipe_entry **sampling_entry)
{
	doca_be32_t src_ip_addr;
	doca_error_t result;

	src_ip_addr = BE_IPV4_ADDR(1, 1, 1, 1);
	result = add_root_pipe_entry(pipe, sampling_pipe, src_ip_addr, status, sampling_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry - go to sampling pipe: %s", doca_error_get_descr(result));
		return result;
	}

	src_ip_addr = BE_IPV4_ADDR(2, 2, 2, 2);
	result = add_root_pipe_entry(pipe, distribution_pipe, src_ip_addr, status, distribution_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add entry - go to sampling pipe: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Calculate the mask used to achieve given certain percentage.
 *
 * The function finds the neerest supported percentage which is not bigger than requested
 * percentage and calulates mask for it.
 * The supported percentages are: [50, 25, 12.5, ... , 0.0015258789].
 *
 * @percentage [in]: the certain percentage user wish to get in sampling.
 * @return: value to use in match_mask.parser_meta.random field for getting this percentage.
 */
static uint16_t
get_random_mask(double percentage)
{
	double next_highest_supported_percentage = 50;
	uint16_t mask;
	uint8_t i;

	for (i = 1; i <= RANDOM_WIDTH; ++i) {
		if (percentage >= next_highest_supported_percentage)
			break;

		next_highest_supported_percentage /= 2;
	}

	if (percentage > next_highest_supported_percentage)
		DOCA_LOG_WARN("Requested %g%% is not supported, converted to %g%% instead",
			      percentage, next_highest_supported_percentage);

	mask = (1 << i) - 1;
	DOCA_LOG_DBG("Get random mask 0x%04x for sampling %g%% of traffic", mask, next_highest_supported_percentage);

	return mask;
}

/*
 * Add DOCA Flow pipe for sampling according to random value
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @percentage [in]: the certain percentage user wish to get in sampling
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_random_sampling_pipe(struct doca_flow_port *port, int port_id, double percentage, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_match match_mask;
	struct doca_flow_monitor monitor;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match, 0, sizeof(match));
	memset(&match_mask, 0, sizeof(match_mask));
	memset(&monitor, 0, sizeof(monitor));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	/* Calculate the mask according to requested percentage */
	match_mask.parser_meta.random = get_random_mask(percentage);
	/*
	 * Specific value 0, 0 is valid value for any supported percentage.
	 */
	match.parser_meta.random = 0;

	/* Add counter to see how many packet are sampled */
	monitor.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	pipe_cfg.attr.name = "SAMPLING_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_flows = 1; /* Must be 1 for basic pipe */
	pipe_cfg.match = &match;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.monitor = &monitor;
	pipe_cfg.port = port;

	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entry to the random sampling pipe.
 *
 * @pipe [in]: pipe of the entries
 * @status [in]: user context for adding entry
 * @entry [out]: created entry pointer.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_random_sampling_pipe_entry(struct doca_flow_pipe *pipe, struct entries_status *status, struct doca_flow_pipe_entry **entry)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	/*
	 * The values for both fwd and match structures was provided as specific in pipe creation,
	 * no need to provide fresh information here again.
	 */

	return doca_flow_pipe_add_entry(0, pipe, &match, NULL, NULL, &fwd, 0, status, entry);
}

/*
 * Add DOCA Flow hash pipe for distribution according to random value.
 *
 * @port [in]: port of the pipe
 * @nb_flows [in]: number of entries for this pipe.
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_random_distribution_pipe(struct doca_flow_port *port, int nb_flows, struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match_mask;
	struct doca_flow_fwd fwd;
	uint16_t rss_queues = 0;
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&match_mask, 0, sizeof(match_mask));
	memset(&fwd, 0, sizeof(fwd));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	/* The distribution is determined by number of entries, we can use full mask */
	match_mask.parser_meta.random = UINT16_MAX;

	pipe_cfg.attr.name = "DISTRIBUTION_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_HASH;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_flows = nb_flows;
	pipe_cfg.match_mask = &match_mask;
	pipe_cfg.port = port;

	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = &rss_queues;
	fwd.num_of_queues = UINT32_MAX;

	return doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, pipe);
}

/*
 * Add DOCA Flow pipe entries to the random distribution pipe.
 *
 * @pipe [in]: pipe of the entries.
 * @nb_entries [in]: number of entries to add.
 * @status [in]: user context for adding entry.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_random_distribution_pipe_entries(struct doca_flow_pipe *pipe, int nb_entries, struct entries_status *status)
{
	enum doca_flow_flags_type flags = DOCA_FLOW_WAIT_FOR_BATCH;
	struct doca_flow_pipe_entry *entry;
	struct doca_flow_fwd fwd;
	doca_error_t result;
	uint16_t queue;
	uint16_t i;

	memset(&fwd, 0, sizeof(fwd));

	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = &queue;
	fwd.num_of_queues = 1;

	for (i = 0; i < nb_entries; i++) {
		queue = i;

		if (i == nb_entries - 1)
			flags = DOCA_FLOW_NO_WAIT;

		result = doca_flow_pipe_hash_add_entry(0, pipe, i, NULL, NULL, &fwd, flags, status, &entry);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add hash pipe entry %u: %s", i, doca_error_get_descr(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Get results about cretain persentage sampling.
 *
 * @port_id [in]: port id.
 * @root_entry [in]: entry sent packets to sampling.
 * @random_entry [in]: entry samples cretain persentage of traffic.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
random_sampling_results(uint16_t port_id, struct doca_flow_pipe_entry *root_entry,
			struct doca_flow_pipe_entry *random_entry)
{
	struct doca_flow_query root_query_stats;
	struct doca_flow_query random_query_stats;
	double actuall_persentage;
	uint32_t total_packets;
	uint32_t nb_packets;
	doca_error_t result;

	result = doca_flow_query_entry(root_entry, &root_query_stats);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query root entry: %s", doca_error_get_descr(result));
		return result;
	}

	total_packets = root_query_stats.total_pkts;
	if (total_packets == 0)
		return DOCA_SUCCESS;

	result = doca_flow_query_entry(random_entry, &random_query_stats);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query random entry: %s", doca_error_get_descr(result));
		return result;
	}

	nb_packets = random_query_stats.total_pkts;
	actuall_persentage = GET_PERSENTAGE(nb_packets, total_packets);

	DOCA_LOG_INFO("Port %d sampling information:", port_id);
	DOCA_LOG_INFO("This pipeline samples %u packets which is %g%% of the traffic (%u/%u)",
		      nb_packets, actuall_persentage, nb_packets, total_packets);

	return DOCA_SUCCESS;
}

/*
 * Dequeue packets from DPDK queues and calculates the distribution.
 *
 * @port_id [in]: port id for dequeue packets.
 * @nb_queues [in]: number of Rx queues.
 * @root_entry [in]: entry sent packets before distribution.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
random_distribution_results(uint16_t port_id, uint16_t nb_queues, struct doca_flow_pipe_entry *root_entry)
{
	struct rte_mbuf *packets[PACKET_BURST];
	struct doca_flow_query root_query_stats;
	double actuall_persentage;
	uint32_t total_packets;
	uint16_t nb_packets;
	doca_error_t result;
	int i;

	result = doca_flow_query_entry(root_entry, &root_query_stats);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query root entry in port %u: %s", port_id, doca_error_get_descr(result));
		return result;
	}

	total_packets = root_query_stats.total_pkts;
	if (total_packets == 0)
		return DOCA_SUCCESS;

	DOCA_LOG_INFO("Port %d distribution information:", port_id);

	for (i = 0; i < nb_queues; i++) {
		nb_packets = rte_eth_rx_burst(port_id, i, packets, PACKET_BURST);
		actuall_persentage = GET_PERSENTAGE(nb_packets, total_packets);

		DOCA_LOG_INFO("Queue %u received %u packets which is %g%% of the traffic (%u/%u)",
			      i, nb_packets, actuall_persentage, nb_packets, total_packets);
	}

	return DOCA_SUCCESS;
}

/*
 * Run flow_random sample.
 *
 * This sample tests the 2 common use-cases of random matching:
 *  1. Sampling certain persentage of traffic.
 *  2. Random distribution over port/queues.
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
flow_random(int nb_queues)
{
	int nb_ports = 2;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *root_pipe;
	struct doca_flow_pipe *sampling_pipe;
	struct doca_flow_pipe *distribution_pipe;
	struct doca_flow_pipe_entry *root2sampling_entry[nb_ports];
	struct doca_flow_pipe_entry *root2distribution_entry[nb_ports];
	struct doca_flow_pipe_entry *random_entry[nb_ports];
	struct entries_status status;
	double requested_persentage = 12.5;
	int num_of_entries = 3 + nb_queues;
	doca_error_t result;
	int port_id;

	memset(&status, 0, sizeof(status));
	resource.nb_counters = num_of_entries;

	result = init_doca_flow(nb_queues, "vnf,hws", resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	result = init_doca_flow_ports(nb_ports, ports, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {
		memset(&status, 0, sizeof(status));

		result = create_random_distribution_pipe(ports[port_id], nb_queues, &distribution_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create random distribution pipe: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_random_distribution_pipe_entries(distribution_pipe, nb_queues, &status);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add random distribution pipe entries: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_random_sampling_pipe(ports[port_id], port_id, requested_persentage, &sampling_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create random sampling pipe: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_random_sampling_pipe_entry(sampling_pipe, &status, &random_entry[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add random sampling pipe entry: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = create_root_pipe(ports[port_id], &root_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create root pipe: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = add_root_pipe_entries(root_pipe, &status, distribution_pipe, sampling_pipe,
					       &root2distribution_entry[port_id],
					       &root2sampling_entry[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add root pipe entries: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		result = doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, 0);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		if (status.nb_processed != num_of_entries || status.failure) {
			DOCA_LOG_ERR("Failed to process entries");
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return DOCA_ERROR_BAD_STATE;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	sleep(15);

	for (port_id = 0; port_id < nb_ports; port_id++) {
		/* Show the results for sampling */
		result = random_sampling_results(port_id, root2sampling_entry[port_id], random_entry[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to show sampling results in port %u: %s",
				     port_id, doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}

		/* Show the results for distribution */
		result = random_distribution_results(port_id, nb_queues, root2distribution_entry[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to show distribution results in port %u: %s",
				     port_id, doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
	}

	stop_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return DOCA_SUCCESS;
}
