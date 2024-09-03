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
#ifndef APSH_COMMON_H_
#define APSH_COMMON_H_

#include <doca_apsh.h>
#include <doca_apsh_attr.h>
#include <doca_dev.h>

/* Configuration struct */
struct apsh_config {
	DOCA_APSH_PROCESS_PID_TYPE pid;				/* Process Identifier */
	char system_vuid[DOCA_DEVINFO_VUID_SIZE + 1];		/* Virtual Unique Identifier */
	char dma_dev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE + 1];	/* DMA device name */
	enum doca_apsh_system_os os_type;			/* System OS type - windows/linux */
};

/*
 * Register the command line parameters for the DOCA APSH samples.
 *
 * @add_os_arg [in]: true/false - need OS type from argv
 * @add_pid_arg [in]: true/false - need process PID from argv
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_apsh_params(bool add_os_arg, bool add_pid_arg);

/*
 * Creates and starts a DOCA Apsh context, in order to make the library usable.
 *
 * @dma_device_name [in]: String representing a DMA capable device
 * @ctx [out]: Memory storage for the context pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: On failure all lib Apsh resources are freed
 */
doca_error_t init_doca_apsh(const char *dma_device_name, struct doca_apsh_ctx **ctx);

/*
 * Creates and starts a DOCA Apsh System context, in order to apply the library on a specific host system.
 *
 * @ctx [in]: App shield initiated context
 * @os_type [in]: Target OS type
 * @os_symbols [in]: Path to the symbols JSON created by doca_apsh_config tool
 * @mem_region [in]: Path to the mem_regions JSON created by doca_apsh_config tool
 * @pci_vuid [in]: VUID of the VF/PF exposed to the target
 * @system [out]: Memory storage for the context pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: On failure all lib Apsh resources are freed
 */
doca_error_t init_doca_apsh_system(struct doca_apsh_ctx *ctx, enum doca_apsh_system_os os_type, const char *os_symbols,
				   const char *mem_region, const char *pci_vuid, struct doca_apsh_system **system);

/*
 * Destroys the system and context handler and free inner resources.
 *
 * @ctx [in]: App shield created context
 * @system [in]: App shield System created context (or NULL to ignore)
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t cleanup_doca_apsh(struct doca_apsh_ctx *ctx, struct doca_apsh_system *system);

/*
 * Searches for a process on the "sys" with a specific PID.
 *
 * @pid [in]: PID of a process to be searched for
 * @sys [in]: App shield System initiated context
 * @nb_procs [out]: Length of processes list
 * @processes [out]: List of all processes in the target system
 * @process [out]: Pointer to the specific process
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: If process pid is not found, the processes list is not returned
 */
doca_error_t process_get(DOCA_APSH_PROCESS_PID_TYPE pid, struct doca_apsh_system *sys, int *nb_procs,
			 struct doca_apsh_process ***processes, struct doca_apsh_process **process);

#endif /* APSH_COMMON_H_ */
