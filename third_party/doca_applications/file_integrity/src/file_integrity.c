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

#include <string.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <utils.h>

#include "file_integrity_core.h"

DOCA_LOG_REGISTER(FILE_INTEGRITY);

/*
 * File Integrity application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	struct file_integrity_config app_cfg = {
		.mode = NO_VALID_INPUT,
	};

	struct doca_comm_channel_ep_t *ep = NULL;
	struct doca_comm_channel_addr_t *peer_addr = NULL;
	struct doca_sha *sha_ctx = NULL;
	struct program_core_objects state = {0};
	doca_error_t result;

#ifdef DOCA_ARCH_HOST
	app_cfg.mode = CLIENT;
#else
	app_cfg.mode = SERVER;
#endif

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_file_integrity", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}

	result = register_file_integrity_params();
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

	result = file_integrity_init(&ep, &peer_addr, &app_cfg, &state, &sha_ctx);
	if (result != DOCA_SUCCESS) {
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* Start client/server logic */
	if (app_cfg.mode == CLIENT)
		result = file_integrity_client(ep, &peer_addr, &app_cfg, &state);
	else
		result = file_integrity_server(ep, &peer_addr, &app_cfg, &state, sha_ctx);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("File integrity encountered errors");
		file_integrity_cleanup(&state, sha_ctx, ep, &peer_addr);
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	file_integrity_cleanup(&state, sha_ctx, ep, &peer_addr);

	/* ARGP cleanup */
	doca_argp_destroy();

	return EXIT_SUCCESS;
}
