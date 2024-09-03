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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <doca_log.h>
#include <doca_error.h>
#include <doca_argp.h>

#include "rdma_common.h"

DOCA_LOG_REGISTER(RDMA::COMMON);

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
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	char *device_name = (char *)param;
	int len;

	len = strnlen(device_name, DOCA_DEVINFO_IBDEV_NAME_SIZE);
	if (len == DOCA_DEVINFO_IBDEV_NAME_SIZE) {
		DOCA_LOG_ERR("Entered IB device name exceeding the maximum size of %d",
				DOCA_DEVINFO_IBDEV_NAME_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strncpy(rdma_cfg->device_name, device_name, len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle send string parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
send_string_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	char *send_string = (char *)param;
	int len;

	len = strnlen(send_string, MAX_ARG_SIZE);
	if (len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered send string exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}
	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->send_string, send_string, len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle read string parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
read_string_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	char *read_string = (char *)param;
	int len;

	len = strnlen(read_string, MAX_ARG_SIZE);
	if (len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered read string exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}
	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->read_string, read_string, len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle write string parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
write_string_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	char *write_string = (char *)param;
	int len;

	len = strnlen(write_string, MAX_ARG_SIZE);
	if (len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered send string exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}
	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->write_string, write_string, len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle exported descriptor file path parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
local_descriptor_path_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	const char *path = (char *)param;
	int path_len;

	path_len = strnlen(path, MAX_ARG_SIZE);
	if (path_len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered path exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->local_connection_desc_path, path, path_len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle exported descriptor file path parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
remote_descriptor_path_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	const char *path = (char *)param;
	int path_len = strnlen(path, MAX_ARG_SIZE);

	if (path_len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered path exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->remote_connection_desc_path, path, path_len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle exported descriptor file path parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
mmap_descriptor_path_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	const char *path = (char *)param;
	int path_len = strnlen(path, MAX_ARG_SIZE);

	if (path_len == MAX_ARG_SIZE) {
		DOCA_LOG_ERR("Entered path exceeded buffer size: %d", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* The string will be '\0' terminated due to the strnlen check above */
	strncpy(rdma_cfg->remote_resource_desc_path, path, path_len + 1);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle gid_index parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
gid_index_param_callback(void *param, void *config)
{
	struct rdma_config *rdma_cfg = (struct rdma_config *)config;
	const int gid_index = *(uint32_t *)param;

	if (gid_index < 0) {
		DOCA_LOG_ERR("GID index for DOCA RDMA must be non-negative");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rdma_cfg->is_gid_index_set = true;
	rdma_cfg->gid_index = (uint32_t)gid_index;

	return DOCA_SUCCESS;
}

doca_error_t
register_rdma_send_string_param(void)
{
	struct doca_argp_param *send_string_param;
	doca_error_t result;

	/* Create and register send_string param */
	result = doca_argp_param_create(&send_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(send_string_param, "s");
	doca_argp_param_set_long_name(send_string_param, "send-string");
	doca_argp_param_set_arguments(send_string_param, "<Send string>");
	doca_argp_param_set_description(send_string_param, "String to send (optional). If not provided then \"" DEFAULT_STRING "\" will be chosen");
	doca_argp_param_set_callback(send_string_param, send_string_callback);
	doca_argp_param_set_type(send_string_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(send_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

doca_error_t
register_rdma_read_string_param(void)
{
	struct doca_argp_param *read_string_param;
	doca_error_t result;

	/* Create and register read_string param */
	result = doca_argp_param_create(&read_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(read_string_param, "r");
	doca_argp_param_set_long_name(read_string_param, "read-string");
	doca_argp_param_set_arguments(read_string_param, "<Read string>");
	doca_argp_param_set_description(read_string_param, "String to read (optional). If not provided then \"" DEFAULT_STRING "\" will be chosen");
	doca_argp_param_set_callback(read_string_param, read_string_callback);
	doca_argp_param_set_type(read_string_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(read_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}


	return result;
}

doca_error_t
register_rdma_write_string_param(void)
{
	struct doca_argp_param *write_string_param;
	doca_error_t result;

	/* Create and register write_string param */
	result = doca_argp_param_create(&write_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(write_string_param, "w");
	doca_argp_param_set_long_name(write_string_param, "write-string");
	doca_argp_param_set_arguments(write_string_param, "<Write string>");
	doca_argp_param_set_description(write_string_param, "String to write (optional). If not provided then \"" DEFAULT_STRING "\" will be chosen");
	doca_argp_param_set_callback(write_string_param, write_string_callback);
	doca_argp_param_set_type(write_string_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(write_string_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}

doca_error_t
register_rdma_common_params(void)
{
	doca_error_t result;
	struct doca_argp_param *device_param;
	struct doca_argp_param *local_desc_path_param;
	struct doca_argp_param *remote_desc_path_param;
	struct doca_argp_param *remote_resource_desc_path;
	struct doca_argp_param *gid_index_param;

	/* Create and register device param */
	result = doca_argp_param_create(&device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(device_param, "d");
	doca_argp_param_set_long_name(device_param, "device");
	doca_argp_param_set_arguments(device_param, "<IB device name>");
	doca_argp_param_set_description(device_param, "IB device name");
	doca_argp_param_set_callback(device_param, device_address_callback);
	doca_argp_param_set_type(device_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(device_param);
	result = doca_argp_register_param(device_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register local descriptor file path param */
	result = doca_argp_param_create(&local_desc_path_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(local_desc_path_param, "ld");
	doca_argp_param_set_long_name(local_desc_path_param, "local-descriptor-path");
	doca_argp_param_set_description(local_desc_path_param,
					"Local descriptor file path that includes the local connection descriptor, to be copied to the remote program");
	doca_argp_param_set_callback(local_desc_path_param, local_descriptor_path_callback);
	doca_argp_param_set_type(local_desc_path_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(local_desc_path_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register remote descriptor file path param */
	result = doca_argp_param_create(&remote_desc_path_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(remote_desc_path_param, "re");
	doca_argp_param_set_long_name(remote_desc_path_param, "remote-descriptor-path");
	doca_argp_param_set_description(remote_desc_path_param,
					"Remote descriptor file path that includes the remote connection descriptor, to be copied from the remote program");
	doca_argp_param_set_callback(remote_desc_path_param, remote_descriptor_path_callback);
	doca_argp_param_set_type(remote_desc_path_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(remote_desc_path_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register mmap descriptor file path param */
	result = doca_argp_param_create(&remote_resource_desc_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(remote_resource_desc_path, "m");
	doca_argp_param_set_long_name(remote_resource_desc_path, "remote-resource-descriptor-path");
	doca_argp_param_set_description(remote_resource_desc_path,
					"Remote descriptor file path that includes the remote mmap connection descriptor, to be copied from the remote program");
	doca_argp_param_set_callback(remote_resource_desc_path, mmap_descriptor_path_callback);
	doca_argp_param_set_type(remote_resource_desc_path, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(remote_resource_desc_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register gid_index param */
	result = doca_argp_param_create(&gid_index_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(gid_index_param, "g");
	doca_argp_param_set_long_name(gid_index_param, "gid-index");
	doca_argp_param_set_description(gid_index_param, "GID index for DOCA RDMA (optional)");
	doca_argp_param_set_callback(gid_index_param, gid_index_param_callback);
	doca_argp_param_set_type(gid_index_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(gid_index_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Open DOCA device
 *
 * @device_name [in]: The name of the wanted IB device (could be empty string)
 * @func [in]: Function to check if a given device is capable of executing some task
 * @doca_device [out]: An allocated DOCA device on success and NULL otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
open_doca_device(const char *device_name, task_check func, struct doca_dev **doca_device)
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
		result = doca_devinfo_get_ibdev_name(dev_list[i], ibdev_name, sizeof(ibdev_name));
		if (result != DOCA_SUCCESS ||
			(strlen(device_name) != 0 && strncmp(device_name, ibdev_name, DOCA_DEVINFO_IBDEV_NAME_SIZE) != 0))
			continue;
		/* If any special capabilities are needed */
		if (func != NULL && func(dev_list[i]) != DOCA_SUCCESS)
			continue;
		result = doca_dev_open(dev_list[i], doca_device);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			goto out;
		}
		break;
	}

out:
	doca_devinfo_destroy_list(dev_list);

	if (*doca_device == NULL) {
		DOCA_LOG_ERR("Couldn't get DOCA device");
		return DOCA_ERROR_NOT_FOUND;
	}

	return result;
}

doca_error_t
allocate_rdma_resources(struct rdma_config *cfg, const uint32_t mmap_permissions,
			const uint32_t rdma_permissions, task_check func, struct rdma_resources *resources)
{
	doca_error_t result, tmp_result;

	resources->cfg = cfg;
	resources->run_main_loop = true;
	resources->num_remaining_tasks = 0;

	/* Open DOCA device */
	result = open_doca_device(cfg->device_name, func, &(resources->doca_device));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create mmap with no user data */
	result = doca_mmap_create(&(resources->mmap));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA mmap: %s", doca_error_get_descr(result));
		goto close_doca_dev;
	}

	/* Set permissions for DOCA mmap */
	result = doca_mmap_set_permissions(resources->mmap, mmap_permissions);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions for DOCA mmap: %s", doca_error_get_descr(result));
		goto destroy_doca_mmap;
	}

	/* Allocate memory for memory range */
	resources->mmap_memrange = calloc(MEM_RANGE_LEN, sizeof(*resources->mmap_memrange));
	if (resources->mmap_memrange == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for mmap_memrange: %s", doca_error_get_descr(result));
		result = DOCA_ERROR_NO_MEMORY;
		goto destroy_doca_mmap;
	}

	/* Set memory range for DOCA mmap */
	result = doca_mmap_set_memrange(resources->mmap, (void *)resources->mmap_memrange, MEM_RANGE_LEN);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set memrange for DOCA mmap: %s", doca_error_get_descr(result));
		goto free_memrange;
	}

	/* Add DOCA device for DOCA mmap */
	result = doca_mmap_add_dev(resources->mmap, resources->doca_device);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add device for DOCA mmap: %s", doca_error_get_descr(result));
		goto free_memrange;
	}

	/* Start DOCA mmap */
	result = doca_mmap_start(resources->mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA mmap: %s", doca_error_get_descr(result));
		goto free_memrange;
	}

	result = doca_pe_create(&(resources->pe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions to DOCA RDMA: %s", doca_error_get_descr(result));
		goto stop_doca_mmap;
	}

	/* Create DOCA RDMA instance */
	result = doca_rdma_create(resources->doca_device, &(resources->rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA RDMA: %s", doca_error_get_descr(result));
		goto destroy_pe;
	}

	/* Convert DOCA RDMA to general DOCA context */
	resources->rdma_ctx = doca_rdma_as_ctx(resources->rdma);
	if (resources->rdma_ctx == NULL) {
		result = DOCA_ERROR_UNEXPECTED;
		DOCA_LOG_ERR("Failed to convert DOCA RDMA to DOCA context: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* Set permissions to DOCA RDMA */
	result = doca_rdma_set_permissions(resources->rdma, rdma_permissions);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions to DOCA RDMA: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* Set gid_index to DOCA RDMA if it's provided */
	if (cfg->is_gid_index_set) {
		/* Set gid_index to DOCA RDMA */
		result = doca_rdma_set_gid_index(resources->rdma, cfg->gid_index);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set gid_index to DOCA RDMA: %s", doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}
	}

	result = doca_pe_connect_ctx(resources->pe, resources->rdma_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set progress engine for RDMA: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	return result;

destroy_doca_rdma:
	/* Destroy DOCA RDMA */
	tmp_result = doca_rdma_destroy(resources->rdma);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_pe:
	/* Destroy DOCA progress engine */
	tmp_result = doca_pe_destroy(resources->pe);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA progress engine: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
stop_doca_mmap:
	/* Stop DOCA mmap */
	tmp_result = doca_mmap_stop(resources->mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
free_memrange:
	/* Free DOCA mmap memory range */
	free(resources->mmap_memrange);
destroy_doca_mmap:
	/* Destroy DOCA mmap */
	tmp_result = doca_mmap_destroy(resources->mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
close_doca_dev:
	/* Close DOCA device */
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
	return result;
}


/*
 * Delete file if exists
 *
 * @file_path [in]: The path of the file we want to delete
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
delete_file(const char *file_path)
{
	FILE *fp;
	int res;
	doca_error_t result = DOCA_SUCCESS;

	/* Check if file exists before deleting it */
	fp = fopen(file_path, "r");
	if (fp) {
		/* Delete file by using unlink */
		res = unlink(file_path);
		if (res != 0) {
			result = DOCA_ERROR_IO_FAILED;
			DOCA_LOG_ERR("Failed to delete file %s: %s", file_path, doca_error_get_descr(result));
		}
		fclose(fp);
	}

	return result;
}

/*
 * Delete the description files that we created
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
clean_up_files(struct rdma_config *cfg)
{
	doca_error_t result = DOCA_SUCCESS;

	result = delete_file(cfg->local_connection_desc_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Deleting file %s failed: %s", cfg->local_connection_desc_path,
				doca_error_get_descr(result));
		return result;
	}

	result = delete_file(cfg->remote_connection_desc_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Deleting file %s failed: %s", cfg->remote_connection_desc_path,
				doca_error_get_descr(result));
		return result;
	}

	result = delete_file(cfg->remote_resource_desc_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Deleting file %s failed: %s", cfg->remote_resource_desc_path,
				doca_error_get_descr(result));
		return result;
	}

	return result;
}

doca_error_t
destroy_rdma_resources(struct rdma_resources *resources, struct rdma_config *cfg)
{
	doca_error_t result = DOCA_SUCCESS, tmp_result;

	/* Stop and destroy remote mmap if exists */
	if (resources->remote_mmap != NULL) {
		result = doca_mmap_stop(resources->remote_mmap);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to stop DOCA remote mmap: %s", doca_error_get_descr(result));

		tmp_result = doca_mmap_destroy(resources->remote_mmap);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy DOCA remote mmap: %s", doca_error_get_descr(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}

	}

	/* Destroy remote sync event if exists */
	if (resources->remote_se != NULL) {
		tmp_result = doca_sync_event_remote_net_destroy(resources->remote_se);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy DOCA remote sync event: %s", doca_error_get_descr(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}

	/* Destroy DOCA RDMA */
	tmp_result = doca_rdma_destroy(resources->rdma);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Destroy DOCA progress engine */
	tmp_result = doca_pe_destroy(resources->pe);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA progress engine: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Stop DOCA mmap */
	tmp_result = doca_mmap_stop(resources->mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Free DOCA mmap memory range */
	free(resources->mmap_memrange);

	/* Destroy DOCA mmap */
	tmp_result = doca_mmap_destroy(resources->mmap);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA mmap: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Free remote connection descriptor */
	if (resources->remote_rdma_conn_descriptor)
		free(resources->remote_rdma_conn_descriptor);

	/* Free remote mmap descriptor */
	if (resources->remote_mmap_descriptor)
		free(resources->remote_mmap_descriptor);

	/* Free remote mmap descriptor */
	if (resources->sync_event_descriptor)
		free(resources->sync_event_descriptor);

	/* Close DOCA device */
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	/* Delete description files that we created */
	tmp_result = clean_up_files(cfg);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to clean up files: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

doca_error_t
write_file(const char *file_path, const char *string, size_t string_len)
{
	FILE *fp;
	doca_error_t result = DOCA_SUCCESS;

	/* Check if the file exists by opening it to read */
	fp = fopen(file_path, "r");
	if (fp) {
		DOCA_LOG_ERR("File %s already exists. Please delete it prior to running the sample", file_path);
		fclose(fp);
		return DOCA_ERROR_IO_FAILED;
	}

	fp = fopen(file_path, "wb");
	if (fp == NULL) {
		DOCA_LOG_ERR("Failed to create file %s", file_path);
		return DOCA_ERROR_IO_FAILED;
	}

	/* Write the string */
	if (fwrite(string, 1, string_len, fp) != string_len) {
		DOCA_LOG_ERR("Failed to write on file %s", file_path);
		fclose(fp);
		return DOCA_ERROR_IO_FAILED;
	}

	/* Close the file */
	fclose(fp);

	return result;
}

doca_error_t
read_file(const char *file_path, char **string, size_t *string_len)
{
	FILE *fp;
	long file_size;
	doca_error_t result = DOCA_SUCCESS;

	/* Open the file for reading */
	fp = fopen(file_path, "r");
	if (fp == NULL) {
		DOCA_LOG_ERR("Failed to open the file %s for reading", file_path);
		return DOCA_ERROR_IO_FAILED;
	}

	/* Calculate the size of the file */
	if (fseek(fp, 0, SEEK_END) != 0) {
		DOCA_LOG_ERR("Failed to calculate file size");
		fclose(fp);
		return DOCA_ERROR_IO_FAILED;
	}

	file_size = ftell(fp);
	if (file_size == -1) {
		DOCA_LOG_ERR("Failed to calculate file size");
		fclose(fp);
		return DOCA_ERROR_IO_FAILED;
	}

	/* Rewind file to the start */
	if (fseek(fp, 0, SEEK_SET) != 0) {
		DOCA_LOG_ERR("Failed to rewind file");
		fclose(fp);
		return DOCA_ERROR_IO_FAILED;
	}

	*string_len = file_size;
	*string = malloc(file_size);
	if (*string == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory of size %lu\n", file_size);
		fclose(fp);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Read the file to the string */
	if (fread(*string, 1, file_size, fp) != (size_t)file_size) {
		DOCA_LOG_ERR("Failed read from file %s", file_path);
		result = DOCA_ERROR_IO_FAILED;
		free(*string);
	}

	fclose(fp);
	return result;
}
