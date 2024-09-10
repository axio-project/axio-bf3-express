/*
 * Copyright (c) 2022-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include "dpa_common.h"

DOCA_LOG_REGISTER(DPA_COMMON);

/*
 * A struct that includes all needed info on registered kernels and is initialized during linkage by DPACC.
 * Variable name should be the token passed to DPACC with --app-name parameter.
 */
extern struct doca_dpa_app *dpa_sample_app;

/*
 * ARGP Callback - Handle device name parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t device_name_param_callback(void *param, void *config)
{
	struct dpa_config *dpa_cgf = (struct dpa_config *)config;
	char *device_name = (char *)param;
	int len;

	len = strnlen(device_name, DOCA_DEVINFO_IBDEV_NAME_SIZE);
	if (len == DOCA_DEVINFO_IBDEV_NAME_SIZE) {
		DOCA_LOG_ERR("Entered device name exceeding the maximum size of %d", DOCA_DEVINFO_IBDEV_NAME_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strncpy(dpa_cgf->device_name, device_name, len + 1);

	return DOCA_SUCCESS;
}

doca_error_t register_dpa_params(void)
{
	doca_error_t result;
	struct doca_argp_param *device_param;

	result = doca_argp_param_create(&device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(device_param, "d");
	doca_argp_param_set_long_name(device_param, "device");
	doca_argp_param_set_arguments(device_param, "<device name>");
	doca_argp_param_set_description(
		device_param,
		"device name that supports DPA (optional). If not provided then a random device will be chosen");
	doca_argp_param_set_callback(device_param, device_name_param_callback);
	doca_argp_param_set_type(device_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Open DPA DOCA device
 *
 * @device_name [in]: Wanted device name, can be NOT_SET and then a random DPA supported device is chosen
 * @doca_device [out]: An allocated DOCA DPA device on success and NULL otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t open_dpa_device(const char *device_name, struct doca_dev **doca_device)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs = 0;
	doca_error_t result;
	char ibdev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE] = {0};
	uint32_t i = 0;

	result = doca_devinfo_create_list(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load DOCA devices list: %s", doca_error_get_descr(result));
		return result;
	}

	/* Search device with same dev name*/
	for (i = 0; i < nb_devs; i++) {
		result = doca_dpa_cap_is_supported(dev_list[i]);
		if (result != DOCA_SUCCESS)
			continue;
		result = doca_devinfo_get_ibdev_name(dev_list[i], ibdev_name, sizeof(ibdev_name));
		if (result != DOCA_SUCCESS ||
		    (strcmp(device_name, DEVICE_DEFAULT_NAME) != 0 && strcmp(device_name, ibdev_name) != 0))
			continue;
		result = doca_dev_open(dev_list[i], doca_device);
		if (result != DOCA_SUCCESS) {
			doca_devinfo_destroy_list(dev_list);
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			return result;
		}
		break;
	}

	doca_devinfo_destroy_list(dev_list);

	if (*doca_device == NULL) {
		DOCA_LOG_ERR("Couldn't get DOCA device");
		return DOCA_ERROR_NOT_FOUND;
	}

	return result;
}

