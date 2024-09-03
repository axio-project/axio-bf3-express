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

#ifndef RDMA_COMMON_H_
#define RDMA_COMMON_H_

#include <time.h>

#include <doca_dev.h>
#include <doca_rdma.h>
#include <doca_mmap.h>
#include <doca_error.h>
#include <doca_pe.h>
#include <doca_sync_event.h>

#include "common.h"


#define MEM_RANGE_LEN					(4096)					/* DOCA mmap memory range length */
#define INVENTORY_NUM_INITIAL_ELEMENTS			(16)					/* Number of DOCA inventory initial elements */
#define MAX_USER_ARG_SIZE				(256)					/* Maximum size of user input argument */
#define MAX_ARG_SIZE					(MAX_USER_ARG_SIZE + 1)			/* Maximum size of input argument */
#define DEFAULT_STRING					"Hi DOCA RDMA!"				/* Default string to use in our samples */
#define DEFAULT_LOCAL_CONNECTION_DESC_PATH		"/tmp/local_connection_desc_path.txt"	/* Default path to save the local connection descriptor that should be passed to the other side */
#define DEFAULT_REMOTE_CONNECTION_DESC_PATH		"/tmp/remote_connection_desc_path.txt"	/* Default path to save the remote connection descriptor that should be passed from the other side */
#define DEFAULT_REMOTE_RESOURCE_CONNECTION_DESC_PATH	"/tmp/remote_resource_desc_path.txt"	/* Default path to read/save the remote mmap connection descriptor that should be passed to the other side */
#define NUM_RDMA_TASKS					(1)					/* Number of RDMA tasks*/
#define SLEEP_IN_NANOS					(10 * 1000)				/* Sample the task every 10 microseconds  */

/* Function to check if a given device is capable of executing some task */
typedef doca_error_t (*task_check)(const struct doca_devinfo *);

struct rdma_config {
	char device_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];	/* DOCA device name */
	char send_string[MAX_ARG_SIZE];			/* String to send */
	char read_string[MAX_ARG_SIZE];			/* String to read */
	char write_string[MAX_ARG_SIZE];		/* String to write */
	char local_connection_desc_path[MAX_ARG_SIZE];	/* Path to save the local connection information */
	char remote_connection_desc_path[MAX_ARG_SIZE];	/* Path to read the remote connection information */
	char remote_resource_desc_path[MAX_ARG_SIZE];	/* Path to read/save the remote mmap connection information */
	bool is_gid_index_set;				/* Is the set_index parameter passed */
	uint32_t gid_index;				/* GID index for DOCA RDMA */

};

struct rdma_resources {
	struct rdma_config *cfg;			/* RDMA samples configuration parameters */
	struct doca_dev *doca_device;			/* DOCA device */
	struct doca_pe *pe;				/* DOCA progress engine */
	struct doca_mmap *mmap;				/* DOCA memory map */
	struct doca_mmap *remote_mmap;			/* DOCA remote memory map */
	struct doca_sync_event *sync_event;		/* DOCA sync event */
	struct doca_sync_event_remote_net *remote_se;	/* DOCA remote sync event */
	char *mmap_memrange;				/* DOCA remote memory map memory range */
	struct doca_buf_inventory *buf_inventory;	/* DOCA buffer inventory */
	const void *mmap_descriptor;			/* DOCA memory map descriptor */
	size_t mmap_descriptor_size;			/* DOCA memory map descriptor size */
	struct doca_rdma *rdma;				/* DOCA RDMA instance */
	struct doca_ctx *rdma_ctx;			/* DOCA context to be used with DOCA RDMA */
	struct doca_buf *src_buf;			/* DOCA source buffer */
	struct doca_buf *dst_buf;			/* DOCA destination buffer */
	const void *rdma_conn_descriptor;		/* DOCA RMDA connection descriptor */
	size_t rdma_conn_descriptor_size;		/* DOCA RMDA connection descriptor size */
	void *remote_rdma_conn_descriptor;		/* DOCA RMDA remote connection descriptor */
	size_t remote_rdma_conn_descriptor_size;	/* DOCA RMDA remote connection descriptor size */
	void *remote_mmap_descriptor;			/* DOCA RMDA remote memory map descriptor */
	size_t remote_mmap_descriptor_size;		/* DOCA RMDA remote memory map descriptor size */
	void *sync_event_descriptor;			/* DOCA RMDA remote sync event descriptor */
	size_t sync_event_descriptor_size;		/* DOCA RMDA remote sync event descriptor size */
	doca_error_t task_result;			/* Result of the task we submit */
	bool run_main_loop;				/* Flag whether to keep running main progress loop */
	size_t num_remaining_tasks;			/* Number of remaining tasks to submit */
};

/*
 * Allocate DOCA RDMA resources
 *
 * @cfg [in]: Configuration parameters
 * @mmap_permissions [in]: Access flags for DOCA mmap
 * @rdma_permissions [in]: Access permission flags for DOCA RDMA
 * @func [in]: Function to check if a given device is capable of executing some task
 * @resources [in/out]: DOCA RDMA resources to allocate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allocate_rdma_resources(struct rdma_config *cfg, const uint32_t mmap_permissions,
				     const uint32_t rdma_permissions, task_check func,
				     struct rdma_resources *resources);

/*
 * Destroy DOCA RDMA resources
 *
 * @resources [in]: DOCA RDMA resources to destroy
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_rdma_resources(struct rdma_resources *resources, struct rdma_config *cfg);

/*
 * Register the common command line parameters for the sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_rdma_common_params(void);

/*
 * Register ARGP send string parameter
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_rdma_send_string_param(void);

/*
 * Register ARGP read string parameter
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_rdma_read_string_param(void);

/*
 * Register ARGP write string parameter
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_rdma_write_string_param(void);

/*
 * Write the string on a file
 *
 * @file_path [in]: The path of the file
 * @string [in]: The string to write
 * @string_len [in]: The length of the string
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t write_file(const char *file_path, const char *string, size_t string_len);

/*
 * Read a string from a file
 *
 * @file_path [in]: The path of the file we want to read
 * @string [out]: The string we read
 * @string_len [out]: The length of the string we read
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t read_file(const char *file_path, char **string, size_t *string_len);

#endif /* RDMA_COMMON_H_ */
