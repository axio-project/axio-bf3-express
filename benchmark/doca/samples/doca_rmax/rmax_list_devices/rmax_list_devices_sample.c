/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdlib.h>
#include <stdbool.h>
#include <netinet/in.h>

#include <doca_dev.h>
#include <doca_log.h>
#include <doca_rmax.h>

#include "rmax_common.h"

DOCA_LOG_REGISTER(RMAX_LIST_DEVICES);

/*
 * Run rmax_list_devices sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rmax_list_devices(void)
{
	doca_error_t result = DOCA_SUCCESS;
	uint32_t nb_devs, i;
	uint8_t addr[4];
	bool has_ptp = true;
	struct doca_devinfo **devinfo;
	char dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];

	result = doca_rmax_init();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize DOCA Rivermax: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_devinfo_create_list(&devinfo, &nb_devs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create devices info list: %s", doca_error_get_descr(result));
		return result;
	}

	for (i = 0; i < nb_devs; ++i) {
		/* get PCI address */
		result = doca_devinfo_get_pci_addr_str(devinfo[i], dev_pci_addr);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to get PCI address: %s", doca_error_get_descr(result));
			return result;
		}
		/* get IP address */
		result = doca_devinfo_get_ipv4_addr(devinfo[i], addr, 4);
		if (result != DOCA_SUCCESS) {
			memset(&addr, 0, sizeof(addr));
		} else {
			/* query PTP capability can be queried if IPv4 address is configured */
			result = doca_rmax_get_ptp_clock_supported(devinfo[i]);
			if (result != DOCA_SUCCESS) {
				if (result != DOCA_ERROR_NOT_SUPPORTED) {
					DOCA_LOG_ERR("Failed to get PTP clock capability: %s",
						     doca_error_get_descr(result));
					return result;
				}
				has_ptp = false;
			} else
				has_ptp = true;
		}

		DOCA_LOG_INFO("PCI address: %s, IP: %d.%d.%d.%d, PTP: %c",
			      dev_pci_addr,
			      addr[0],
			      addr[1],
			      addr[2],
			      addr[3],
			      (has_ptp) ? 'y' : 'n');
	}

	result = doca_devinfo_destroy_list(devinfo);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy devices info list: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_rmax_release();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy the DOCA Rivermax: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}