doca_error_t create_doca_dpa_wait_sync_event(struct doca_dpa *doca_dpa,
					     struct doca_dev *doca_device,
					     struct doca_sync_event **wait_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(wait_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_add_publisher_location_cpu(*wait_event, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU as publisher for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_wait_event;
	}

	result = doca_sync_event_add_subscriber_location_dpa(*wait_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as subscriber for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_wait_event;
	}

	result = doca_sync_event_start(*wait_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_wait_event;
	}

	return result;

destroy_wait_event:
	tmp_result = doca_sync_event_destroy(*wait_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

doca_error_t create_doca_dpa_completion_sync_event(struct doca_dpa *doca_dpa,
						   struct doca_dev *doca_device,
						   struct doca_sync_event **comp_event,
						   doca_dpa_dev_sync_event_t *handle)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_add_publisher_location_dpa(*comp_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as publisher for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_comp_event;
	}

	result = doca_sync_event_add_subscriber_location_cpu(*comp_event, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU as subscriber for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_comp_event;
	}

	result = doca_sync_event_start(*comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_comp_event;
	}

	if (handle != NULL) {
		result = doca_sync_event_get_dpa_handle(*comp_event, doca_dpa, handle);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_sync_event_get_dpa_handle failed (%d)", result);
			goto destroy_comp_event;
		}
	}

	return result;

destroy_comp_event:
	tmp_result = doca_sync_event_destroy(*comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

doca_error_t create_doca_dpa_kernel_sync_event(struct doca_dpa *doca_dpa, struct doca_sync_event **kernel_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(kernel_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_add_publisher_location_dpa(*kernel_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as publisher for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_kernel_event;
	}

	result = doca_sync_event_add_subscriber_location_dpa(*kernel_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as subscriber for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_kernel_event;
	}

	result = doca_sync_event_start(*kernel_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_kernel_event;
	}

	return result;

destroy_kernel_event:
	tmp_result = doca_sync_event_destroy(*kernel_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

doca_error_t create_doca_remote_net_sync_event(struct doca_dev *doca_device, struct doca_sync_event **remote_net_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(remote_net_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_add_publisher_location_remote_net(*remote_net_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set remote net as publisher for DOCA sync event: %s",
			     doca_error_get_descr(result));
		goto destroy_remote_net_event;
	}

	result = doca_sync_event_add_subscriber_location_cpu(*remote_net_event, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU as subscriber for DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_remote_net_event;
	}

	result = doca_sync_event_start(*remote_net_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_error_get_descr(result));
		goto destroy_remote_net_event;
	}

	return result;

destroy_remote_net_event:
	tmp_result = doca_sync_event_destroy(*remote_net_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

doca_error_t export_doca_remote_net_sync_event_to_dpa(struct doca_dev *doca_device,
						      struct doca_dpa *doca_dpa,
						      struct doca_sync_event *remote_net_event,
						      struct doca_sync_event_remote_net **remote_net_exported_event,
						      doca_dpa_dev_sync_event_remote_net_t *remote_net_event_dpa_handle)
{
	doca_error_t result, tmp_result;
	const uint8_t *remote_net_event_export_data;
	size_t remote_net_event_export_size;

	result = doca_sync_event_export_to_remote_net(remote_net_event,
						      &remote_net_event_export_data,
						      &remote_net_event_export_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export DOCA sync event to remote net: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_remote_net_create_from_export(doca_device,
							       remote_net_event_export_data,
							       remote_net_event_export_size,
							       remote_net_exported_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create remote net DOCA sync event: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_sync_event_remote_net_get_dpa_handle(*remote_net_exported_event,
							   doca_dpa,
							   remote_net_event_dpa_handle);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export remote net DOCA sync event to DPA: %s", doca_error_get_descr(result));
		goto destroy_export_remote_net_event;
	}

	return result;

destroy_export_remote_net_event:
	tmp_result = doca_sync_event_remote_net_destroy(*remote_net_exported_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy remote net DOCA sync event: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}

doca_error_t allocate_dpa_resources(struct dpa_config *cfg, struct dpa_resources *resources)
{
	doca_error_t result;

	/* open doca device */
	result = open_dpa_device(cfg->device_name, &resources->doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function open_dpa_device() failed");
		goto exit_label;
	}

	/* create doca_dpa context */
	result = doca_dpa_create(resources->doca_device, &(resources->doca_dpa));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA context: %s", doca_error_get_descr(result));
		goto close_doca_dev;
	}

	/* set doca_dpa app */
	result = doca_dpa_set_app(resources->doca_dpa, dpa_sample_app);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DOCA DPA app: %s", doca_error_get_descr(result));
		goto destroy_doca_dpa;
	}

	/* start doca_dpa context */
	result = doca_dpa_start(resources->doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA DPA context: %s", doca_error_get_descr(result));
		goto destroy_doca_dpa;
	}

	return result;

destroy_doca_dpa:
	doca_dpa_destroy(resources->doca_dpa);
close_doca_dev:
	doca_dev_close(resources->doca_device);
exit_label:
	return result;
}

doca_error_t destroy_dpa_resources(struct dpa_resources *resources)
{
	doca_error_t result = DOCA_SUCCESS;
	doca_error_t tmp_result;

	/* destroy doca_dpa context */
	tmp_result = doca_dpa_destroy(resources->doca_dpa);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_destroy() failed: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* close doca device */
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

doca_error_t dpa_thread_obj_init(struct dpa_thread_obj *dpa_thread_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS;

	doca_err = doca_dpa_thread_create(dpa_thread_obj->doca_dpa, &(dpa_thread_obj->thread));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_create failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	doca_err = doca_dpa_thread_set_func_arg(dpa_thread_obj->thread, dpa_thread_obj->func, dpa_thread_obj->arg);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_set_func_arg failed (%s)", doca_error_get_descr(doca_err));
		dpa_thread_obj_destroy(dpa_thread_obj);
		return doca_err;
	}

	if (dpa_thread_obj->tls_dev_ptr) {
		doca_err = doca_dpa_thread_set_local_storage(dpa_thread_obj->thread, dpa_thread_obj->tls_dev_ptr);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_dpa_thread_set_local_storage failed (%s)",
				     doca_error_get_descr(doca_err));
			dpa_thread_obj_destroy(dpa_thread_obj);
			return doca_err;
		}
	}

	doca_err = doca_dpa_thread_start(dpa_thread_obj->thread);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_start failed (%s)", doca_error_get_descr(doca_err));
		dpa_thread_obj_destroy(dpa_thread_obj);
		return doca_err;
	}

	return doca_err;
}

doca_error_t dpa_thread_obj_destroy(struct dpa_thread_obj *dpa_thread_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_dpa_thread_destroy(dpa_thread_obj->thread);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_destroy failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}

doca_error_t dpa_completion_obj_init(struct dpa_completion_obj *dpa_completion_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS;

	doca_err = doca_dpa_completion_create(dpa_completion_obj->doca_dpa,
					      dpa_completion_obj->queue_size,
					      &(dpa_completion_obj->dpa_comp));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_completion_create failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	doca_err = doca_dpa_completion_set_thread(dpa_completion_obj->dpa_comp, dpa_completion_obj->thread);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_completion_set_thread failed (%s)", doca_error_get_descr(doca_err));
		dpa_completion_obj_destroy(dpa_completion_obj);
		return doca_err;
	}

	doca_err = doca_dpa_completion_start(dpa_completion_obj->dpa_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_completion_start failed (%s)", doca_error_get_descr(doca_err));
		dpa_completion_obj_destroy(dpa_completion_obj);
		return doca_err;
	}

	doca_err = doca_dpa_completion_get_dpa_handle(dpa_completion_obj->dpa_comp, &(dpa_completion_obj->handle));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_completion_get_dpa_handle failed (%s)", doca_error_get_descr(doca_err));
		dpa_completion_obj_destroy(dpa_completion_obj);
		return doca_err;
	}

	return DOCA_SUCCESS;
}

doca_error_t dpa_completion_obj_destroy(struct dpa_completion_obj *dpa_completion_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_dpa_completion_destroy(dpa_completion_obj->dpa_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_completion_destroy failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}

doca_error_t dpa_notification_completion_obj_init(struct dpa_notification_completion_obj *notification_completion_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS;

	doca_err = doca_dpa_notification_completion_create(notification_completion_obj->doca_dpa,
							   notification_completion_obj->thread,
							   &(notification_completion_obj->notification_comp));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_notification_completion_create failed (%s)",
			     doca_error_get_descr(doca_err));
		return doca_err;
	}

	doca_err = doca_dpa_notification_completion_start(notification_completion_obj->notification_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_notification_completion_start failed (%s)",
			     doca_error_get_descr(doca_err));
		dpa_notification_completion_obj_destroy(notification_completion_obj);
		return doca_err;
	}

	doca_err = doca_dpa_notification_completion_get_dpa_handle(notification_completion_obj->notification_comp,
								   &(notification_completion_obj->handle));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_notification_completion_get_dpa_handle failed (%s)",
			     doca_error_get_descr(doca_err));
		dpa_notification_completion_obj_destroy(notification_completion_obj);
		return doca_err;
	}

	return DOCA_SUCCESS;
}

