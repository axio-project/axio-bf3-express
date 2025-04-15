/*
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES.
 * Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Source file with functions for Flow Steering Rules tables */

#include <malloc.h>
#include <stdint.h>
#include <assert.h>

#include "flow_steering_utils.h"

enum matcher_criteria {
	MATCHER_CRITERIA_EMPTY = 0,
	MATCHER_CRITERIA_OUTER = 1 << 0,
	MATCHER_CRITERIA_MISC  = 1 << 1,
	MATCHER_CRITERIA_INNER = 1 << 2,
	MATCHER_CRITERIA_MISC2 = 1 << 3,
	MATCHER_CRITERIA_MISC3 = 1 << 4,
};

struct mlx5_ifc_dr_match_spec_bits {
	uint8_t smac_47_16[0x20];

	uint8_t smac_15_0[0x10];
	uint8_t ethertype[0x10];

	uint8_t dmac_47_16[0x20];

	uint8_t dmac_15_0[0x10];
	uint8_t first_prio[0x3];
	uint8_t first_cfi[0x1];
	uint8_t first_vid[0xc];

	uint8_t ip_protocol[0x8];
	uint8_t ip_dscp[0x6];
	uint8_t ip_ecn[0x2];
	uint8_t cvlan_tag[0x1];
	uint8_t svlan_tag[0x1];
	uint8_t frag[0x1];
	uint8_t ip_version[0x4];
	uint8_t tcp_flags[0x9];

	uint8_t tcp_sport[0x10];
	uint8_t tcp_dport[0x10];

	uint8_t reserved_at_c0[0x18];
	uint8_t ip_ttl_hoplimit[0x8];

	uint8_t udp_sport[0x10];
	uint8_t udp_dport[0x10];

	uint8_t src_ip_127_96[0x20];

	uint8_t src_ip_95_64[0x20];

	uint8_t src_ip_63_32[0x20];

	uint8_t src_ip_31_0[0x20];

	uint8_t dst_ip_127_96[0x20];

	uint8_t dst_ip_95_64[0x20];

	uint8_t dst_ip_63_32[0x20];

	uint8_t dst_ip_31_0[0x20];
};

/* Every usage of this value is in bytes */
#define MATCH_VAL_BSIZE 64

static struct flow_matcher
*create_flow_matcher_sw_steer_rx(struct ibv_context *ibv_ctx,
				 struct mlx5dv_flow_match_parameters *match_mask,
				 enum mlx5dv_dr_domain_type type)
{
	struct flow_matcher *flow_match;

	flow_match = (struct flow_matcher *)calloc(1, sizeof(*flow_match));
	assert(flow_match);

	/* SW steering table and matcher are not used for RX steering */
	flow_match->dr_table_sws = NULL;
	flow_match->dr_matcher_sws = NULL;

