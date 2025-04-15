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

DOCA_LOG_REGISTER(PROCESS_NETSCAN_GET);

/*
 * Calls the DOCA APSH API function that matches this sample name and prints the result
 *
 * @dma_device_name [in]: IBDEV Name of the device to use for DMA
 * @pci_vuid [in]: VUID of the device exposed to the target system
 * @os_type [in]: Indicates the OS type of the target system
 * @pid [in]: PID of the target process
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t process_netscan_get(const char *dma_device_name,
				 const char *pci_vuid,
				 enum doca_apsh_system_os os_type,
				 DOCA_APSH_PROCESS_PID_TYPE pid,
				 const char *mem_region,
				 const char *os_symbols)
{
	doca_error_t result;
	int i, nb_processes;
	struct doca_apsh_ctx *apsh_ctx;
	struct doca_apsh_system *sys;
	struct doca_apsh_process *proc, **processes;
	int num_connections;
	struct doca_apsh_netscan **connections;

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
			DOCA_LOG_ERR("DOCA APSH encountered an error: %s", doca_error_get_descr(result));
		cleanup_doca_apsh(apsh_ctx, sys);
		return result;
	}
	DOCA_LOG_INFO("Process with PID %u found", pid);
	DOCA_LOG_INFO("Proc(%d) name: %s", pid, doca_apsh_process_info_get(proc, DOCA_APSH_PROCESS_COMM));

	result = doca_apsh_process_netscan_get(proc, &connections, &num_connections);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read connections info from host");
		doca_apsh_processes_free(processes);
		cleanup_doca_apsh(apsh_ctx, sys);
		return result;
	}
	DOCA_LOG_INFO("Successfully performed %s. Host proc(%d) contains %d connections",
		      __func__,
		      pid,
		      num_connections);

	/* Print some attributes of the connections */
	DOCA_LOG_INFO("Connections for process %u:", pid);
	for (i = 0; i < num_connections; ++i) {
		DOCA_LOG_INFO(
			"\tConnection %d  -  Protocol: %s, local address: %s:%lu, local address: %s:%lu, state %s",
			i,
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_PROTOCOL),
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LOCAL_ADDR),
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LOCAL_PORT),
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_REMOTE_ADDR),
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_REMOTE_PORT),
			doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_STATE));
		if (os_type == DOCA_APSH_SYSTEM_LINUX) {
			DOCA_LOG_INFO("\tConnection %d  -  FD: %u, family: %s, type: %s, filter %s, namespace: %u",
				      i,
				      doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_FD),
				      doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_FAMILY),
				      doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TYPE),
				      doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_FILTER),
				      doca_apsh_netscan_info_get(connections[i],
								 DOCA_APSH_NETSCAN_LINUX_NET_NAMESPACE));
		}
		if (os_type == DOCA_APSH_SYSTEM_LINUX &&
		    !strcmp(doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_PROTOCOL), "TCP")) {
			DOCA_LOG_INFO(
				"\tConnection %d - TCP connection - Bytes sent: %lu, Bytes acked: %lu, Bytes received: %lu, Segs in: %u, Segs out: %u, Data segs in: %u, Data segs out: %u",
				i,
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_BYTES_SENT),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_BYTES_ACKED),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_BYTES_RECEIVED),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_SEGS_IN),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_SEGS_OUT),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_DATA_SEGS_IN),
				doca_apsh_netscan_info_get(connections[i], DOCA_APSH_NETSCAN_LINUX_TCP_DATA_SEGS_OUT));
		}
	}

	/* Cleanup */
	doca_apsh_netscan_free(connections);
	doca_apsh_processes_free(processes);
	cleanup_doca_apsh(apsh_ctx, sys);
	return DOCA_SUCCESS;
}