doca_error_t dpa_notification_completion_obj_destroy(struct dpa_notification_completion_obj *notification_completion_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_dpa_notification_completion_destroy(notification_completion_obj->notification_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_notification_completion_destroy failed (%s)",
			     doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}

doca_error_t dpa_rdma_srq_obj_init(struct dpa_rdma_srq_obj *dpa_rdma_srq_obj)
{
	doca_error_t doca_err = doca_rdma_srq_create(dpa_rdma_srq_obj->doca_device, &(dpa_rdma_srq_obj->rdma_srq));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_create failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	dpa_rdma_srq_obj->rdma_srq_as_ctx = doca_rdma_srq_as_ctx(dpa_rdma_srq_obj->rdma_srq);

	doca_err = doca_rdma_srq_set_shared_recv_queue_size(dpa_rdma_srq_obj->rdma_srq, dpa_rdma_srq_obj->srq_size);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_set_shared_recv_queue_size failed (%s)",
			     doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	doca_err = doca_rdma_srq_task_receive_set_dst_buf_list_len(dpa_rdma_srq_obj->rdma_srq,
								   dpa_rdma_srq_obj->buf_list_len);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_task_receive_set_dst_buf_list_len failed (%s)",
			     doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	doca_err = doca_rdma_srq_set_type(dpa_rdma_srq_obj->rdma_srq, dpa_rdma_srq_obj->srq_type);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_set_type failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	doca_err = doca_ctx_set_datapath_on_dpa(dpa_rdma_srq_obj->rdma_srq_as_ctx, dpa_rdma_srq_obj->doca_dpa);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_set_datapath_on_dpa failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	doca_err = doca_ctx_start(dpa_rdma_srq_obj->rdma_srq_as_ctx);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_start failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	doca_err = doca_rdma_srq_get_dpa_handle(dpa_rdma_srq_obj->rdma_srq, &(dpa_rdma_srq_obj->dpa_rdma_srq));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_get_dpa_handle failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_srq_obj_destroy(dpa_rdma_srq_obj);
		return doca_err;
	}

	return doca_err;
}

