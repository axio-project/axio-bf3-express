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

#include <doca_log.h>
#include <doca_argp.h>

#include "dpa_all_to_all_core.h"

DOCA_LOG_REGISTER(A2A);

/*
 * ARGP Callback - Handle message size parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
msgsize_callback(void *param, void *config)
{
	struct a2a_config *a2a_cgf = (struct a2a_config *)config;
	int msgsize = *((int *)param);

	if (msgsize % sizeof(int) != 0) {
		DOCA_LOG_ERR("Entered message size is not in multiplies of integer size (%lu)", sizeof(int));
		return DOCA_ERROR_INVALID_VALUE;
	}
	a2a_cgf->msgsize = msgsize;

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle IB device name parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
devices_name_callback(void *param, void *config)
{
	struct a2a_config *a2a_cgf = (struct a2a_config *)config;
	char *devices_names = (char *)param;
	char *devices_names_list;
	int len;

	len = strnlen(devices_names, MAX_IB_DEVICE_NAME_LEN);
	if (len == MAX_IB_DEVICE_NAME_LEN) {
		DOCA_LOG_ERR("Entered IB device name exceeding the maximum size of %d", MAX_USER_IB_DEVICE_NAME_LEN*MAX_DEVICES + 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* Split the devices names by space */
	devices_names_list = strtok(devices_names, ",");
	strncpy(a2a_cgf->device1_name, devices_names_list, MAX_USER_IB_DEVICE_NAME_LEN);

	if (!dpa_device_exists_check(a2a_cgf->device1_name)) {
		DOCA_LOG_ERR("Entered IB device name: %s doesn't exist or doesn't support DPA", a2a_cgf->device1_name);
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* If another name was provided then get it as well */
	devices_names_list = strtok(NULL, ",");
	if (devices_names_list != NULL) {
		strncpy(a2a_cgf->device2_name, devices_names_list, MAX_USER_IB_DEVICE_NAME_LEN);
		if (!dpa_device_exists_check(a2a_cgf->device2_name)) {
			DOCA_LOG_ERR("Entered IB device name: %s doesn't exist or doesn't support DPA", a2a_cgf->device2_name);
			return DOCA_ERROR_INVALID_VALUE;
		}
		/* Max two devices, so check if a third device was added */
		devices_names_list = strtok(NULL, ",");
		if (devices_names_list != NULL) {
			DOCA_LOG_ERR("Entered more than two IB devices");
			return DOCA_ERROR_INVALID_VALUE;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Register the command line parameters for the All to All application.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
register_all_to_all_params(void)
{
	doca_error_t result;
	struct doca_argp_param *msgsize_param;
	struct doca_argp_param *devices_param;

	result = doca_argp_param_create(&msgsize_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(msgsize_param, "m");
	doca_argp_param_set_long_name(msgsize_param, "msgsize");
	doca_argp_param_set_arguments(msgsize_param, "<Message size>");
	doca_argp_param_set_description(msgsize_param, "The message size - the size of the sendbuf and recvbuf (in bytes). Must be in multiplies of integer size. Default is size of one integer times the number of processes.");
	doca_argp_param_set_callback(msgsize_param, msgsize_callback);
	doca_argp_param_set_type(msgsize_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(msgsize_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_argp_param_create(&devices_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(devices_param, "d");
	doca_argp_param_set_long_name(devices_param, "devices");
	doca_argp_param_set_arguments(devices_param, "<IB device names>");
	doca_argp_param_set_description(devices_param, "IB devices names that supports DPA, separated by comma without spaces (max of two devices). If not provided then a random IB device will be chosen.");
	doca_argp_param_set_callback(devices_param, devices_name_callback);
	doca_argp_param_set_type(devices_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(devices_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Prepare the arg parser user parameters
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @cfg [out]: User configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
prepare_argp_parameters(int argc, char **argv, struct a2a_config *cfg)
{
	doca_error_t result;

	/* Initialize arg parser for the All to All application */
	result = doca_argp_init("doca_dpa_all_to_all", cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		return result;
	}

	/* Register all_to_all params */
	result = register_all_to_all_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register application parameters: %s", doca_error_get_descr(result));
		doca_argp_destroy();
		return result;
	}

	/* Start arg parser */
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_error_get_descr(result));
		doca_argp_destroy();
	}

	return result;
}

/*
 * Application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	int rank, size;
	struct a2a_config cfg = {0};
	doca_error_t result;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_SUCCESS;

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

	/* Initialize MPI variables */
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	/* Set default value for devices names */
	strcpy(cfg.device1_name, IB_DEVICE_DEFAULT_NAME);
	strcpy(cfg.device2_name, IB_DEVICE_DEFAULT_NAME);

	/* Set default value of message size */
	cfg.msgsize = MESSAGE_SIZE_DEFAULT_LEN;

	/* Proccess of rank 0 will prepare the parameters and send them to the rest of the processes */
	if (rank == 0)
		result = prepare_argp_parameters(argc, argv, &cfg);

	/* Using MPI Bcast, send the result from process rank 0 to the rest of the processes */
	MPI_Bcast(&result, sizeof(result), MPI_BYTE, 0, MPI_COMM_WORLD);
	if (result != DOCA_SUCCESS) {
		exit_status = EXIT_FAILURE;
		goto destroy_resources;
	}

	/* Using MPI Bcast, send the parameter configuration struct from process rank 0 to the rest of the processes */
	MPI_Bcast(&cfg, sizeof(cfg), MPI_BYTE, 0, MPI_COMM_WORLD);

	/* All to all logic */
	result = dpa_a2a(argc, argv, &cfg);
	if (result != DOCA_SUCCESS) {
		if (rank == 0)
			DOCA_LOG_ERR("dpa_a2a() encountered errors: %s", doca_error_get_descr(result));
		exit_status = EXIT_FAILURE;
	}

destroy_resources:
	if (rank == 0)
		doca_argp_destroy();
	MPI_Finalize();

	return exit_status;
}
