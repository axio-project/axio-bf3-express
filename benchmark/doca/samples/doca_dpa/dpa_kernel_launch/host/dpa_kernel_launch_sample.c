/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <unistd.h>

#include <infiniband/mlx5dv.h>

#include <doca_error.h>
#include <doca_log.h>

#include "dpa_common.h"

DOCA_LOG_REGISTER(KERNEL_LAUNCH::SAMPLE);

/* Kernel function decleration */
extern doca_dpa_func_t hello_world;

/*
 * Run kernel_launch sample
 *
 * @resources [in]: DOCA DPA resources that the DPA sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
kernel_launch(struct dpa_resources *resources)
{
	struct doca_sync_event *wait_event = NULL;
	struct doca_sync_event *comp_event = NULL;
	/* Wait event threshold */
	uint64_t wait_thresh = 0;
	/* Completion event val */
	uint64_t comp_event_val = 10;
	/* Number of DPA threads */
	const unsigned int num_dpa_threads = 1;
	doca_error_t result, tmp_result;

	/* Creating DOCA sync event for DPA kernel completion */
	result = create_doca_dpa_completion_sync_event(resources->doca_dpa, resources->doca_device, &comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event for DPA kernel completion: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("All DPA resources have been created");

	/* kernel launch */
	result = doca_dpa_kernel_launch_update_set(resources->doca_dpa, wait_event, wait_thresh,
						comp_event, comp_event_val, num_dpa_threads, &hello_world);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to launch hello_world kernel: %s", doca_error_get_descr(result));
		goto destroy_event;
	}

	/* Wait until completion event reach completion val */
	result = doca_sync_event_wait_gt(comp_event, comp_event_val - 1, SYNC_EVENT_MASK_FFS);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to wait for host completion event: %s", doca_error_get_descr(result));

destroy_event:
	/* destroy events */
	tmp_result = doca_sync_event_destroy(comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}
