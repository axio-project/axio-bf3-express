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

#ifndef ETH_COMMON_H_
#define ETH_COMMON_H_

#include <unistd.h>

#include <doca_pe.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_buf_inventory.h>
#include <doca_error.h>

#include "common.h"

struct eth_core_resources {
	struct program_core_objects core_objs;		/* DOCA core objects */
	void *mmap_addr;				/* Address of DOCA memory map */
	uint32_t mmap_size;				/* DOCA memory map size */
};

struct eth_core_config {
	uint32_t mmap_size;				/* Size of the memory map */
	size_t inventory_num_elements	;		/* Elements number of the buffer inventory */
	tasks_check check_device;			/* Function to check device capability */
	const char *ibdev_name;				/* DOCA IB device name */
};

struct ether_hdr {
	uint8_t src_addr[DOCA_DEVINFO_MAC_ADDR_SIZE];	/* Destination addr bytes in tx order */
	uint8_t dst_addr[DOCA_DEVINFO_MAC_ADDR_SIZE];	/* Source addr bytes in tx order */
	uint16_t ether_type;				/* Frame type */
} __attribute__((__packed__));

/*
 * Allocate ETH core resources
 *
 * @cfg [in]: Configuration parameters
 * @resources [in/out]: ETH core resources to allocate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allocate_eth_core_resources(struct eth_core_config *cfg, struct eth_core_resources *resources);

/*
 * Destroy ETH core resources
 *
 * @resources [in]: ETH core resources to destroy
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_eth_core_resources(struct eth_core_resources *resources);

/*
 * Extract IB device name after checking it's a valid IB device name
 *
 * @ibdev_name [in]: IB device name to check and extract from
 * @ibdev_name_out [in/out]: buffer to extract/copy IB device name to
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t extract_ibdev_name(char *ibdev_name, char *ibdev_name_out);

/*
 * Extract MAC address after checking it's a valid MAC address
 *
 * @mac_addr [in]: MAC address to check and extract from
 * @mac_addr_out [in/out]: uint8_t array to extract MAC address to
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t extract_mac_addr(char *mac_addr, uint8_t *mac_addr_out);

#endif /* ETH_COMMON_H_ */
