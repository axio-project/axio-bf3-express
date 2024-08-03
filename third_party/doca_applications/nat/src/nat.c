/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <signal.h>

#include <doca_argp.h>
#include <doca_log.h>

#include "nat_core.h"

DOCA_LOG_REGISTER(NAT);

static bool force_quit;		/* Set when signal is received */

/*
 * Signals handler function to handle SIGINT and SIGTERM signals
 *
 * @signum [in]: signal number
 */
static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		force_quit = true;
	}
}

/*
 * NAT application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = 2,
		.port_config.nb_queues = 2,
		.port_config.nb_hairpin_q = 2,
		.sft_config = {0},
	};

	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
	struct nat_cfg app_cfg = {0};
	struct nat_rule_match *nat_rules = NULL;
	int nat_num_rules;

	force_quit = false;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	result = doca_argp_init("doca_nat", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = register_nat_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register application params: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update application ports and queues: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_destroy;
	}

	/* init doca flows and ports */
	result = nat_init(&app_cfg, &dpdk_config);
	if (result != DOCA_SUCCESS) {
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	/* parse nat rule from json and add to internal struct */
	result = parsing_nat_rules(app_cfg.json_path, app_cfg.mode, &nat_num_rules, &nat_rules);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse NAT rules from JSON: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto nat_cleanup;
	}

	/* set nat rules to pipes */
	result = nat_pipes_init(nat_rules, nat_num_rules, &app_cfg, dpdk_config.port_config.nb_ports);
	if (result != DOCA_SUCCESS) {
		exit_status = EXIT_FAILURE;
		goto nat_cleanup;
	}
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	DOCA_LOG_INFO("Waiting for traffic, press Ctrl+C for termination");
	while (!force_quit)
		sleep(1);

nat_cleanup:
	/* cleanup app resources */
	nat_destroy(dpdk_config.port_config.nb_ports, nat_rules);

dpdk_cleanup:
	/* cleanup resources */
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_destroy:
	dpdk_fini();

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
