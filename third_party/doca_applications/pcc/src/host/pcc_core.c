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
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#include <doca_argp.h>

#include "pcc_core.h"

/*
 * Check if the provided device name is a name of a valid IB device with PCC capabilities
 *
 * @device_name [in]: The wanted IB device name with PCC capabilites
 * @return: True if device_name is an IB device with PCC capabilities, false otherwise.
 */
static bool
pcc_device_exists_check(const char *device_name)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs = 0;
	doca_error_t result;
	bool exists = false;
	char ibdev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE] = {0};
	uint32_t i = 0;

	result = doca_devinfo_list_create(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to load DOCA devices list: %s\n", doca_get_error_string(result));
		return false;
	}

	/* Search device with same device name and PCC capabilites supported */
	for (i = 0; i < nb_devs; i++) {
		result = doca_devinfo_get_is_pcc_supported(dev_list[i]);
		if (result != DOCA_SUCCESS)
			continue;
		result = doca_devinfo_get_ibdev_name(dev_list[i], ibdev_name, sizeof(ibdev_name));
		if (result != DOCA_SUCCESS)
			continue;

		/* Check if we found the device with the wanted name */
		if (strncmp(device_name, ibdev_name, DOCA_DEVINFO_IBDEV_NAME_SIZE) == 0) {
			exists = true;
			break;
		}
	}

	doca_devinfo_list_destroy(dev_list);

	return exists;
}

/*
 * Open DOCA device that supports PCC
 *
 * @device_name [in]: Requested IB device name
 * @doca_device [out]: An allocated DOCA device that supports PCC on success and NULL otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
open_pcc_device(const char *device_name, struct doca_dev **doca_device)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs = 0;
	doca_error_t result;
	char ibdev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE] = {0};
	uint32_t i = 0;

	result = doca_devinfo_list_create(&dev_list, &nb_devs);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to load DOCA devices list: %s\n", doca_get_error_string(result));
		return result;
	}

	/* Search device with same device name and PCC capabilites supported */
	for (i = 0; i < nb_devs; i++) {
		result = doca_devinfo_get_is_pcc_supported(dev_list[i]);
		if (result != DOCA_SUCCESS)
			continue;
		result = doca_devinfo_get_ibdev_name(dev_list[i], ibdev_name, sizeof(ibdev_name));
		if (result != DOCA_SUCCESS)
			continue;

		/* Check if the device has the requested device name */
		if (strncmp(device_name, ibdev_name, DOCA_DEVINFO_IBDEV_NAME_SIZE) != 0)
			continue;

		result = doca_dev_open(dev_list[i], doca_device);
		if (result != DOCA_SUCCESS) {
			doca_devinfo_list_destroy(dev_list);
			PRINT_ERROR("Error: Failed to open DOCA device: %s\n", doca_get_error_string(result));
			return result;
		}
		break;
	}

	doca_devinfo_list_destroy(dev_list);

	if (*doca_device == NULL) {
		PRINT_ERROR("Error: Couldn't get DOCA device\n");
		return DOCA_ERROR_NOT_FOUND;
	}

	return result;
}

