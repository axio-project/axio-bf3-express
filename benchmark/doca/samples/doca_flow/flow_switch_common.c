/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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
#include <string.h>

#include <rte_byteorder.h>

#include <doca_argp.h>
#include <doca_log.h>
#include <doca_dpdk.h>

#include <dpdk_utils.h>

#include "../common.h"

#include "flow_switch_common.h"

#define FLOW_SWITCH_DEV_ARGS "dv_flow_en=2,fdb_def_rule_en=0,vport_match=1,repr_matching_en=0,dv_xmeta_en=4"
#define FLOW_SWITCH_REP_ARG ",representor="

DOCA_LOG_REGISTER(flow_switch_common);

doca_error_t init_flow_switch_dpdk(int argc, char **dpdk_argv)
{
	char *argv[argc + 2];

	memcpy(argv, dpdk_argv, sizeof(argv[0]) * argc);
	argv[argc++] = "-a";
	argv[argc++] = "pci:00:00.0";

	return dpdk_init(argc, argv);
}

/*
 * Get DOCA Flow switch device PCI
 *
 * @param [in]: input paramete
 * @config [out]: configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t param_flow_switch_pci_callback(void *param, void *config)
{
	struct flow_switch_ctx *ctx = (struct flow_switch_ctx *)config;
	char *n = (char *)param;

	ctx->dev_arg[ctx->nb_ports++] = n;

	return DOCA_SUCCESS;
}

/*
 * Get DOCA Flow switch device representor
 *
 * @param [in]: input paramete
 * @config [out]: configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t param_flow_switch_rep_callback(void *param, void *config)
{
	struct flow_switch_ctx *ctx = (struct flow_switch_ctx *)config;
	char *n = (char *)param;

	ctx->rep_arg[ctx->nb_reps++] = n;

	return DOCA_SUCCESS;
}

/*
 * Get DOCA Flow switch mode
 *
 * @param [in]: input paramete
 * @config [out]: configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t param_flow_switch_exp_callback(void *param, void *config)
{
	struct flow_switch_ctx *ctx = (struct flow_switch_ctx *)config;

	ctx->is_expert = *(bool *)param;

	return DOCA_SUCCESS;
}

doca_error_t register_doca_flow_switch_param(void)
{
	doca_error_t result;
	struct doca_argp_param *pci_param;
	struct doca_argp_param *rep_param;
	struct doca_argp_param *exp_param;

	result = doca_argp_param_create(&pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(pci_param, "p");
	doca_argp_param_set_long_name(pci_param, "pci");
	doca_argp_param_set_description(pci_param, "device PCI address");
	doca_argp_param_set_callback(pci_param, param_flow_switch_pci_callback);
	doca_argp_param_set_type(pci_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(pci_param);
	doca_argp_param_set_multiplicity(pci_param);
	result = doca_argp_register_param(pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_argp_param_create(&rep_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(rep_param, "r");
	doca_argp_param_set_long_name(rep_param, "rep");
	doca_argp_param_set_description(rep_param, "device representor");
	doca_argp_param_set_callback(rep_param, param_flow_switch_rep_callback);
	doca_argp_param_set_type(rep_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(rep_param);
	doca_argp_param_set_multiplicity(rep_param);
	result = doca_argp_register_param(rep_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_argp_param_create(&exp_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(exp_param, "exp");
	doca_argp_param_set_long_name(exp_param, "expert-mode");
	doca_argp_param_set_description(exp_param, "set expert mode");
	doca_argp_param_set_callback(exp_param, param_flow_switch_exp_callback);
	doca_argp_param_set_type(exp_param, DOCA_ARGP_TYPE_BOOLEAN);
	result = doca_argp_register_param(exp_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register flow switch ARGP param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

doca_error_t init_doca_flow_switch_common(struct flow_switch_ctx *ctx)
{
	char *port_args[FLOW_SWITCH_PORTS_MAX] = {0};
	char *dpdk_arg;
	doca_error_t result;
	int i;

	for (i = 0; i < ctx->nb_ports; i++) {
		/* Probe dpdk dev by doca_dev */
		result = open_doca_device_with_pci(ctx->dev_arg[i], NULL, &ctx->doca_dev[i]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			goto quit;
		}

		if (ctx->rep_arg[i]) {
			port_args[i] = calloc(1,
					      strlen(FLOW_SWITCH_DEV_ARGS) + strlen(FLOW_SWITCH_REP_ARG) +
						      strlen(ctx->rep_arg[i]) + 1);
			if (!port_args[i]) {
				DOCA_LOG_ERR("Failed to allocate dpdk args port: %d", i);
				result = DOCA_ERROR_NO_MEMORY;
				goto quit;
			}
			strcpy(port_args[i], FLOW_SWITCH_DEV_ARGS);
			strcat(port_args[i], FLOW_SWITCH_REP_ARG);
			strcat(port_args[i], ctx->rep_arg[i]);
			dpdk_arg = port_args[i];
		} else {
			dpdk_arg = FLOW_SWITCH_DEV_ARGS;
		}
		result = doca_dpdk_port_probe(ctx->doca_dev[i], dpdk_arg);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe DOCA device: %s", doca_error_get_descr(result));
			goto quit;
		}
	}

quit:
	for (i = 0; i < ctx->nb_ports; i++)
		free(port_args[i]);
	if (result != DOCA_SUCCESS)
		destroy_doca_flow_switch_common(ctx);
	return result;
}

void destroy_doca_flow_switch_common(struct flow_switch_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->nb_ports; i++) {
		if (ctx->doca_dev[i]) {
			doca_dev_close(ctx->doca_dev[i]);
			ctx->doca_dev[i] = NULL;
		}
	}
}
