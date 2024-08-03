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
#include <doca_apsh.h>
#include <doca_log.h>

#include "apsh_common.h"

DOCA_LOG_REGISTER(ENVARS_GET);

/*
 * Calls the DOCA APSH API function that matches this sample name and prints the result
 *
 * @dma_device_name [in]: IBDEV Name of the device to use for DMA
 * @pci_vuid [in]: VUID of the device exposed to the target system
 * @pid [in]: PID of the target process
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
envars_get(const char *dma_device_name, const char *pci_vuid, DOCA_APSH_PROCESS_PID_TYPE pid)
{
	doca_error_t result;
	int i, nb_processes;
	struct doca_apsh_ctx *apsh_ctx;
	struct doca_apsh_system *sys;
	struct doca_apsh_process *proc, **processes;
	int num_envars;
	struct doca_apsh_envar **envars_list;
	/* Hardcoded pathes to the files created by doca_apsh_config tool */
	const char *os_symbols = "/tmp/symbols.json";
	const char *mem_region = "/tmp/mem_regions.json";
	/* OS Type: Currently only Windows is supported */
	enum doca_apsh_system_os os_type = DOCA_APSH_SYSTEM_WINDOWS;

	/* Init */
	result = init_doca_apsh(dma_device_name, &apsh_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init the DOCA APSH lib");
		return result;
	}
	DOCA_LOG_INFO("DOCA APSH lib context init successful");

	result = init_doca_apsh_system(apsh_ctx, os_type, os_symbols, mem_region, pci_vuid, &sys);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init the system context");
		return result;
	}
	DOCA_LOG_INFO("DOCA APSH system context created");

	result = process_get(pid, sys, &nb_processes, &processes, &proc);
	if (result != DOCA_SUCCESS) {
		if (result == DOCA_ERROR_NOT_FOUND)
			DOCA_LOG_ERR("Process pid %d not found", pid);
		else
			DOCA_LOG_ERR("DOCA APSH encountered an error: %s", doca_get_error_string(result));
		cleanup_doca_apsh(apsh_ctx, sys);
		return result;
	}
	DOCA_LOG_INFO("Process with PID %d found", pid);
	DOCA_LOG_INFO("Proc(%d) name: %s", pid, doca_apsh_process_info_get(proc, DOCA_APSH_PROCESS_COMM));

	result = doca_apsh_envars_get(proc, &envars_list, &num_envars);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read libs info from host");
		doca_apsh_processes_free(processes);
		cleanup_doca_apsh(apsh_ctx, sys);
		return result;
	}
	DOCA_LOG_INFO("Successfully performed %s. Host proc(%d) contains %d envars", __func__, pid, num_envars);

	/* Print some attributes of the envars */
	DOCA_LOG_INFO("Envars for process %d:", pid);
	for (i = 0; i < num_envars; ++i) {
		DOCA_LOG_INFO("\tEnvars %d - variable: %s, value: %s", i,
			doca_apsh_envar_info_get(envars_list[i], DOCA_APSH_ENVARS_VARIABLE),
			doca_apsh_envar_info_get(envars_list[i], DOCA_APSH_ENVARS_VALUE));
	}

	/* Cleanup */
	doca_apsh_envars_free(envars_list);
	doca_apsh_processes_free(processes);
	cleanup_doca_apsh(apsh_ctx, sys);
	return DOCA_SUCCESS;
}
