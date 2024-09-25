/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#ifndef FLOW_SWITCH_COMMON_H_
#define FLOW_SWITCH_COMMON_H_

#include <rte_byteorder.h>

#include <doca_flow.h>
#include <doca_dev.h>

#define FLOW_SWITCH_PORTS_MAX (2)

/* doca flow switch context */
struct flow_switch_ctx {
	bool is_expert;					  /* switch expert mode */
	uint16_t nb_ports;				  /* switch port number */
	uint16_t nb_reps;				  /* switch port number */
	const char *dev_arg[FLOW_SWITCH_PORTS_MAX];	  /* dpdk dev_arg */
	const char *rep_arg[FLOW_SWITCH_PORTS_MAX];	  /* dpdk rep_arg */
	struct doca_dev *doca_dev[FLOW_SWITCH_PORTS_MAX]; /* port doca_dev */
};

/*
 * Init DOCA Flow switch
 *
 * @argc [in]: dpdk argc
 * @dpdk_argv [in]: dpdk argv
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t init_flow_switch_dpdk(int argc, char **dpdk_argv);

/*
 * Register DOCA Flow switch parameter
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t register_doca_flow_switch_param(void);

/*
 * Init DOCA Flow switch
 *
 * @ctx [in]: flow switch context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t init_doca_flow_switch_common(struct flow_switch_ctx *ctx);

/*
 * Destroy dOCA Flow switch context
 *
 * @ctx [in]: flow switch context
 */
void destroy_doca_flow_switch_common(struct flow_switch_ctx *ctx);

#endif /* FLOW_SWITCH_COMMON_H_ */
