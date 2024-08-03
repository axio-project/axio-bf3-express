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
#include <time.h>

#include <doca_log.h>
#include <doca_argp.h>
#include <doca_comm_channel.h>
#include <doca_sync_event.h>

#include "common_common.h"

DOCA_LOG_REGISTER(SYNC_EVENT::COMMON);

#define SLEEP_IN_NANOS (10 * 1000)	     /* Sample the job every 10 microseconds */
#define TIMEOUT_IN_NANOS (1 * 1000000000) /* Poll the job for maximum of 1 second */

/*
 * common helper for copying PCI address user input
 *
 * @pci_addr_src [in]: input PCI address string
 * @pci_addr_dest [out]: destination PCI address string buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
pci_addr_cb(const char *pci_addr_src, char pci_addr_dest[DOCA_DEVINFO_PCI_ADDR_SIZE])
{
	int len = strnlen(pci_addr_src, DOCA_DEVINFO_PCI_ADDR_SIZE);

	if (len >= DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	strncpy(pci_addr_dest, pci_addr_src, len + 1);

	return DOCA_SUCCESS;
}

/*
 * argp callback - handle local device PCI address parameter
 *
 * @param [in]: input parameter
 * @config [in/out]: program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
dev_pci_addr_cb(void *param, void *config)
{
	return pci_addr_cb((char *)param, ((struct sync_event_config *)config)->dev_pci_addr);
}

/*
 * argp callback - handle DPU representor PCI address parameter
 *
 * @param [in]: input parameter
 * @config [in/out]: program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
rep_pci_addr_cb(void *param, void *config)
{
	return pci_addr_cb((char *)param, ((struct sync_event_config *)config)->rep_pci_addr);
}

/*
 * argp callback - handle sync event asynchronous mode parameter
 *
 * @param [in]: input parameter
 * @config [in/out]: program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
is_async_mode_cb(void *param, void *config)
{
	(void)(param);

	((struct sync_event_config *)config)->is_async_mode = true;

	return DOCA_SUCCESS;
}

/*
 * argp callback - handle sync event asynchronous workq depth parameter
 *
 * @param [in]: input parameter
 * @config [in/out]: program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
async_q_depth_cb(void *param, void *config)
{
	((struct sync_event_config *)config)->async_q_depth = *(int *)param;

	return DOCA_SUCCESS;
}

/*
 * argp callback - handle sync event atomic parameter
 *
 * @param [in]: input parameter
 * @config [in/out]: program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
is_update_atomic_cb(void *param, void *config)
{
	(void)(param);

	((struct sync_event_config *)config)->is_update_atomic = true;

	return DOCA_SUCCESS;
}

/*
 * Register command line parameters for DOCA Sync Event sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_params_register(void)
{
	doca_error_t result;
	struct doca_argp_param *dev_pci_addr_param = NULL,
			       *is_async_mode_param = NULL,
			       *async_q_depth = NULL,
			       *is_update_atomic = NULL;

	result = doca_argp_param_create(&dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create dev-pci-addr param: %s", doca_get_error_string(result));
		return result;
	}

	doca_argp_param_set_short_name(dev_pci_addr_param, "d");
	doca_argp_param_set_long_name(dev_pci_addr_param, "dev-pci-addr");
	doca_argp_param_set_description(dev_pci_addr_param, "Device PCI address");
	doca_argp_param_set_mandatory(dev_pci_addr_param);
	doca_argp_param_set_callback(dev_pci_addr_param, dev_pci_addr_cb);
	doca_argp_param_set_type(dev_pci_addr_param, DOCA_ARGP_TYPE_STRING);

	result = doca_argp_register_param(dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register dev-pci-addr param: %s", doca_get_error_string(result));
		return result;
	}

#ifdef DOCA_ARCH_DPU
	struct doca_argp_param *rep_pci_addr_param = NULL;

	result = doca_argp_param_create(&rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create rep-pci-addr param: %s", doca_get_error_string(result));
		return result;
	}

	doca_argp_param_set_short_name(rep_pci_addr_param, "r");
	doca_argp_param_set_long_name(rep_pci_addr_param, "rep-pci-addr");
	doca_argp_param_set_description(rep_pci_addr_param, "DPU representor PCI address");
	doca_argp_param_set_mandatory(rep_pci_addr_param);
	doca_argp_param_set_callback(rep_pci_addr_param, rep_pci_addr_cb);
	doca_argp_param_set_type(rep_pci_addr_param, DOCA_ARGP_TYPE_STRING);

	result = doca_argp_register_param(rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register rep-pci-addr param: %s", doca_get_error_string(result));
		return result;
	}
#endif

	result = doca_argp_param_create(&is_async_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create async param: %s", doca_get_error_string(result));
		return result;
	}

	doca_argp_param_set_long_name(is_async_mode_param, "async");
	doca_argp_param_set_description(is_async_mode_param, "Start DOCA Sync Event in asynchronous mode (synchronous mode by default)");
	doca_argp_param_set_callback(is_async_mode_param, is_async_mode_cb);
	doca_argp_param_set_type(is_async_mode_param, DOCA_ARGP_TYPE_BOOLEAN);

	result = doca_argp_register_param(is_async_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register async param: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_argp_param_create(&async_q_depth);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create qdepth param: %s", doca_get_error_string(result));
		return result;
	}

	doca_argp_param_set_long_name(async_q_depth, "qdepth");
	doca_argp_param_set_description(async_q_depth, "DOCA WorkQ depth (for asynchronous mode)");
	doca_argp_param_set_callback(async_q_depth, async_q_depth_cb);
	doca_argp_param_set_type(async_q_depth, DOCA_ARGP_TYPE_INT);

	result = doca_argp_register_param(async_q_depth);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register qdepth param: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_argp_param_create(&is_update_atomic);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create atomic param: %s", doca_get_error_string(result));
		return result;
	}

	doca_argp_param_set_long_name(is_update_atomic, "atomic");
	doca_argp_param_set_description(is_update_atomic, "Update DOCA Sync Event using Add operation (Set operation by default)");
	doca_argp_param_set_callback(is_update_atomic, is_update_atomic_cb);
	doca_argp_param_set_type(is_update_atomic, DOCA_ARGP_TYPE_BOOLEAN);

	result = doca_argp_register_param(is_update_atomic);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register atomic param: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Validate configured flow by user input
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t sync_event_config_validate(const struct sync_event_config *se_cfg, const struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;

	if (!se_cfg->is_async_mode)
		return DOCA_SUCCESS;

	result = doca_sync_event_job_get_supported(doca_dev_as_devinfo(se_rt_objs->dev), DOCA_SYNC_EVENT_JOB_WAIT_GT);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DOCA Sync Event asynchronous wait is not supported (%s) on the given device", doca_get_error_string(result));
		return result;
	}

	if ((int)(se_cfg->async_q_depth) <= 0) {
		DOCA_LOG_ERR("Please specify DOCA WorkQ grater than 0 (asynchronous mode)");
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

/*
 * Start Sample's DOCA Sync Event in asynchronous operation mode
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_start_async(const struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;

	se_rt_objs->se_ctx = doca_sync_event_as_ctx(se_rt_objs->se);
	if (se_rt_objs->se_ctx == NULL) {
		DOCA_LOG_ERR("Failed to convert sync event to ctx: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_ctx_start(se_rt_objs->se_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start sync event ctx: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_workq_create(se_cfg->async_q_depth, &se_rt_objs->se_workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca workq: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_ctx_workq_add(se_rt_objs->se_ctx, se_rt_objs->se_workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add workq to sync event ctx: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Initialize Sample's DOCA comm_channel
 *
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_cc_init(struct sync_event_runtime_objects *se_rt_objs)
{
	doca_error_t result = DOCA_SUCCESS;

	result = doca_comm_channel_ep_create(&se_rt_objs->ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create cc client endpoint: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_comm_channel_ep_set_device(se_rt_objs->ep, se_rt_objs->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set cc device: %s", doca_get_error_string(result));
		return result;
	}

#ifdef DOCA_ARCH_DPU
	result = doca_comm_channel_ep_set_device_rep(se_rt_objs->ep, se_rt_objs->rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set cc rep: %s", doca_get_error_string(result));
		return result;
	}
#endif

	result = doca_comm_channel_ep_set_max_msg_size(se_rt_objs->ep, SYNC_EVENT_CC_MAX_MSG_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set cc max msg size: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_comm_channel_ep_set_send_queue_size(se_rt_objs->ep, SYNC_EVENT_CC_MAX_QUEUE_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set cc send queue size: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_comm_channel_ep_set_recv_queue_size(se_rt_objs->ep, SYNC_EVENT_CC_MAX_QUEUE_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set cc recv queue size: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Submit asynchronous DOCA Jobs on Sample's DOCA Sync Event (DOCA) Context
 *
 * @se_rt_objs [in]: sample's runtime resources
 * @job [in]: DOCA Job to submit
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_async_job_submit(struct sync_event_runtime_objects *se_rt_objs, struct doca_job *job)
{
	doca_error_t result = DOCA_SUCCESS;
	struct doca_event ev = {0};
	int timeout = TIMEOUT_IN_NANOS;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	result = doca_workq_submit(se_rt_objs->se_workq, job);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit set job for sync event: %s", doca_get_error_string(result));
		return result;
	}

	while ((result = doca_workq_progress_retrieve(se_rt_objs->se_workq, &ev, 0)) == DOCA_ERROR_AGAIN) {
		if (timeout == 0) {
			DOCA_LOG_ERR("Failed to retrieve set job progress: timeout");
			return DOCA_ERROR_TIME_OUT;
		}

		nanosleep(&ts, &ts);
		timeout -= SLEEP_IN_NANOS;
	}

	if (result == DOCA_ERROR_IO_FAILED) {
		DOCA_LOG_ERR("Failed to execute set job for sync event: %s", doca_get_error_string(ev.result.u64));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Sample's tear down flow
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
void
sync_event_tear_down(struct sync_event_runtime_objects *se_rt_objs)
{
	if (se_rt_objs->peer_addr != NULL)
		doca_comm_channel_ep_disconnect(se_rt_objs->ep, se_rt_objs->peer_addr);

	if (se_rt_objs->ep != NULL)
		doca_comm_channel_ep_destroy(se_rt_objs->ep);

	if (se_rt_objs->se_workq != NULL) {
		doca_ctx_workq_rm(se_rt_objs->se_ctx, se_rt_objs->se_workq);
		doca_workq_destroy(se_rt_objs->se_workq);
	}

	if (se_rt_objs->se_ctx != NULL)
		doca_ctx_stop(se_rt_objs->se_ctx);

	if (se_rt_objs->se != NULL) {
		if (se_rt_objs->se_ctx == NULL)
			doca_sync_event_stop(se_rt_objs->se);
		doca_sync_event_destroy(se_rt_objs->se);
	}

	if (se_rt_objs->rep != NULL)
		doca_dev_rep_close(se_rt_objs->rep);

	if (se_rt_objs->dev != NULL)
		doca_dev_close(se_rt_objs->dev);
}
