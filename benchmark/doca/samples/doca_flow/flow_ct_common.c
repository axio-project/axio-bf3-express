/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <doca_log.h>

#include <dpdk_utils.h>

#include "flow_ct_common.h"

#define DPDK_ADDITIONAL_ARG 2

DOCA_LOG_REGISTER(FLOW_CT_COMMON);

/*
 * ARGP Callback - Handle DOCA Flow CT device PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t pci_addr_callback(void *param, void *config)
{
	struct ct_config *cfg = (struct ct_config *)config;
	const char *dev_pci_addr = (char *)param;
	int len;

	len = strnlen(dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE);
	/* Check using >= to make static code analysis satisfied */
	if (len >= DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d",
			     DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(cfg->ct_dev_pci_addr[cfg->n_ports++], dev_pci_addr, len + 1);

	return DOCA_SUCCESS;
}

#define FNV1A_32_OFFSET (uint32_t)2166136261
#define FNV1A_32_PRIME (uint32_t)16777619
/*
 * FNV1A 32 bit hash calculation function
 *
 * @buf [in]: Input buffer to calculates hash on it's byte
 * @len [in]: Bytes size of the input buffer
 * @hash [in]: FNV1A_32_OFFSET or previous hash calculation
 * @return: FNV1A hash calculation of the buffer
 */
static uint32_t fnv1a_32bit_hash(const void *buf, size_t len, uint32_t hash)
{
	const uint8_t *bytes = (const uint8_t *)buf;
	size_t i;

	for (i = 0; i < len; i++) {
		hash ^= (uint32_t)bytes[i];
		hash *= FNV1A_32_PRIME;
	}

	return hash;
}

