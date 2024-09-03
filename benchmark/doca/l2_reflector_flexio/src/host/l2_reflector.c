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

#include <assert.h>
#include <malloc.h>
#include <signal.h>
#include <unistd.h>

#include <libflexio/flexio.h>

#include <doca_argp.h>
#include <doca_log.h>

#include "utils.h"

#include "../common/l2_reflector_common.h"
#include "l2_reflector_core.h"

DOCA_LOG_REGISTER(L2_REFLECTOR);

static bool force_quit; /* Set to true to terminate the application */
extern flexio_func_t l2_reflector_device_init;

/*
 * Signals handler function to handle SIGINT and SIGTERM signals
 *
 * @signum [in]: signal number
 */
static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		/* Add additional new lines for output readability */
		DOCA_LOG_INFO("");
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		DOCA_LOG_INFO("");
		force_quit = true;
	}
}

/*
 * L2 reflector application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	int ret = 0;
	uint64_t rpc_ret_val;
	struct l2_reflector_config app_cfg;
	doca_error_t result;

	force_quit = false;
	memset(&app_cfg, 0, sizeof(app_cfg));

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("l2_reflector", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	result = register_l2_reflector_params();
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

	/* Open IB device and allocate UAR */
	result = l2_reflector_setup_ibv_device(&app_cfg);
	if (result != DOCA_SUCCESS) {
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Create FlexIO Process and allocate memory */
	result = l2_reflector_setup_device(&app_cfg);
	if (result != DOCA_SUCCESS)
		goto ibv_device_cleanup;

	/* Allocate device WQs, CQs and data */
	result = l2_reflector_allocate_device_resources(&app_cfg);
	if (result != DOCA_SUCCESS)
		goto device_cleanup;

	/* Run init function on device */
	ret = flexio_process_call(app_cfg.flexio_process, &l2_reflector_device_init, &rpc_ret_val,
				  app_cfg.dev_data_daddr);
	if (ret != FLEXIO_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Failed to call init function on device");
		goto device_resources_cleanup;
	}

	/* Steering rule */
	result = l2_reflector_create_steering_rule_rx(&app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create RX steering rule");
		goto device_resources_cleanup;
	}

	result = l2_reflector_create_steering_rule_tx(&app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create TX steering rule");
		goto rule_cleanup;
	}

	ret = flexio_event_handler_run(app_cfg.event_handler, 0);
	if (ret != FLEXIO_STATUS_SUCCESS) {
		DOCA_LOG_ERR("Failed to run event handler on device");
		goto rule_cleanup;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	DOCA_LOG_INFO("L2 reflector Started");
	/* Add an additional new line for output readability */
	DOCA_LOG_INFO("");
	DOCA_LOG_INFO("Press Ctrl+C to terminate");
	while (!force_quit)
		sleep(1);

	l2_reflector_destroy(&app_cfg);
	return EXIT_SUCCESS;

rule_cleanup:
	l2_reflector_steering_rules_destroy(&app_cfg);
device_resources_cleanup:
	l2_reflector_device_resources_destroy(&app_cfg);
device_cleanup:
	l2_reflector_device_destroy(&app_cfg);
ibv_device_cleanup:
	l2_reflector_ibv_device_destroy(&app_cfg);
	doca_argp_destroy();
	return EXIT_FAILURE;
}
