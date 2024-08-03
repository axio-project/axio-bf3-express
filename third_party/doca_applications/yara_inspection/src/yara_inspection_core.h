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

#ifndef YARA_INSPECTION_CORE_H_
#define YARA_INSPECTION_CORE_H_

#include <doca_apsh.h>
#include <doca_apsh_attr.h>
#include <doca_dev.h>
#include <doca_telemetry.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The path is read from the host memory, from the OS process structs.
 * In the linux case the path is actually the process "comm" which max len is 16.
 * In Windows the path is actually the process "image_file_name" which unofficial sources are saying is 0x20 bytes long,
 * the official doc refer only to a full path to file and is saying the default MAX_PATH_LEN value is 260 (can be changed).
 */
#define MAX_PATH_LEN 260
#define MAX_PROCESS_NAME_LEN 1000

struct yara_config {
	DOCA_APSH_PROCESS_PID_TYPE pid;				/* Pid of process to validate integrity of */
	char exec_hash_map_path[MAX_PATH_LEN];			/* Path to APSH's hash.zip file */
	char system_mem_region_path[MAX_PATH_LEN];		/* Path to APSH's mem_regions.json file */
	char system_vuid[DOCA_DEVINFO_VUID_SIZE + 1];		/* Virtual Unique Identifier belonging to the PF/VF
								 * that is exposed to the target system.
								 */
	char dma_dev_name[DOCA_DEVINFO_IBDEV_NAME_SIZE + 1];	/* DMA device name */
	char system_os_symbol_map_path[MAX_PATH_LEN];		/* Path to APSH's os_symbols.json file */
	int time_interval;					/* Seconds to sleep between two integrity checks */
};

struct yara_resources {
	struct doca_apsh_ctx *ctx;			/* Lib Asph context */
	struct doca_apsh_system *sys;			/* Lib Apsh system context */
	struct doca_dev_rep *system_device;		/* DOCA PF/VF representor exposed to the target system */
	struct doca_dev *dma_device;			/* DOCA device capable of DMA into the target system,
							 * matches to the PF of the system device.
							 */
};

/* Event struct from which report will be serialized */
struct yara_event {
	doca_telemetry_timestamp_t	timestamp;				/* Timestamp of when the scan and the validation were completed */
	int32_t				pid;					/* Process id number that have been matched by yara rule */
	char				process_name[MAX_PROCESS_NAME_LEN + 1];	/* The name of that process  */
	char				yara_rule_name[MAX_PATH_LEN + 1];	/* The end result of the scan, 0 on uncompromising, error otherwise */
	uint64_t			vad;					/* This scan number, beginning with 0 */
} __attribute__((packed));

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_yara_params(void);

/*
 * Created and initialized all needed resources for the agent to run
 *
 * @conf [in]: Configureation values
 * @resources [out]: Memory location to store the created resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: In case of failure, all already allocated resource are freed
 */
doca_error_t yara_inspection_init(struct yara_config *conf, struct yara_resources *resources);

/*
 * Close and free the given resources, freed resources are set to NULL and unset/freed resources are expected to be NULL
 *
 * @resources [in]: Resources to cleanup
 */
void yara_inspection_cleanup(struct yara_resources *resources);

/*
 * Creates a new DOCA Telemetry schema and source, with a register yara event
 *
 * @telemetry_schema [out]: Memory location to store the created schema
 * @telemetry_source [out]: Memory location to store the created source
 * @index [out]: Memory location to store the yara event type index in the telemetry schema
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t telemetry_start(struct doca_telemetry_schema **telemetry_schema,
			     struct doca_telemetry_source **telemetry_source, doca_telemetry_type_index_t *index);

/*
 * Destroys the DOCA Telemetry schema and source
 *
 * @telemetry_schema [in]: Pointer to the DOCA Telemetry schema
 * @telemetry_source [in]: Pointer to the DOCA Telemetry source
 */
void telemetry_destroy(struct doca_telemetry_schema *telemetry_schema, struct doca_telemetry_source *telemetry_source);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YARA_INSPECTION_CORE_H_ */
