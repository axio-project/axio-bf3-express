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

#ifndef DNS_FILTER_CORE_H_
#define DNS_FILTER_CORE_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifdef GPU_SUPPORT
#include <cuda_runtime.h>
#endif

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_flow.h>
#include <doca_regex.h>

#include <offload_rules.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FILE_NAME 255		/* Maximal length of file path */
#define MAX_REGEX_RESPONSE_SIZE 256	/* Maximal size of RegEx jobs response */
#define DNS_FILTER_MAX_FLOWS 1024	/* Maximal number of FLOWS in application pipes */

/* DNS listing types */
enum dns_type_listing {
	DNS_NON_LISTING = 0,	/* Non listing type */
	DNS_ALLOW_LISTING,	/* Allowlist type */
	DNS_DENY_LISTING,	/* Denylist type */
};

/* DNS configuration structure */
struct dns_filter_config {
	struct doca_flow_pipe **drop_pipes;		/* Holds ports drop pipes */
	enum dns_type_listing listing_type;		/* Holds dns listing type */
	struct application_dpdk_config *dpdk_cfg;	/* App DPDK configuration struct */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE];	/* RegEx PCI address to use */
	char rules_file_path[MAX_FILE_NAME];		/* Path to RegEx rules file */
	struct doca_dev *dev;				/* DOCA device */
	struct doca_regex *doca_reg;			/* DOCA RegEx interface */
};

struct power_mngt {
	uint32_t zero_rx_packet_count;	/* Number of consecutive times all ports received zero packets */
	uint32_t zero_rx_port_count;	/* Number of ports that got 0 packets in current loop iteration */
};

/* Context structure per DPDK thread */
struct dns_worker_ctx {
	int queue_id;								/* Queue ID */
	char **queries;								/* Holds DNS queries */
	struct dns_filter_config *app_cfg;					/* App config struct */
	struct doca_regex_search_result responses[MAX_REGEX_RESPONSE_SIZE];	/* DOCA RegEx jobs responses */
	struct doca_buf *buffers[MAX_REGEX_RESPONSE_SIZE];			/* Buffers in use for job batch */
	struct doca_buf_inventory *buf_inventory;				/* DOCA buffer inventory */
	struct doca_workq *workq;						/* DOCA work queue */
	struct dpdk_mempool_shadow *mempool_shadow;				/* Shadow of a DPDK memory pool */
	struct power_mngt rx_cnts;						/* struct power_mngt to monitor when zero packets received */
};

/*
 * Initialize the DNS filter application, includes init DOCA flow, pipes creation and init DOCA RegEx
 *
 * @app_cfg [in/out]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t dns_filter_init(struct dns_filter_config *app_cfg);

/*
 * DNS filter destroy
 *
 * @app_cfg [in]: application configuration structure
 */
void dns_filter_destroy(struct dns_filter_config *app_cfg);

/*
 * Triggering DNS filter DPDK threads, each thread proccess packets from single queue
 *
 * @app_cfg [in]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t dns_worker_lcores_run(struct dns_filter_config *app_cfg);

/*
 * Register the command line parameters for the DNS filter application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_dns_filter_params(void);

#ifdef GPU_SUPPORT
/*
 * Launching GPU packets processing by creating CUDA kernel to inspect the packets burst and extract the DNS queries
 *
 * @comm_list [in]: array of communication objects, holds the bursted packets context
 * @c_stream [in]: CUDA stream
 * @queries [out]: array of DNS queries
 */
void workload_launch_gpu_processing(struct rte_gpu_comm_list *comm_list, cudaStream_t c_stream, char **queries);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DNS_FILTER_CORE_H_ */