doca_error_t dpa_rdma_srq_obj_destroy(struct dpa_rdma_srq_obj *dpa_rdma_srq_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_ctx_stop(dpa_rdma_srq_obj->rdma_srq_as_ctx);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_stop failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	doca_err = doca_rdma_srq_destroy(dpa_rdma_srq_obj->rdma_srq);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_srq_destroy failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}

doca_error_t dpa_rdma_obj_init(struct dpa_rdma_srq_obj *dpa_rdma_srq_obj, struct dpa_rdma_obj *dpa_rdma_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS;

	if (dpa_rdma_srq_obj != NULL)
		doca_err = doca_rdma_create_with_srq(dpa_rdma_obj->doca_device,
						     dpa_rdma_srq_obj->rdma_srq,
						     &(dpa_rdma_obj->rdma));
	else
		doca_err = doca_rdma_create(dpa_rdma_obj->doca_device, &(dpa_rdma_obj->rdma));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RDMA create failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	dpa_rdma_obj->rdma_as_ctx = doca_rdma_as_ctx(dpa_rdma_obj->rdma);

	doca_err = doca_rdma_set_permissions(dpa_rdma_obj->rdma, dpa_rdma_obj->permissions);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_set_permissions failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	doca_err = doca_rdma_set_grh_enabled(dpa_rdma_obj->rdma, true);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_set_grh_enabled failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	doca_err = doca_ctx_set_datapath_on_dpa(dpa_rdma_obj->rdma_as_ctx, dpa_rdma_obj->doca_dpa);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_set_datapath_on_dpa failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	doca_err = doca_ctx_set_user_data(dpa_rdma_obj->rdma_as_ctx, dpa_rdma_obj->user_data);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_set_user_data failed (%s)", doca_error_get_descr(doca_err));
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	return doca_err;
}