doca_error_t
pcc_init(struct pcc_config *cfg, struct pcc_resources *resources)
{
	doca_error_t result, tmp_result;
	uint32_t min_num_threads, max_num_threads;

	/* Open DOCA device that supports PCC */
	result = open_pcc_device(cfg->device_name, &(resources->doca_device));
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to open DOCA device that supports PCC\n");
		return result;
	}

	/* Create DOCA PCC context */
	result = doca_pcc_create(resources->doca_device, &(resources->doca_pcc));
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to create DOCA PCC context\n");
		goto close_doca_dev;
	}

	result = doca_pcc_get_min_num_threads(resources->doca_pcc, &min_num_threads);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Failed to get minimum DOCA PCC number of threads\n");
		goto destroy_pcc;
	}

	result = doca_pcc_get_max_num_threads(resources->doca_pcc, &max_num_threads);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Failed to get maximum DOCA PCC number of threads\n");
		goto destroy_pcc;
	}

	if (cfg->pcc_threads_num < min_num_threads || cfg->pcc_threads_num > max_num_threads) {
		PRINT_ERROR("Invalid number of PCC threads: %u. The Minimum number of PCC threads is %d and the maximum number of PCC threads is %d\n",
				cfg->pcc_threads_num, min_num_threads, max_num_threads);
		result = DOCA_ERROR_INVALID_VALUE;
		goto destroy_pcc;
	}

	/* Set DOCA PCC app */
	result = doca_pcc_set_app(resources->doca_pcc, pcc_main_app);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to set DOCA PCC app\n");
		goto destroy_pcc;
	}

	/* Set DOCA PCC thread affinity */
	result = doca_pcc_set_thread_affinity(resources->doca_pcc, cfg->pcc_threads_num, cfg->pcc_threads_list);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to set thread affinity for DOCA PCC\n");
		goto destroy_pcc;
	}

	return result;

