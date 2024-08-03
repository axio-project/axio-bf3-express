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

#include <doca_argp.h>
#include <doca_compress.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>

#include <utils.h>

DOCA_LOG_REGISTER(COMPRESS_DEFLATE::MAIN);

#define USER_MAX_FILE_NAME 255				/* max file name length */
#define MAX_FILE_NAME (USER_MAX_FILE_NAME + 1)		/* max file name string length */
#define MAX_FILE_SIZE (128 * 1024 * 1024)		/* compress files up to 128MB */

/* Configuration struct */
struct compress_cfg {
	char file_path[MAX_FILE_NAME];			/* file to compress/decompress */
	char output_path[MAX_FILE_NAME];		/* output file */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* device PCI address */
	enum doca_compress_job_types mode;		/* compress job type */
};

/* Sample's Logic */
doca_error_t compress_deflate(const char *pci_addr, char *src_buffer, size_t file_size, enum doca_compress_job_types job_type, const char *output_path);

/*
 * ARGP Callback - Handle PCI device address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
pci_address_callback(void *param, void *config)
{
	struct compress_cfg *compress_cfg = (struct compress_cfg *)config;
	char *pci_address = (char *)param;
	int len;

	len = strnlen(pci_address, DOCA_DEVINFO_PCI_ADDR_SIZE);
	if (len == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strncpy(compress_cfg->pci_address, pci_address, len + 1);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle user file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
file_callback(void *param, void *config)
{
	struct compress_cfg *compress_cfg = (struct compress_cfg *)config;
	char *file = (char *)param;
	int len;

	len = strnlen(file, MAX_FILE_NAME);
	if (len == MAX_FILE_NAME) {
		DOCA_LOG_ERR("Invalid file name length, max %d", USER_MAX_FILE_NAME);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strcpy(compress_cfg->file_path, file);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle mode parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
mode_callback(void *param, void *config)
{
	struct compress_cfg *compress_cfg = (struct compress_cfg *)config;
	char *mode = (char *)param;

	if (strcmp(mode, "compress") == 0)
		compress_cfg->mode = DOCA_COMPRESS_DEFLATE_JOB;
	else if (strcmp(mode, "decompress") == 0)
		compress_cfg->mode = DOCA_DECOMPRESS_DEFLATE_JOB;
	else {
		DOCA_LOG_ERR("Illegal mode = [%s]", mode);
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle output file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
output_callback(void *param, void *config)
{
	struct compress_cfg *compress_cfg = (struct compress_cfg *)config;
	char *file = (char *)param;
	int len;

	len = strnlen(file, MAX_FILE_NAME);
	if (len == MAX_FILE_NAME) {
		DOCA_LOG_ERR("Invalid file name length, max %d", USER_MAX_FILE_NAME);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strcpy(compress_cfg->output_path, file);
	return DOCA_SUCCESS;
}

/*
 * Register the command line parameters for the sample.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
register_compress_params(void)
{
	doca_error_t result;
	struct doca_argp_param *pci_param, *file_param, *mode_param, *output_param;

	result = doca_argp_param_create(&pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(pci_param, "p");
	doca_argp_param_set_long_name(pci_param, "pci-addr");
	doca_argp_param_set_description(pci_param, "DOCA device PCI device address");
	doca_argp_param_set_callback(pci_param, pci_address_callback);
	doca_argp_param_set_type(pci_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(pci_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_argp_param_create(&file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(file_param, "f");
	doca_argp_param_set_long_name(file_param, "file");
	doca_argp_param_set_description(file_param, "input file to compress/decompress");
	doca_argp_param_set_callback(file_param, file_callback);
	doca_argp_param_set_type(file_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_argp_param_create(&mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(mode_param, "m");
	doca_argp_param_set_long_name(mode_param, "mode");
	doca_argp_param_set_description(mode_param, "mode - {compress, decompress}");
	doca_argp_param_set_callback(mode_param, mode_callback);
	doca_argp_param_set_type(mode_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_argp_param_create(&output_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(output_param, "o");
	doca_argp_param_set_long_name(output_param, "output");
	doca_argp_param_set_description(output_param, "output file");
	doca_argp_param_set_callback(output_param, output_callback);
	doca_argp_param_set_type(output_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(output_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	struct compress_cfg compress_cfg;
	char *file_data = NULL;
	size_t file_size;
	int exit_status = EXIT_FAILURE;

	strcpy(compress_cfg.pci_address, "03:00.0");
	strcpy(compress_cfg.file_path, "data_to_compress.txt");
	strcpy(compress_cfg.output_path, "out.txt");
	compress_cfg.mode = DOCA_COMPRESS_DEFLATE_JOB;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_compress_deflate", &compress_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}

	result = register_compress_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register ARGP params: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = read_file(compress_cfg.file_path, &file_data, &file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read file: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}
	if (file_size > MAX_FILE_SIZE) {
		DOCA_LOG_ERR("Invalid file size. Should be smaller then %d", MAX_FILE_SIZE);
		goto data_file_cleanup;
	}

	result = compress_deflate(compress_cfg.pci_address, file_data, file_size,
				  compress_cfg.mode, compress_cfg.output_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("compress_deflate() encountered an error: %s", doca_get_error_string(result));
		goto data_file_cleanup;
	}

	exit_status = EXIT_SUCCESS;

data_file_cleanup:
	if (file_data != NULL)
		free(file_data);
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
