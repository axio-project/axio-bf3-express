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

#include <stdlib.h>
#include <string.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_log.h>

#include "dma_copy_core.h"

DOCA_LOG_REGISTER(DMA_COPY);

/*
 * DMA copy application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	struct dma_copy_cfg dma_cfg = {0};
	struct doca_comm_channel_ep_t *ep;
	struct doca_comm_channel_addr_t *peer_addr = NULL;
	struct doca_dev *cc_dev = NULL;
	struct doca_dev_rep *cc_dev_rep = NULL;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_SUCCESS;

#ifdef DOCA_ARCH_DPU
	dma_cfg.mode = DMA_COPY_MODE_DPU;
#endif

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	result = doca_argp_init("doca_dma_copy", &dma_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	result = register_dma_copy_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register the program parameters: %s", doca_error_get_descr(result));
		exit_status = EXIT_FAILURE;
		goto destroy_argp;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		exit_status = EXIT_FAILURE;
		goto destroy_argp;
	}

	result = init_cc(&dma_cfg, &ep, &cc_dev, &cc_dev_rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to Initiate Comm Channel");
		exit_status = EXIT_FAILURE;
		goto destroy_argp;
	}

	if (dma_cfg.mode == DMA_COPY_MODE_HOST)
		result = host_start_dma_copy(&dma_cfg, ep, &peer_addr);
	else
		result = dpu_start_dma_copy(&dma_cfg, ep, &peer_addr);

	if (result != DOCA_SUCCESS)
		exit_status = EXIT_FAILURE;

	/* Destroy Comm Channel */
	result = destroy_cc(ep, peer_addr, cc_dev, cc_dev_rep);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA Comm Channel");
destroy_argp:
	/* ARGP destroy_resources */
	doca_argp_destroy();

	return exit_status;
}
