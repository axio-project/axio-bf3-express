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

#include <stdbool.h>
#include <rte_ethdev.h>
#include <rte_mbuf_core.h>

#include <doca_argp.h>
#include <doca_buf_inventory.h>
#include <doca_dev.h>
#include <doca_dpdk.h>
#include <doca_dpi.h>
#include <doca_error.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <offload_rules.h>

#include "dpi_common.h"

#include <common.h>

DOCA_LOG_REGISTER(DPI_SCAN::MAIN);

/* Sample's Logic */
doca_error_t dpi_scan(const char *sig_file, struct doca_dev *dev, struct doca_buf *pkt_doca_buf,
		      struct doca_dpi_parsing_info *parsing_info, uint32_t *payload_offset);

/*
 * The wrapper of whether doca_dpi is supported
 *
 * @devinfo [in]: The device info
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
dpi_job_is_supported(struct doca_devinfo *devinfo)
{
	return doca_dpi_job_get_supported(devinfo, DOCA_DPI_JOB);
}

/*
 * Create a DPDK bridge
 *
 * @buf_inventory [in]: The DOCA buf_inventory to be initialized
 * @doca_dpdk_pool [in]: The DPDK bridge pool to be initialized
 * @dev [in]: The DOCA dev to be initialized
 * @pci_addr [in]: The doca_dev's PCI address
 * @mbuf_pool [in]: The DPDK mbuf pool
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
dpdk_bridge_create(struct doca_buf_inventory **buf_inventory, struct doca_dpdk_mempool **doca_dpdk_pool,
		   struct doca_dev **dev, const char *pci_addr, struct rte_mempool *mbuf_pool)
{
	doca_error_t result;

	if (buf_inventory == NULL || doca_dpdk_pool == NULL || dev == NULL || pci_addr == NULL || mbuf_pool == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	/* Open doca_dev */
	result = open_doca_device_with_pci(pci_addr, &dpi_job_is_supported, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open device with pci address: %s", doca_get_error_string(result));
		return result;
	}

	/* DOCA buf_inventory create and start for DPDK bridge */
	result = doca_buf_inventory_create(NULL, NUM_MBUFS, DOCA_BUF_EXTENSION_NONE, buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Buf_inventory create failure, %s", doca_get_error_string(result));
		return result;
	}
	result = doca_buf_inventory_start(*buf_inventory);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Buf_inventory start failure, %s", doca_get_error_string(result));
		doca_buf_inventory_destroy(*buf_inventory);
		return result;
	}

	/*
	 * DOCA DPDK bridge create and start.
	 * So the rte_mbuf can be converted into doca_buf.
	 */
	result = doca_dpdk_mempool_create(mbuf_pool, doca_dpdk_pool);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DPDK bridge_mempool create failure, %s", doca_get_error_string(result));
		doca_buf_inventory_destroy(*buf_inventory);
		return result;
	}
	result = doca_dpdk_mempool_dev_add(*doca_dpdk_pool, *dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DOCA DPDK mempool dev add failure, %s", doca_get_error_string(result));
		doca_dpdk_mempool_destroy(*doca_dpdk_pool);
		doca_buf_inventory_destroy(*buf_inventory);
		return result;
	}
	result = doca_dpdk_mempool_start(*doca_dpdk_pool);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DOCA DPDK mempool start failure, %s", doca_get_error_string(result));
		doca_dpdk_mempool_destroy(*doca_dpdk_pool);
		doca_buf_inventory_destroy(*buf_inventory);
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Cleanup a DPDK bridge
 *
 * @buf_inventory [in]: The DOCA buf_inventory used in DPDK bridge
 * @doca_dpdk_pool [in]: The DPDK bridge pool used in DPDK bridge
 * @dev [in]: The DOCA dev used in DPDK bridge
 */
static void
dpdk_bridge_destroy(struct doca_buf_inventory *buf_inventory, struct doca_dpdk_mempool *doca_dpdk_pool,
		    struct doca_dev *dev)
{
	if (buf_inventory == NULL || doca_dpdk_pool == NULL || dev == NULL)
		return;

	doca_dpdk_mempool_destroy(doca_dpdk_pool);
	doca_buf_inventory_destroy(buf_inventory);
	doca_dev_close(dev);
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
	struct dpi_scan_config cfg;
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = 1,
		.port_config.nb_queues = 1,
		.reserve_main_thread = true,
	};
	bool packet_inspected = false;
	int packet_number;
	uint8_t port = 0;
	uint16_t nb_rx;
	uint16_t queue_id = 0;
	uint16_t burst_size = 8;
	uint32_t payload_offset = 0;
	struct rte_mbuf *packets[burst_size];
	struct doca_dpi_parsing_info parsing_info = {
		/* Hardcoded values */
		.ethertype = rte_cpu_to_be_16(0x0800),
		.l4_protocol = IPPROTO_UDP,
		.l4_dport = 776,
		.l4_sport = 775,
		.dst_ip.ipv4.s_addr = RTE_IPV4(127, 0, 0, 1),
	};
	struct doca_buf_inventory *buf_inventory = NULL;
	struct doca_dpdk_mempool *doca_dpdk_pool = NULL;
	struct doca_buf *pkt_doca_buf = NULL;
	struct doca_dev *dev = NULL;
	int exit_status = EXIT_FAILURE;

	/* Set the configuration default values */
	memset(&cfg, 0, sizeof(struct dpi_scan_config));

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting the sample");

	result = doca_argp_init("doca_dpi_scan", &cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		goto sample_exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = register_dpi_scan_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_get_error_string(result));
		goto argp_cleanup;
	}

	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update ports and queues: %s", doca_get_error_string(result));
		goto dpdk_cleanup;
	}

	result = dpdk_bridge_create(&buf_inventory, &doca_dpdk_pool, &dev, cfg.pci_address, dpdk_config.mbuf_pool);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DPDK bridge: %s", doca_get_error_string(result));
		goto dpdk_ports_queues_cleanup;
	}

	while (!packet_inspected) {
		nb_rx = rte_eth_rx_burst(port, queue_id, packets, burst_size);
		if (nb_rx != 0) {
			for (packet_number = 0; packet_number < nb_rx; packet_number++) {
				result = doca_dpdk_mempool_mbuf_to_buf(doca_dpdk_pool, buf_inventory,
								       packets[packet_number], &pkt_doca_buf);
				if (result != DOCA_SUCCESS) {
					DOCA_LOG_ERR("DPI DPDK bridge doca_buf allocation failed, %s",
							doca_get_error_string(result));
					goto dpdk_bridge_cleanup;
				}
				result = dpi_scan(cfg.sig_file_path, dev, pkt_doca_buf, &parsing_info,
						  &payload_offset);
				if (result != DOCA_SUCCESS) {
					DOCA_LOG_ERR("dpi_scan() encountered an error: %s",
											doca_get_error_string(result));
					doca_buf_refcount_rm(pkt_doca_buf, NULL);
					goto dpdk_bridge_cleanup;
				}
				doca_buf_refcount_rm(pkt_doca_buf, NULL);
				rte_pktmbuf_free(packets[packet_number]);
			}
			packet_inspected = true;
		}
	}

	exit_status = EXIT_SUCCESS;

dpdk_bridge_cleanup:
	dpdk_bridge_destroy(buf_inventory, doca_dpdk_pool, dev);
dpdk_ports_queues_cleanup:
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
	dpdk_fini();
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