destroy_pcc:
	tmp_result = doca_pcc_destroy(resources->doca_pcc);
	if (tmp_result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to destroy DOCA PCC context: %s\n", doca_get_error_string(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
close_doca_dev:
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to close DOCA device: %s\n", doca_get_error_string(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

doca_error_t
pcc_destroy(struct pcc_resources *resources)
{
	doca_error_t result, tmp_result;

	result = doca_pcc_destroy(resources->doca_pcc);
	if (result != DOCA_SUCCESS)
		PRINT_ERROR("Error: Failed to destroy DOCA PCC context: %s\n", doca_get_error_string(result));

	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to close DOCA device: %s\n", doca_get_error_string(result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

/*
 * ARGP Callback - Handle IB device name parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
device_name_callback(void *param, void *config)
{
	struct pcc_config *pcc_cgf = (struct pcc_config *)config;
	char *device_name = (char *)param;
	int len;

	len = strnlen(device_name, DOCA_DEVINFO_IBDEV_NAME_SIZE);
	if (len == DOCA_DEVINFO_IBDEV_NAME_SIZE) {
		PRINT_ERROR("Error: Entered IB device name exceeding the maximum size of %d\n",
				DOCA_DEVINFO_IBDEV_NAME_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strncpy(pcc_cgf->device_name, device_name, len + 1);

	if (!pcc_device_exists_check(pcc_cgf->device_name)) {
		PRINT_ERROR("Error: Entered IB device name: %s doesn't exist or doesn't support PCC\n", pcc_cgf->device_name);
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle PCC wait time parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
wait_time_callback(void *param, void *config)
{
	struct pcc_config *pcc_cgf = (struct pcc_config *)config;
	int wait_time = *((int *)param);

	/* Wait time must be either positive or infinity (meaning -1 )*/
	if (wait_time == 0) {
		PRINT_ERROR("Error: Entered wait time can't be zero. Must be either positive or infinity (meaning negative value)\n");
		return DOCA_ERROR_INVALID_VALUE;
	}

	pcc_cgf->wait_time = wait_time;

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle PCC threads list parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
pcc_threads_list_callback(void *param, void *config)
{
	struct pcc_config *pcc_cgf = (struct pcc_config *)config;
	char *pcc_threads_list_string = (char *)param;
	static const char delim[2] = " ";
	char *curr_pcc_string;
	int curr_pcc_check, i, len;
	uint32_t curr_pcc;

	len = strnlen(pcc_threads_list_string, MAX_ARG_SIZE);
	if (len == MAX_ARG_SIZE) {
		PRINT_ERROR("Error: Entered PCC threads list exceeded buffer size: %d\n", MAX_USER_ARG_SIZE);
		return DOCA_ERROR_INVALID_VALUE;
	}

	pcc_cgf->pcc_threads_num = 0;

	/* Check and fill out the PCC threads list */
	/* Get the first PCC thread number */
	curr_pcc_string = strtok(pcc_threads_list_string, delim);
	if (curr_pcc_string == NULL) {
		PRINT_ERROR("Error: Invalid PCC threads list: %s\n", pcc_threads_list_string);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* Walk through rest of the PCC threads numbers */
	while (curr_pcc_string != NULL) {
		/* Check if it's a number by checking its digits */
		len = strnlen(pcc_threads_list_string, MAX_ARG_SIZE);
		for (i = 0; i < len; i++) {
			if (!isdigit(curr_pcc_string[i])) {
				PRINT_ERROR("Error: Invalid PCC thread number: %s\n", curr_pcc_string);
				return DOCA_ERROR_INVALID_VALUE;
			}
		}

		/* Convert to integer to check if it is non-negative */
		curr_pcc_check = (int)atoi(curr_pcc_string);
		if (curr_pcc_check < 0) {
			PRINT_ERROR("Error: Invalid PCC thread number %d. PCC threads numbers must be non-negative\n",
					curr_pcc_check);
			return DOCA_ERROR_INVALID_VALUE;
		}

		curr_pcc = (uint32_t)atoi(curr_pcc_string);
		pcc_cgf->pcc_threads_list[pcc_cgf->pcc_threads_num++] = curr_pcc;
		curr_pcc_string = strtok(NULL, delim);
	}

	return DOCA_SUCCESS;
}


doca_error_t
register_pcc_params(void)
{
	doca_error_t result;
	struct doca_argp_param *device_param;
	struct doca_argp_param *wait_time_param;
	struct doca_argp_param  *pcc_threads_list_param;

	/* Create and register DOCA device name parameter */
	result = doca_argp_param_create(&device_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to create ARGP param: %s\n", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(device_param, "d");
	doca_argp_param_set_long_name(device_param, "device");
	doca_argp_param_set_arguments(device_param, "<IB device names>");
	doca_argp_param_set_description(device_param, "IB device name that supports PCC (mandatory).");
	doca_argp_param_set_callback(device_param, device_name_callback);
	doca_argp_param_set_type(device_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(device_param);
	result = doca_argp_register_param(device_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to register program param: %s\n", doca_get_error_string(result));
		return result;
	}

	/* Create and register PCC wait time parameter */
	result = doca_argp_param_create(&wait_time_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to create ARGP param: %s\n", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(wait_time_param, "w");
	doca_argp_param_set_long_name(wait_time_param, "wait-time");
	doca_argp_param_set_arguments(wait_time_param, "<PCC wait time>");
	doca_argp_param_set_description(wait_time_param, "The duration of the DOCA PCC wait (optional), can provide negative values which means infinity. If not provided then -1 will be chosen.");
	doca_argp_param_set_callback(wait_time_param, wait_time_callback);
	doca_argp_param_set_type(wait_time_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(wait_time_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to register program param: %s\n", doca_get_error_string(result));
		return result;
	}

	/* Create and register PCC threads list parameter */
	result = doca_argp_param_create(&pcc_threads_list_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to create ARGP param: %s\n", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(pcc_threads_list_param, "p");
	doca_argp_param_set_long_name(pcc_threads_list_param, "pcc-threads");
	doca_argp_param_set_arguments(pcc_threads_list_param, "<PCC threads list>");
	doca_argp_param_set_description(pcc_threads_list_param, "A list of the PCC threads numbers to be chosen for the DOCA PCC context to run on (optional). Must be provided as a string, such that the number are separated by a space.");
	doca_argp_param_set_callback(pcc_threads_list_param, pcc_threads_list_callback);
	doca_argp_param_set_type(pcc_threads_list_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(pcc_threads_list_param);
	if (result != DOCA_SUCCESS) {
		PRINT_ERROR("Error: Failed to register program param: %s\n", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}
