/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <unistd.h>

#include <doca_log.h>
#include <doca_comm_channel.h>
#include <doca_sync_event.h>

#include <common.h>

#include "common_common.h"

DOCA_LOG_REGISTER(SYNC_EVENT::SAMPLE);

/*
 * DOCA device with create-doca-sync-event-from-export capability filter callback
 *
 * @devinfo [in]: doca_devinfo
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
sync_event_get_create_from_export_supported(struct doca_devinfo *devinfo)
{
	return doca_sync_event_cap_is_create_from_export_supported(devinfo);
}

/*
 * Initialize sample's DOCA comm_channel
 *
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
cc_init(struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;

	result = sync_event_cc_init(se_rt_objs);
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_comm_channel_ep_listen(se_rt_objs->ep, SYNC_EVENT_CC_SERVICE_NAME);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to listen to host: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Initialize sample's DOCA Sync Event
 *
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
se_init(struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;
	char se_blob[SYNC_EVENT_CC_MAX_MSG_SIZE];
	size_t se_blob_sz = SYNC_EVENT_CC_MAX_MSG_SIZE;
	int timeout = SYNC_EVENT_CC_TIMEOUT_SEC;

	DOCA_LOG_INFO("Listening to host");
	while ((result = doca_comm_channel_ep_recvfrom(se_rt_objs->ep, se_blob, &se_blob_sz, DOCA_CC_MSG_FLAG_NONE,
	      &se_rt_objs->peer_addr)) == DOCA_ERROR_AGAIN) {
		if (timeout == 0) {
			DOCA_LOG_ERR("Failed to retrieve set task progress: timeout");
			return DOCA_ERROR_TIME_OUT;
		}
		sleep(1);
		timeout--;
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to establish connection with DPU: %s", doca_error_get_descr(result));
		return result;
	}
	DOCA_LOG_INFO("Received blob from host");

	result = doca_sync_event_create_from_export(se_rt_objs->dev, (const uint8_t *)se_blob, se_blob_sz, &se_rt_objs->se);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA Sync Event from export: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Communicate with DPU through DOCA Sync Event in synchronous mode
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
se_communicate_sync(const struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;
	uint64_t fetched = 0;

	DOCA_LOG_INFO("Waiting for sync event to be signaled from host");
	result = doca_sync_event_wait_gt(se_rt_objs->se, 0, UINT64_MAX);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to wait for sync event: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("Signaling sync event for host");
	if (se_cfg->is_update_atomic)
		result = doca_sync_event_update_add(se_rt_objs->se, 1, &fetched);
	else
		result = doca_sync_event_update_set(se_rt_objs->se, 2);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to signal sync event: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("Done");

	return DOCA_SUCCESS;
}

/*
 * Communicate with DPU through DOCA Sync Event in asynchronous mode
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
se_communicate_async(const struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;
	struct doca_sync_event_task_wait_gt *wait_gt_task;
	struct doca_sync_event_task_notify_add *notify_add_task;
	struct doca_sync_event_task_notify_set *notify_set_task;
	uint64_t fetched = 0;
	union doca_data user_data;

	user_data.u64 = 0;

	result = doca_sync_event_task_wait_gt_alloc_init(se_rt_objs->se, 0, UINT64_MAX, user_data, &wait_gt_task);
	if (result != DOCA_SUCCESS)
		return result;

	DOCA_LOG_INFO("Waiting for sync event to be signaled from host");
	result = sync_event_async_task_submit(se_rt_objs, doca_sync_event_task_wait_gt_as_doca_task(wait_gt_task));
	if (result != DOCA_SUCCESS)
		return result;

	DOCA_LOG_INFO("Signaling sync event for host");
	if (se_cfg->is_update_atomic) {
		result = doca_sync_event_task_notify_add_alloc_init(se_rt_objs->se, 1, &fetched, user_data,
								    &notify_add_task);
		if (result != DOCA_SUCCESS)
			return result;

		result = sync_event_async_task_submit(se_rt_objs,
						      doca_sync_event_task_notify_add_as_doca_task(notify_add_task));

		doca_task_free(doca_sync_event_task_notify_add_as_doca_task(notify_add_task));

		if (result != DOCA_SUCCESS)
			return result;
	} else {
		result = doca_sync_event_task_notify_set_alloc_init(se_rt_objs->se, 2, user_data, &notify_set_task);
		if (result != DOCA_SUCCESS)
			return result;

		result = sync_event_async_task_submit(se_rt_objs,
						      doca_sync_event_task_notify_set_as_doca_task(notify_set_task));

		doca_task_free(doca_sync_event_task_notify_set_as_doca_task(notify_set_task));

		if (result != DOCA_SUCCESS)
			return result;
	}

	DOCA_LOG_INFO("Done");

	doca_task_free(doca_sync_event_task_wait_gt_as_doca_task(wait_gt_task));

	return DOCA_SUCCESS;
}

/*
 * Sample's logic
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_run(const struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;

	result = open_doca_device_with_pci(se_cfg->dev_pci_addr, sync_event_get_create_from_export_supported, &se_rt_objs->dev);
	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	result = open_doca_device_rep_with_pci(se_rt_objs->dev, DOCA_DEVINFO_REP_FILTER_NET, se_cfg->rep_pci_addr, &se_rt_objs->rep);
	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	result = sync_event_config_validate(se_cfg, se_rt_objs);
	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	result = cc_init(se_rt_objs);
	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	result = se_init(se_rt_objs);
	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	if (se_cfg->is_async_mode)
		result = sync_event_start_async(se_cfg, se_rt_objs);
	else
		result = doca_sync_event_start(se_rt_objs->se);

	if (result != DOCA_SUCCESS) {
		sync_event_tear_down(se_rt_objs);
		return result;
	}

	if (se_cfg->is_async_mode)
		result = se_communicate_async(se_cfg, se_rt_objs);
	else
		result = se_communicate_sync(se_cfg, se_rt_objs);

	sync_event_tear_down(se_rt_objs);

	return result;
}
