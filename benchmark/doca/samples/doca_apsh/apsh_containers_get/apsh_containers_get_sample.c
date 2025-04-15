/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <doca_apsh.h>
#include <doca_log.h>

#include "apsh_common.h"

DOCA_LOG_REGISTER(CONTAINERS_GET);

/*
 * Calls the DOCA APSH API function that matches this sample name and prints the result
 *
 * @dma_device_name [in]: IBDEV Name of the device to use for DMA
 * @pci_vuid [in]: VUID of the device exposed to the target system
 * @os_type [in]: Indicates the OS type of the target system
 * @pid [in]: PID of the target process
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t containers_get(const char *dma_device_name,
			    const char *pci_vuid,
			    enum doca_apsh_system_os os_type,
			    const char *mem_region,
			    const char *os_symbols)
{
	doca_error_t result;
	int i;
	struct doca_apsh_ctx *apsh_ctx;
	struct doca_apsh_system *sys;
	struct doca_apsh_process **processes;
	int num_containers, num_processes;
	struct doca_apsh_container **containers_list;

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

	result = doca_apsh_containers_get(sys, &containers_list, &num_containers);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read containers info from host");
		cleanup_doca_apsh(apsh_ctx, sys);
		return result;
	}

	DOCA_LOG_INFO("Successfully performed %s. Host system contains %d containers", __func__, num_containers);

	for (i = 0; i < num_containers; ++i) {
		const char *container_id = doca_apsh_container_info_get(containers_list[i], DOCA_APSH_CONTAINER_ID);

		DOCA_LOG_INFO("\tContainer %d  -  id: %s", i, container_id);

		result = doca_apsh_container_processes_get(containers_list[i], &processes, &num_processes);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to read containers info from host");
			doca_apsh_containers_free(containers_list);
			cleanup_doca_apsh(apsh_ctx, sys);
			return result;
		}
		DOCA_LOG_INFO("First 5 (or less) processes of container:");
		for (int j = 0; j < num_processes && j < 5; j++)
			DOCA_LOG_INFO("\t\tProcess %d  -  name: %s, pid: %u, pid_ns %u, mnt_ns %u, net_ns %u",
				      j,
				      doca_apsh_process_info_get(processes[j], DOCA_APSH_PROCESS_COMM),
				      doca_apsh_process_info_get(processes[j], DOCA_APSH_PROCESS_PID),
				      doca_apsh_process_info_get(processes[j], DOCA_APSH_PROCESS_LINUX_NS_PID),
				      doca_apsh_process_info_get(processes[j], DOCA_APSH_PROCESS_LINUX_NS_MNT),
				      doca_apsh_process_info_get(processes[j], DOCA_APSH_PROCESS_LINUX_NS_NET));
		doca_apsh_processes_free(processes);
	}

	/* Cleanup */
	doca_apsh_containers_free(containers_list);
	cleanup_doca_apsh(apsh_ctx, sys);
	return DOCA_SUCCESS;
}
