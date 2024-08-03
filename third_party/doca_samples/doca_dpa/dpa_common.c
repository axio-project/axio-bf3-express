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

#include <doca_log.h>
#include <doca_error.h>
#include <doca_dev.h>
#include <doca_argp.h>

#include "dpa_common.h"

DOCA_LOG_REGISTER(DPA_COMMON);

/*
 * A struct that includes all needed info on registered kernels and is initialized during linkage by DPACC.
 * Variable name should be the token passed to DPACC with --app-name parameter.
 */
extern struct doca_dpa_app *dpa_sample_app;

/*
 * ARGP Callback - Handle IB device name parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
device_address_callback(void *param, void *config)
{
	struct dpa_config *dpa_cgf = (struct dpa_config *)config;
	char *device_name = (char *)param;
	int len;

	len = strnlen(device_name, MAX_IB_DEVICE_NAME);
	if (len == MAX_IB_DEVICE_NAME) {
		DOCA_LOG_ERR("Entered IB device name exceeding the maximum size of %d", MAX_USER_IB_DEVICE_NAME);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strcpy(dpa_cgf->device_name, device_name);

	return DOCA_SUCCESS;
}

doca_error_t
register_dpa_params(void)
{
	doca_error_t result;
	struct doca_argp_param *device_param;

	result = doca_argp_param_create(&device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(device_param, "d");
	doca_argp_param_set_long_name(device_param, "device");
	doca_argp_param_set_arguments(device_param, "<IB device name>");
	doca_argp_param_set_description(device_param, "IB device name that supports DPA (optional). If not provided then a random IB device will be chosen");
	doca_argp_param_set_callback(device_param, device_address_callback);
	doca_argp_param_set_type(device_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Open DPA DOCA device
 *
 * @device_name [in]: Wanted IB device name, can be NOT_SET and then a random device IB DPA supported device is chosen
 * @doca_device [out]: An allocated DOCA DPA device on success and NULL otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
open_dpa_device(char *device_name, struct doca_dev **doca_device)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs = 0;
	doca_error_t result;
	char ibdev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE] = {0};
	uint32_t i = 0;

	result = doca_devinfo_list_create(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load DOCA devices list: %s", doca_get_error_string(result));
		return result;
	}

	/* Search device with same dev name*/
	for (i = 0; i < nb_devs; i++) {
		result = doca_devinfo_get_is_dpa_supported(dev_list[i]);
		if (result != DOCA_SUCCESS)
			continue;
		result = doca_devinfo_get_ibdev_name(dev_list[i], ibdev_name, sizeof(ibdev_name));
		if (result != DOCA_SUCCESS ||
			(strcmp(device_name, IB_DEVICE_DEFAULT_NAME) != 0 && strcmp(device_name, ibdev_name) != 0))
			continue;
		result = doca_dev_open(dev_list[i], doca_device);
		if (result != DOCA_SUCCESS) {
			doca_devinfo_list_destroy(dev_list);
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_get_error_string(result));
			return result;
		}
		break;
	}

	doca_devinfo_list_destroy(dev_list);

	if (*doca_device == NULL) {
		DOCA_LOG_ERR("Couldn't get DOCA device");
		return DOCA_ERROR_NOT_FOUND;
	}

	return result;
}

doca_error_t
create_doca_dpa_wait_sync_event(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, struct doca_sync_event **wait_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(wait_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_sync_event_publisher_add_location_cpu(*wait_event, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU as publisher for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_wait_event;
	}

	result = doca_sync_event_subscriber_add_location_dpa(*wait_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as subscriber for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_wait_event;
	}

	result = doca_sync_event_start(*wait_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_wait_event;
	}

	return result;

destroy_wait_event:
	tmp_result = doca_sync_event_destroy(*wait_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);

	}
	return result;
}

doca_error_t
create_doca_dpa_completion_sync_event(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, struct doca_sync_event **comp_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_sync_event_publisher_add_location_dpa(*comp_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as publisher for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_comp_event;
	}

	result = doca_sync_event_subscriber_add_location_cpu(*comp_event, doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU as subscriber for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_comp_event;
	}

	result = doca_sync_event_start(*comp_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_comp_event;
	}

	return result;

destroy_comp_event:
	tmp_result = doca_sync_event_destroy(*comp_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);

	}
	return result;
}

doca_error_t
create_doca_dpa_kernel_sync_event(struct doca_dpa *doca_dpa, struct doca_sync_event **kernel_event)
{
	doca_error_t result, tmp_result;

	result = doca_sync_event_create(kernel_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA sync event: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_sync_event_publisher_add_location_dpa(*kernel_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as publisher for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_kernel_event;
	}

	result = doca_sync_event_subscriber_add_location_dpa(*kernel_event, doca_dpa);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DPA as subscriber for DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_kernel_event;
	}

	result = doca_sync_event_start(*kernel_event);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA sync event: %s", doca_get_error_string(result));
		goto destroy_kernel_event;
	}

	return result;

destroy_kernel_event:
	tmp_result = doca_sync_event_destroy(*kernel_event);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA sync event: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);

	}
	return result;
}

doca_error_t
allocate_dpa_resources(struct dpa_resources *resources, struct dpa_config *cfg)
{
	doca_error_t result;

	/* open doca device */
	result = open_dpa_device(cfg->device_name, &resources->doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("open_dpa_device() failed");
		goto exit_label;
	}

	/* create doca_dpa context */
	result = doca_dpa_create(resources->doca_device, dpa_sample_app, &(resources->doca_dpa), 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA DPA context: %s", doca_get_error_string(result));
		goto close_doca_dev;
	}

	return result;

close_doca_dev:
	doca_dev_close(resources->doca_device);
exit_label:
	return result;
}

doca_error_t
destroy_dpa_resources(struct dpa_resources *resources)
{
	doca_error_t result = DOCA_SUCCESS;
	doca_error_t tmp_result;

	/* destroy doca_dpa context */
	tmp_result = doca_dpa_destroy(resources->doca_dpa);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("doca_dpa_destroy() failed: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* close doca device */
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}
