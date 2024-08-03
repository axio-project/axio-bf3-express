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

#include <doca_dev.h>
#include <doca_rdma.h>
#include <doca_mmap.h>
#include <doca_error.h>

#define MEM_RANGE_LEN				(4096)					/* DOCA mmap memory range length */
#define WORKQ_DEPTH				(10)					/* DOCA workQ depth */
#define INVENTORY_NUM_INITIAL_ELEMENTS		(16)					/* Number of DOCA inventory initial elements */
#define MAX_USER_ARG_SIZE			(256)					/* Maximum size of user input argument */
#define MAX_ARG_SIZE				(MAX_USER_ARG_SIZE + 1)			/* Maximum size of input argument */
#define DEFAULT_STRING				"Hi DOCA RDMA!"				/* Default string to use in our samples */
#define DEFAULT_LOCAL_CONNECTION_DESC_PATH	"/tmp/local_connection_desc_path.txt"	/* Default path to save the local connection information */
#define DEFAULT_REMOTE_CONNECTION_DESC_PATH	"/tmp/remote_connection_desc_path.txt"	/* Default path to save the remote connection information */
#define DEFAULT_MMAP_CONNECTION_DESC_PATH	"/tmp/mmap_connection_desc_path.txt"	/* Default path to read/save the remote mmap connection information */

struct rdma_resources {
	struct doca_dev *doca_device;		/* DOCA device */
	struct doca_workq *workq;		/* DOA workQ */
	struct doca_mmap *mmap;			/* DOCA memory map */
	struct doca_mmap *remote_mmap;		/* DOCA remote memory map */
	char *mmap_memrange;			/* DOCA remote memory map memory range */
	const void *mmap_details;		/* DOCA memory map details */
	size_t mmap_details_size;		/* DOCA memory map details size */
	struct doca_rdma *rdma;			/* DOCA RDMA instance */
	struct doca_ctx *rdma_ctx;		/* DOCA context to be used with DOCA RDMA */
	const void *rdma_conn_details;		/* DOCA RMDA connection details */
	size_t rdma_conn_details_size;		/* DOCA RMDA connection details size */
	char *sendfile_buf;          /* DOCA RMDA connection details */
        size_t sendfile_size;
	void *remote_rdma_conn_details;		/* DOCA RMDA remote connection details */
	size_t remote_rdma_conn_details_size;	/* DOCA RMDA remote connection details size */
	void *remote_mmap_details;		/* DOCA RMDA remote memory map details */
	size_t remote_mmap_details_size;	/* DOCA RMDA remote memory map details size */
};

struct rdma_config {
	char device_name[DOCA_DEVINFO_IBDEV_NAME_SIZE];	/* DOCA device name */
	char send_string[MAX_ARG_SIZE];			/* String to send */
	char read_string[MAX_ARG_SIZE];			/* String to read */
	char write_string[MAX_ARG_SIZE];		/* String to write */
	char local_connection_desc_path[MAX_ARG_SIZE];	/* Path to save the local connection information */
	char remote_connection_desc_path[MAX_ARG_SIZE];	/* Path to read the remote connection information */
	char mmap_connection_desc_path[MAX_ARG_SIZE];	/* Path to read/save the remote mmap connection information */
	bool is_gid_index_set;				/* Is the set_index parameter passed */
	uint32_t gid_index;				/* GID index for DOCA RDMA */
	char sendfile_path[MAX_ARG_SIZE];
};

/*
 * Allocate DOCA RDMA resources
 *
 * @cfg [in]: Configuration parameters
 * @mmap_permissions [in]: Access flags for DOCA mmap
 * @rdma_permissions [in]: Access permission flags for DOCA RDMA
 * @resources [in/out]: DOCA RDMA resources to allocate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allocate_rdma_resources(struct rdma_config *cfg, const uint32_t mmap_permissions,
					const uint32_t rdma_permissions, struct rdma_resources *resources);

/*
 * Destroy DOCA RDMA resources
 *
 * @resources [in]: DOCA RDMA resources to destroy
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_rdma_resources(struct rdma_resources *resources, struct rdma_config *cfg);

/*
 * Register the command line parameters for the sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_rdma_params(void);

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