doca_error_t dpa_rdma_obj_start(struct dpa_rdma_obj *dpa_rdma_obj)
{
	/* start ctx */
	doca_error_t doca_err = doca_ctx_start(dpa_rdma_obj->rdma_as_ctx);
	if (doca_err != DOCA_SUCCESS) {
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	/* get dpa rdma handle */
	doca_err = doca_rdma_get_dpa_handle(dpa_rdma_obj->rdma, &(dpa_rdma_obj->dpa_rdma));
	if (doca_err != DOCA_SUCCESS) {
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	/* export connection details */
	doca_err = doca_rdma_export(dpa_rdma_obj->rdma,
				    &(dpa_rdma_obj->connection_details),
				    &(dpa_rdma_obj->conn_det_len));
	if (doca_err != DOCA_SUCCESS) {
		dpa_rdma_obj_destroy(dpa_rdma_obj);
		return doca_err;
	}

	return doca_err;
}

doca_error_t dpa_rdma_obj_destroy(struct dpa_rdma_obj *dpa_rdma_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_ctx_stop(dpa_rdma_obj->rdma_as_ctx);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_ctx_stop failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	doca_err = doca_rdma_destroy(dpa_rdma_obj->rdma);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_destroy failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}

doca_error_t doca_mmap_obj_init(struct doca_mmap_obj *doca_mmap_obj)
{
	doca_error_t doca_err = doca_mmap_create(&(doca_mmap_obj->mmap));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_create failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	doca_err = doca_mmap_set_permissions(doca_mmap_obj->mmap, doca_mmap_obj->permissions);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_set_permissions failed (%s)", doca_error_get_descr(doca_err));
		doca_mmap_obj_destroy(doca_mmap_obj);
		return doca_err;
	}

	switch (doca_mmap_obj->mmap_type) {
	case MMAP_TYPE_CPU:
		doca_err = doca_mmap_set_memrange(doca_mmap_obj->mmap,
						  doca_mmap_obj->memrange_addr,
						  doca_mmap_obj->memrange_len);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_mmap_set_memrange failed (%s)", doca_error_get_descr(doca_err));
			doca_mmap_obj_destroy(doca_mmap_obj);
			return doca_err;
		}

		doca_err = doca_mmap_add_dev(doca_mmap_obj->mmap, doca_mmap_obj->doca_device);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_mmap_add_dev failed (%s)", doca_error_get_descr(doca_err));
			doca_mmap_obj_destroy(doca_mmap_obj);
			return doca_err;
		}

		break;

	case MMAP_TYPE_DPA:
		doca_err = doca_mmap_set_dpa_memrange(doca_mmap_obj->mmap,
						      doca_mmap_obj->doca_dpa,
						      (uint64_t)doca_mmap_obj->memrange_addr,
						      doca_mmap_obj->memrange_len);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_mmap_set_dpa_memrange failed (%s)", doca_error_get_descr(doca_err));
			doca_mmap_obj_destroy(doca_mmap_obj);
			return doca_err;
		}

		break;

	default:
		DOCA_LOG_ERR("Unsupported mmap_type (%d)", doca_mmap_obj->mmap_type);
		doca_mmap_obj_destroy(doca_mmap_obj);
		return DOCA_ERROR_NOT_SUPPORTED;
	}

	doca_err = doca_mmap_start(doca_mmap_obj->mmap);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_start failed (%s)", doca_error_get_descr(doca_err));
		doca_mmap_obj_destroy(doca_mmap_obj);
		return doca_err;
	}

	doca_err = doca_mmap_dev_get_dpa_handle(doca_mmap_obj->mmap,
						doca_mmap_obj->doca_device,
						&(doca_mmap_obj->dpa_mmap_handle));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_dev_get_dpa_handle failed (%s)", doca_error_get_descr(doca_err));
		doca_mmap_obj_destroy(doca_mmap_obj);
		return doca_err;
	}

	doca_err = doca_mmap_export_rdma(doca_mmap_obj->mmap,
					 doca_mmap_obj->doca_device,
					 &(doca_mmap_obj->rdma_export),
					 &(doca_mmap_obj->export_len));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_export_rdma failed (%s)", doca_error_get_descr(doca_err));
		doca_mmap_obj_destroy(doca_mmap_obj);
		return doca_err;
	}

	return doca_err;
}

doca_error_t doca_mmap_obj_destroy(struct doca_mmap_obj *doca_mmap_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, ret_err = DOCA_SUCCESS;

	doca_err = doca_mmap_destroy(doca_mmap_obj->mmap);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_destroy failed (%s)", doca_error_get_descr(doca_err));
		DOCA_ERROR_PROPAGATE(ret_err, doca_err);
	}

	return ret_err;
}