doca_error_t flow_ct_register_params(void)
{
	doca_error_t result;

	struct doca_argp_param *dev_pci_addr_param;

	/* Create and register DOCA Flow CT device PCI address */
	result = doca_argp_param_create(&dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(dev_pci_addr_param, "p");
	doca_argp_param_set_long_name(dev_pci_addr_param, "pci-addr");
	doca_argp_param_set_description(dev_pci_addr_param, "DOCA Flow CT device PCI address");
	doca_argp_param_set_callback(dev_pci_addr_param, pci_addr_callback);
	doca_argp_param_set_type(dev_pci_addr_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(dev_pci_addr_param);
	doca_argp_param_set_multiplicity(dev_pci_addr_param);
	result = doca_argp_register_param(dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

doca_error_t init_doca_flow_ct(uint32_t flags,
			       uint32_t nb_arm_queues,
			       uint32_t nb_ctrl_queues,
			       uint32_t nb_user_actions,
			       doca_flow_ct_flow_log_cb flow_log_cb,
			       uint32_t nb_ipv4_sessions,
			       uint32_t nb_ipv6_sessions,
			       uint32_t dup_filter_sz,
			       bool o_match_inner,
			       struct doca_flow_meta *o_zone_mask,
			       struct doca_flow_meta *o_modify_mask,
			       bool r_match_inner,
			       struct doca_flow_meta *r_zone_mask,
			       struct doca_flow_meta *r_modify_mask)
{
	struct doca_flow_ct_cfg ct_cfg;
	doca_error_t result;

	if (o_zone_mask == NULL || o_modify_mask == NULL) {
		DOCA_LOG_ERR("Origin masks can't be null");
		return DOCA_ERROR_INVALID_VALUE;
	} else if (r_zone_mask == NULL || r_modify_mask == NULL) {
		DOCA_LOG_ERR("Reply masks can't be null");
		return DOCA_ERROR_INVALID_VALUE;
	}

	memset(&ct_cfg, 0, sizeof(ct_cfg));

	ct_cfg.flags |= DOCA_FLOW_CT_FLAG_MANAGED;

	ct_cfg.nb_arm_queues = nb_arm_queues;
	ct_cfg.nb_ctrl_queues = nb_ctrl_queues;
	ct_cfg.nb_user_actions = nb_user_actions;
	ct_cfg.aging_core = nb_arm_queues + 1;
	ct_cfg.flow_log_cb = flow_log_cb;
	ct_cfg.nb_arm_sessions[DOCA_FLOW_CT_SESSION_IPV4] = nb_ipv4_sessions;
	ct_cfg.nb_arm_sessions[DOCA_FLOW_CT_SESSION_IPV6] = nb_ipv6_sessions;
	ct_cfg.dup_filter_sz = dup_filter_sz;

	ct_cfg.direction[0].match_inner = o_match_inner;
	ct_cfg.direction[0].zone_match_mask = o_zone_mask;
	ct_cfg.direction[0].meta_modify_mask = o_modify_mask;
	ct_cfg.direction[1].match_inner = r_match_inner;
	ct_cfg.direction[1].zone_match_mask = r_zone_mask;
	ct_cfg.direction[1].meta_modify_mask = r_modify_mask;

	ct_cfg.flags |= flags;

	result = doca_flow_ct_init(&ct_cfg);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to initialize DOCA Flow CT: %s", doca_error_get_name(result));

	return result;
}

doca_error_t flow_ct_dpdk_init(int argc, char **dpdk_argv)
{
	char *argv[argc + DPDK_ADDITIONAL_ARG];

	memcpy(argv, dpdk_argv, sizeof(argv[0]) * argc);
	argv[argc++] = "-a";
	argv[argc++] = "pci:00:00.0";

	return dpdk_init(argc, argv);
}

doca_error_t flow_ct_capable(struct doca_devinfo *dev_info)
{
	return doca_flow_ct_cap_is_dev_supported(dev_info);
}

uint32_t flow_ct_hash_6tuple(const struct doca_flow_ct_match *match, bool is_ipv6, bool is_reply)
{
	uint32_t hash = FNV1A_32_OFFSET;
	uint32_t zone;

	if (is_ipv6) {
		zone = doca_flow_ct_meta_get_match_zone(match->ipv6.metadata, is_reply);
		hash = fnv1a_32bit_hash(match->ipv6.src_ip, 4, hash);
		hash = fnv1a_32bit_hash(match->ipv6.dst_ip, 4, hash);
		hash = fnv1a_32bit_hash(&match->ipv6.l4_port.dst_port, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv6.l4_port.src_port, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv6.next_proto, 1, hash);
		hash = fnv1a_32bit_hash(&zone, 1, hash);
	} else {
		zone = doca_flow_ct_meta_get_match_zone(match->ipv4.metadata, is_reply);
		hash = fnv1a_32bit_hash(&match->ipv4.src_ip, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv4.dst_ip, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv4.l4_port.dst_port, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv4.l4_port.src_port, 1, hash);
		hash = fnv1a_32bit_hash(&match->ipv4.next_proto, 1, hash);
		hash = fnv1a_32bit_hash(&zone, 1, hash);
	}

	return hash;
}

void cleanup_procedure(struct doca_flow_pipe *ct_pipe, int nb_ports, struct doca_flow_port *ports[])
{
	doca_error_t result;

	if (ct_pipe != NULL)
		doca_flow_pipe_destroy(ct_pipe);

	result = stop_doca_flow_ports(nb_ports, ports);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to stop doca flow ports: %s", doca_error_get_descr(result));

	doca_flow_ct_destroy();
	doca_flow_destroy();
}

doca_error_t create_ct_root_pipe(struct doca_flow_port *port,
				 bool is_ipv4,
				 bool is_ipv6,
				 enum doca_flow_l4_meta l4_type,
				 struct doca_flow_pipe *fwd_pipe,
				 struct entries_status *status,
				 struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&fwd, 0, sizeof(fwd));

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "root", DOCA_FLOW_PIPE_CONTROL, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(pipe_cfg, NULL, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create root pipe: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	doca_flow_pipe_cfg_destroy(pipe_cfg);

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = fwd_pipe;

	/* Match IPv4 and IPv6 TCP packets */
	if (is_ipv4) {
		memset(&match, 0, sizeof(match));
		match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
		match.parser_meta.outer_l4_type = l4_type;
		match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
		match.outer.ip4.dst_ip = BE_IPV4_ADDR(1, 1, 1, 1);
		result = doca_flow_pipe_control_add_entry(0,
							  1,
							  *pipe,
							  &match,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  &fwd,
							  status,
							  NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add root pipe IPv4 entry: %s", doca_error_get_descr(result));
			return result;
		}
	}

	if (is_ipv6) {
		memset(&match, 0, sizeof(match));
		match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
		match.parser_meta.outer_l4_type = l4_type;
		match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP6;
		match.outer.ip6.dst_ip[0] = 0x01010101;
		match.outer.ip6.dst_ip[1] = 0x01010101;
		match.outer.ip6.dst_ip[2] = 0x01010101;
		match.outer.ip6.dst_ip[3] = 0x01010101;
		match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV6;
		result = doca_flow_pipe_control_add_entry(0,
							  1,
							  *pipe,
							  &match,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  NULL,
							  &fwd,
							  status,
							  NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add IPv6 root pipe entry: %s", doca_error_get_descr(result));
			return result;
		}
	}

	/* Drop non TCP packets */
	fwd.type = DOCA_FLOW_FWD_DROP;
	memset(&match, 0, sizeof(match));
	result = doca_flow_pipe_control_add_entry(0,
						  2,
						  *pipe,
						  &match,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  &fwd,
						  status,
						  NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add root pipe drop entry: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, 0);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to process root entry: %s", doca_error_get_descr(result));

	return result;

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}
