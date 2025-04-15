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

#include <doca_argp.h>
#include <doca_dpdk.h>
#include <doca_log.h>
#include <dpdk_utils.h>
#include <doca_flow.h>

#include <rte_ethdev.h>

#include <signal.h>
#include <stdlib.h>

#include "ip_frag_dp.h"

#define IP_FRAG_TBL_TIMEOUT_MS 2
#define IP_FRAG_TBL_SIZE 2048

DOCA_LOG_REGISTER(IP_FRAG);

/*
 * Handle a given signal
 *
 * @signum [in]: signal number
 */
static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		force_stop = true;
	}
}

/*
 * Callback to handle application mode
 *
 * @param [in]: mode string.
 * @config [in]: Ip_frag config.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_mode_callback(void *param, void *config)
{
	struct ip_frag_config *cfg = config;
	const char *mode_str = param;

	if (!strcmp(mode_str, "bidir")) {
		cfg->mode = IP_FRAG_MODE_BIDIR;
	} else if (!strcmp(mode_str, "multiport")) {
		cfg->mode = IP_FRAG_MODE_MULTIPORT;
	} else {
		DOCA_LOG_ERR("Unsupported mode: %s", mode_str);
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

/*
 * Callback to handle application mtu
 *
 * @param [in]: MTU integer.
 * @config [in]: Ip_frag config.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_mtu_callback(void *param, void *config)
{
	const uint32_t mtu = *(const uint32_t *)param;
	struct ip_frag_config *cfg = config;

	if (mtu < RTE_ETHER_MIN_MTU || mtu > RTE_ETHER_MAX_JUMBO_FRAME_LEN) {
		DOCA_LOG_ERR("Invalid MTU: %u", mtu);
		return DOCA_ERROR_INVALID_VALUE;
	}
	cfg->mtu = mtu;

	return DOCA_SUCCESS;
}

/*
 * Callback to handle fragmentation table timeout in ms
 *
 * @param [in]: timeout integer in ms.
 * @config [in]: Ip_frag config.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_tbl_timeout_callback(void *param, void *config)
{
	const uint32_t timeout = *(const uint32_t *)param;
	struct ip_frag_config *cfg = config;

	cfg->frag_tbl_timeout = timeout;

	return DOCA_SUCCESS;
}

/*
 * Callback to handle fragmentation table size
 *
 * @param [in]: size integer.
 * @config [in]: Ip_frag config.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_tbl_size_callback(void *param, void *config)
{
	const uint32_t size = *(const uint32_t *)param;
	struct ip_frag_config *cfg = config;

	cfg->frag_tbl_size = size;

	return DOCA_SUCCESS;
}

/*
 * Callback to handle fragmentation table size
 *
 * @param [in]: size integer.
 * @config [in]: Ip_frag config.
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_mbuf_chain_callback(void *param, void *config)
{
	const bool mbuf_chain = *(const bool *)param;
	struct ip_frag_config *cfg = config;

	cfg->mbuf_chain = mbuf_chain;

	return DOCA_SUCCESS;
}

/*
 * Handle application parameters registration
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t ip_frag_register_params(void)
{
	struct doca_argp_param *app_mode_param;
	struct doca_argp_param *mtu_param;
	struct doca_argp_param *frag_tbl_timeout_param;
	struct doca_argp_param *frag_tbl_size_param;
	struct doca_argp_param *mbuf_chain_param;
	doca_error_t result;

	/* Create and register ip_frag application mode */
	result = doca_argp_param_create(&app_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(app_mode_param, "m");
	doca_argp_param_set_long_name(app_mode_param, "mode");
	doca_argp_param_set_description(
		app_mode_param,
		"Ip_frag application mode."
		" Bidirectional mode forwards packets between a single reassembly port and a single fragmentation port."
		" Multiport mode forwards packets between two pairs of reassembly and fragmentation ports."
		" For more information consult DOCA IP Fragmentation Application Guide."
		" Format: bidir, multiport");
	doca_argp_param_set_callback(app_mode_param, ip_frag_mode_callback);
	doca_argp_param_set_type(app_mode_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(app_mode_param);
	result = doca_argp_register_param(app_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register ip_frag maximum MTU size */
	result = doca_argp_param_create(&mtu_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(mtu_param, "u");
	doca_argp_param_set_long_name(mtu_param, "mtu");
	doca_argp_param_set_description(mtu_param, "MTU size");
	doca_argp_param_set_callback(mtu_param, ip_frag_mtu_callback);
	doca_argp_param_set_type(mtu_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(mtu_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register frag table timeout */
	result = doca_argp_param_create(&frag_tbl_timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(frag_tbl_timeout_param, "t");
	doca_argp_param_set_long_name(frag_tbl_timeout_param, "frag-aging-timeout");
	doca_argp_param_set_description(
		frag_tbl_timeout_param,
		"Aging timeout of fragments pending packet reassembly in the fragmentation table (in ms)");
	doca_argp_param_set_callback(frag_tbl_timeout_param, ip_frag_tbl_timeout_callback);
	doca_argp_param_set_type(frag_tbl_timeout_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(frag_tbl_timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register frag table size */
	result = doca_argp_param_create(&frag_tbl_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(frag_tbl_size_param, "s");
	doca_argp_param_set_long_name(frag_tbl_size_param, "frag-tbl-size");
	doca_argp_param_set_description(
		frag_tbl_size_param,
		"Frag table size, i.e. maximum amount of concurrent defragmentation contexts per worker thread");
	doca_argp_param_set_callback(frag_tbl_size_param, ip_frag_tbl_size_callback);
	doca_argp_param_set_type(frag_tbl_size_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(frag_tbl_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register ip_frag mbuf chaining optimization toggle */
	result = doca_argp_param_create(&mbuf_chain_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(mbuf_chain_param, "c");
	doca_argp_param_set_long_name(mbuf_chain_param, "mbuf-chain");
	doca_argp_param_set_description(mbuf_chain_param, "Enable mbuf chaining");
	doca_argp_param_set_callback(mbuf_chain_param, ip_frag_mbuf_chain_callback);
	doca_argp_param_set_type(mbuf_chain_param, DOCA_ARGP_TYPE_BOOLEAN);
	result = doca_argp_register_param(mbuf_chain_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

static doca_error_t ip_frag_dpdk_config_num_ports(struct application_dpdk_config *dpdk_config)
{
	uint16_t nb_ports = rte_eth_dev_count_avail();

	if (nb_ports > IP_FRAG_PORT_NUM) {
		DOCA_LOG_ERR("Invalid number (%u) of ports, max %u", nb_ports, IP_FRAG_PORT_NUM);
		return DOCA_ERROR_INVALID_VALUE;
	}
	dpdk_config->port_config.nb_ports = nb_ports;

	return DOCA_SUCCESS;
}

/*
 * Application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	struct application_dpdk_config dpdk_config = {0};
	struct ip_frag_config cfg = {
		.mtu = RTE_ETHER_MAX_LEN,
		.mbuf_chain = false,
		.frag_tbl_timeout = IP_FRAG_TBL_TIMEOUT_MS,
		.frag_tbl_size = IP_FRAG_TBL_SIZE,
	};
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;
	doca_error_t result;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto app_exit;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto app_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto app_exit;

	DOCA_LOG_INFO("Starting the application, pid %d", getpid());

	result = doca_argp_init("doca_ip_frag", &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto app_exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);

	result = ip_frag_register_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register Flow Ct application parameters: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = ip_frag_dpdk_config_num_ports(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to configure num ports");
		goto dpdk_cleanup;
	}

	/* update queues and ports */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update ports and queues");
		goto dpdk_cleanup;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* run app */
	result = ip_frag(&cfg, &dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("ip_frag() encountered an error: %s", doca_error_get_descr(result));
		goto dpdk_ports_queues_cleanup;
	}

	exit_status = EXIT_SUCCESS;
dpdk_ports_queues_cleanup:
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
	dpdk_fini();
argp_cleanup:
	doca_argp_destroy();
app_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Application finished successfully");
	else
		DOCA_LOG_INFO("Application finished with errors");
	return exit_status;
}