	flow_match->dr_domain = mlx5dv_dr_domain_create(ibv_ctx, type);
	if (!flow_match->dr_domain) {
		printf("Fail creating dr_domain (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_table_root = mlx5dv_dr_table_create(flow_match->dr_domain, 0);
	if (!flow_match->dr_table_root) {
		printf("Fail creating dr_table (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_matcher_root = mlx5dv_dr_matcher_create(flow_match->dr_table_root, 0,
							       MATCHER_CRITERIA_OUTER, match_mask);
	if (!flow_match->dr_matcher_root) {
		printf("Fail creating dr_matcher (errno %d)\n", errno);
		goto error;
	}
	return flow_match;

error:
	return NULL;
}

static struct flow_matcher
*create_flow_matcher_sw_steer_tx(struct ibv_context *ibv_ctx,
				 struct mlx5dv_flow_match_parameters *match_mask,
				 enum mlx5dv_dr_domain_type type)
{
	struct flow_matcher *flow_match;

	flow_match = (struct flow_matcher *)calloc(1, sizeof(*flow_match));
	assert(flow_match);

	flow_match->dr_domain = mlx5dv_dr_domain_create(ibv_ctx, type);
	if (!flow_match->dr_domain) {
		printf("Fail creating dr_domain (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_table_root = mlx5dv_dr_table_create(flow_match->dr_domain, 0);
	if (!flow_match->dr_table_root) {
		printf("Fail creating dr_table_root (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_matcher_root = mlx5dv_dr_matcher_create(flow_match->dr_table_root, 0,
							       MATCHER_CRITERIA_OUTER, match_mask);
	if (!flow_match->dr_matcher_root) {
		printf("Fail creating dr_matcher_root (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_table_sws = mlx5dv_dr_table_create(flow_match->dr_domain, 1);
	if (!flow_match->dr_table_sws) {
		printf("Fail creating dr_table_sws (errno %d)\n", errno);
		goto error;
	}

	flow_match->dr_matcher_sws = mlx5dv_dr_matcher_create(flow_match->dr_table_sws, 0,
							      MATCHER_CRITERIA_OUTER, match_mask);
	if (!flow_match->dr_matcher_sws) {
		printf("Fail creating dr_matcher_sws (errno %d)\n", errno);
		goto error;
	}

	return flow_match;

error:
	return NULL;
}

static struct flow_rule *create_flow_rule_rx(struct flow_matcher *flow_matcher,
					     struct mlx5dv_devx_obj *tir_obj,
					     struct mlx5dv_flow_match_parameters *match_value)
{
	struct mlx5dv_dr_action *actions[1];
	struct flow_rule *flow_rule;

	flow_rule = (struct flow_rule *)calloc(1, sizeof(*flow_rule));
	assert(flow_rule);

	flow_rule->action = mlx5dv_dr_action_create_dest_devx_tir(tir_obj);
	if (!flow_rule->action) {
		printf("Failed creating TIR action (errno %d).\n", errno);
		goto err_out;
	}
	actions[0] = flow_rule->action;

	flow_rule->dr_rule = mlx5dv_dr_rule_create(flow_matcher->dr_matcher_root, match_value, 1,
						   actions);
	if (!flow_rule->dr_rule) {
		printf("Fail creating dr_rule (errno %d).\n", errno);
		goto err_out;
	}

	return flow_rule;

err_out:
	if (flow_rule->action)
		mlx5dv_dr_action_destroy(flow_rule->action);
	free(flow_rule);
	return NULL;
}

static struct flow_rule *create_flow_rule_tx(struct flow_matcher *flow_matcher,
					     struct mlx5dv_flow_match_parameters *match_value)
{
	struct mlx5dv_dr_action *actions[1];
	struct flow_rule *flow_rule;

	flow_rule = (struct flow_rule *)calloc(1, sizeof(*flow_rule));
	assert(flow_rule);

	flow_rule->action = mlx5dv_dr_action_create_dest_vport(flow_matcher->dr_domain, 0xFFFF);
	if (!flow_rule->action) {
		printf("Failed creating dest vport action (errno %d).\n", errno);
		goto err_out;
	}
	actions[0] = flow_rule->action;

	flow_rule->dr_rule = mlx5dv_dr_rule_create(flow_matcher->dr_matcher_sws, match_value, 1,
						   actions);
	if (!flow_rule->dr_rule) {
		printf("Fail creating dr_rule (errno %d).\n", errno);
		goto err_out;
	}

	return flow_rule;

err_out:
	if (flow_rule->action)
		mlx5dv_dr_action_destroy(flow_rule->action);
	free(flow_rule);
	return NULL;
}

static struct flow_rule *create_flow_rule_tx_table(struct flow_matcher *flow_matcher,
						   struct mlx5dv_flow_match_parameters *match_value)
{
	struct mlx5dv_dr_action *actions[1];
	struct flow_rule *flow_rule;

	flow_rule = (struct flow_rule *)calloc(1, sizeof(*flow_rule));
	assert(flow_rule);

	flow_rule->action = mlx5dv_dr_action_create_dest_table(flow_matcher->dr_table_sws);
	if (!flow_rule->action) {
		printf("Failed creating dest SWS table action (errno %d).\n", errno);
		goto err_out;
	}
	actions[0] = flow_rule->action;

	flow_rule->dr_rule = mlx5dv_dr_rule_create(flow_matcher->dr_matcher_root, match_value, 1,
						   actions);
	if (!flow_rule->dr_rule) {
		printf("Fail creating dr_rule (errno %d).\n", errno);
		goto err_out;
	}

	return flow_rule;

err_out:
	if (flow_rule->action)
		mlx5dv_dr_action_destroy(flow_rule->action);
	free(flow_rule);
	return NULL;
}

struct flow_matcher *create_matcher_rx(struct ibv_context *ibv_ctx)
{
	struct mlx5dv_flow_match_parameters *match_mask;
	struct flow_matcher *matcher;
	int match_mask_size;

	/* mask & match value */
	match_mask_size = sizeof(*match_mask) + MATCH_VAL_BSIZE;
	match_mask = (struct mlx5dv_flow_match_parameters *)calloc(1, match_mask_size);
	assert(match_mask);

	match_mask->match_sz = MATCH_VAL_BSIZE;
	DEVX_SET(dr_match_spec, match_mask->match_buf, smac_47_16, 0xffffffff);
	DEVX_SET(dr_match_spec, match_mask->match_buf, smac_15_0, 0xffff);

	matcher = create_flow_matcher_sw_steer_rx(ibv_ctx, match_mask,
						  MLX5DV_DR_DOMAIN_TYPE_NIC_RX);
	free(match_mask);

	return matcher;
}

struct flow_matcher *create_matcher_tx(struct ibv_context *ibv_ctx)
{
	struct mlx5dv_flow_match_parameters *match_mask;
	struct flow_matcher *matcher;
	int match_mask_size;

	/* mask & match value */
	match_mask_size = sizeof(*match_mask) + MATCH_VAL_BSIZE;
	match_mask = (struct mlx5dv_flow_match_parameters *)calloc(1, match_mask_size);
	assert(match_mask);

	match_mask->match_sz = MATCH_VAL_BSIZE;
	DEVX_SET(dr_match_spec, match_mask->match_buf, dmac_47_16, 0xffffffff);
	DEVX_SET(dr_match_spec, match_mask->match_buf, dmac_15_0, 0xffff);
	matcher = create_flow_matcher_sw_steer_tx(ibv_ctx, match_mask, MLX5DV_DR_DOMAIN_TYPE_FDB);
	free(match_mask);

	return matcher;
}

struct flow_rule *create_rule_rx_mac_match(struct flow_matcher *flow_match,
					   struct mlx5dv_devx_obj *tir_obj, uint64_t smac)
{
	struct mlx5dv_flow_match_parameters *match_value;
	struct flow_rule *flow_rule;
	int match_value_size;

	/* mask & match value */
	match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
	match_value = (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
	assert(match_value);

	match_value->match_sz = MATCH_VAL_BSIZE;
	DEVX_SET(dr_match_spec, match_value->match_buf, smac_47_16, smac >> 16);
	DEVX_SET(dr_match_spec, match_value->match_buf, smac_15_0, smac % (1 << 16));
	flow_rule = create_flow_rule_rx(flow_match, tir_obj, match_value);
	free(match_value);

	return flow_rule;
}

struct flow_rule *create_rule_tx_fwd_to_vport(struct flow_matcher *flow_match, uint64_t dmac)
{
	struct mlx5dv_flow_match_parameters *match_value;
	struct flow_rule *flow_rule;
	int match_value_size;

	/* mask & match value */
	match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
	match_value = (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
	assert(match_value);

	match_value->match_sz = MATCH_VAL_BSIZE;
	DEVX_SET(dr_match_spec, match_value->match_buf, dmac_47_16, dmac >> 16);
	DEVX_SET(dr_match_spec, match_value->match_buf, dmac_15_0, dmac % (1 << 16));
	flow_rule = create_flow_rule_tx(flow_match, match_value);
	free(match_value);

	return flow_rule;
}

struct flow_rule *create_rule_tx_fwd_to_sws_table(struct flow_matcher *flow_match, uint64_t dmac)
{
	struct mlx5dv_flow_match_parameters *match_value;
	struct flow_rule *flow_rule;
	int match_value_size;

	/* mask & match value */
	match_value_size = sizeof(*match_value) + MATCH_VAL_BSIZE;
	match_value = (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
	assert(match_value);

	match_value->match_sz = MATCH_VAL_BSIZE;
	DEVX_SET(dr_match_spec, match_value->match_buf, dmac_47_16, dmac >> 16);
	DEVX_SET(dr_match_spec, match_value->match_buf, dmac_15_0, dmac % (1 << 16));
	flow_rule = create_flow_rule_tx_table(flow_match, match_value);
	free(match_value);

	return flow_rule;
}

int destroy_matcher(struct flow_matcher *matcher)
{
	int err;

	err = mlx5dv_dr_matcher_destroy(matcher->dr_matcher_root);
	if (err)
		return err;

	err = mlx5dv_dr_table_destroy(matcher->dr_table_root);
	if (err)
		return err;

	if (matcher->dr_matcher_sws) {
		err = mlx5dv_dr_matcher_destroy(matcher->dr_matcher_sws);
		if (err)
			return err;
	}

	if (matcher->dr_table_sws) {
		err = mlx5dv_dr_table_destroy(matcher->dr_table_sws);
		if (err)
			return err;
	}

	err = mlx5dv_dr_domain_destroy(matcher->dr_domain);
	if (err)
		return err;

	free(matcher);

	return 0;
}

int destroy_rule(struct flow_rule *rule)
{
	int err;

	err = mlx5dv_dr_rule_destroy(rule->dr_rule);
	if (err)
		return err;

	err = mlx5dv_dr_action_destroy(rule->action);
	if (err)
		return err;

	free(rule);

	return 0;
}
