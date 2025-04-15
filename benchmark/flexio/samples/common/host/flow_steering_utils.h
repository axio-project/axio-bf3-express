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

/* Header file with declaration of structures and functions for
 * Flow Steering Rules tables */

#ifndef __COM_FLOW_HOST_H__
#define __COM_FLOW_HOST_H__

#include <infiniband/mlx5dv.h>

/* The cumulative structure of the flow matcher */
struct flow_matcher {
	struct mlx5dv_dr_domain *dr_domain;
	struct mlx5dv_dr_table *dr_table_root;
	struct mlx5dv_dr_matcher *dr_matcher_root;
	struct mlx5dv_dr_table *dr_table_sws;
	struct mlx5dv_dr_matcher *dr_matcher_sws;
};

/* The cumulative structure of the flow rule */
struct flow_rule {
	struct mlx5dv_dr_action *action;
	struct mlx5dv_dr_rule *dr_rule;
};

/* Create a flow matcher for Ethernet packets received on the NIC.
 *  ibv_ctx - context of the IBV device.
 */
struct flow_matcher *create_matcher_rx(struct ibv_context *ibv_ctx);

/* Create a SW flow steering rule for ethernet packets received on the NIC.
 *  ibv_ctx - context of the IBV device.
 *  tir_obj - TIR mlx5dv object
 *  smac - Source MAC address
 */
struct flow_rule *create_rule_rx_mac_match(struct flow_matcher *flow_match,
					   struct mlx5dv_devx_obj *tir_obj, uint64_t smac);

/* Create a flow matcher for Ethernet packets transmitted on the NIC.
 *  ibv_ctx - context of the IBV device.
 */
struct flow_matcher *create_matcher_tx(struct ibv_context *ibv_ctx);

/* Create a flow rule for Ethernet packets transmitted on the NIC.
 *  flow_match - pointer to the previously created flow_matcher structure.
 *  dmac - Destination MAC address
 */
struct flow_rule *create_rule_tx_fwd_to_vport(struct flow_matcher *flow_match,
					      uint64_t dmac);

/* Create a flow rule for Ethernet packets transmitted on the NIC through
 *  thw software-steering table.
 *  flow_match - pointer to the previously created flow_matcher structure.
 *  dmac - Destination MAC address
 */
struct flow_rule *create_rule_tx_fwd_to_sws_table(struct flow_matcher *flow_match,
						  uint64_t dmac);
/* Destroy the flow matcher.
 *  matcher - the matcher to destroy.
 */
int destroy_matcher(struct flow_matcher *matcher);

/* Destroy the flow rule.
 *  rule - the rule to destroy.
 */
int destroy_rule(struct flow_rule *rule);

#endif
