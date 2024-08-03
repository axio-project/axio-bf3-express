/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef FILE_SCAN_CORE_H_
#define FILE_SCAN_CORE_H_

#include <stdbool.h>
#include <stdio.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_regex.h>
#include <doca_regex_mempool.h>

#define MAX_FILE_NAME (255)			/* Maximum length of a file name */
#define JOB_RESPONSE_SIZE (32)			/* Size of the job response buffer */
#define DEFAULT_MEMPOOL_SIZE (1024)		/* Default size of the memory pool */
#define MAX_MATCHES_PER_JOB (254)		/* Maximum number of matches per job */
#define BF2_REGEX_JOB_LIMIT (1024 * 16)		/* 16KB */
#define REGEX_QP_INDEX (0)			/* This application uses 1 QP with index = 0 */

struct file_scan_config {
	char rules_file_path[MAX_FILE_NAME];		/* Path to RegEx rules file */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* RegEx PCI address to use */
	char *data_buffer;				/* Data buffer */
	size_t data_buffer_len;				/* Data buffer length */
	char *rules_buffer;				/* RegEx rules buffer */
	size_t rules_buffer_len;			/* RegEx rules buffer length */
	uint16_t nb_overlap_bytes;			/* Overlap bytes for huge jobs */
	uint32_t chunk_size;				/* Input chunk size (0 == all) */
	uint32_t total_matches;				/* Holds the total number of matches */
	uint32_t mempool_size;				/* RegEx memory pool size */
	uint32_t nb_jobs;				/* Number of RegEx jobs */
	uint32_t job_id_next;				/* Counter to provide job id values */
	FILE *csv_fp;					/* CSV output file pointer */
	struct doca_buf_inventory *buf_inventory;	/* DOCA Buffer Inventory to hold DOCA buffers */
	struct doca_dev *dev;				/* DOCA Device instance for RegEx */
	struct doca_mmap *mmap;				/* DOCA MMAP to hold DOCA Inventory */
	struct doca_regex *doca_regex;			/* DOCA RegEx instance */
	struct doca_workq *workq;			/* DOCA work queue */
	struct doca_regex_mempool *metadata_pool;	/* Pool of meta-data objects */
};

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_file_scan_params(void);

/*
 * Initialize the application configuration
 *
 * @app_cfg [in]: Application configuration
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t file_scan_init(struct file_scan_config *app_cfg);

/*
 * The main file scan function, scans the file and reports matches
 * if chunk_size is 0, the whole file is scanned as a single chunk
 * otherwise, the file is scanned in chunks of chunk_size
 *
 * @app_cfg [in]: Application configuration
 * @return: 0 on success and negative value otherwise
 */
int file_scan_run(struct file_scan_config *app_cfg);

/*
 * Cleanup the file scan application resources
 *
 * @app_cfg [in]: Application configuration
 */
void file_scan_cleanup(struct file_scan_config *app_cfg);

#endif /* FILE_SCAN_CORE_H_ */
