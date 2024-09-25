/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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
#include <time.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_dpdk.h>
#include <doca_ctx.h>
#include <doca_pe.h>

#include <samples/common.h>

#include "ipsec_ctx.h"
#include "flow_common.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW::ipsec_ctx);

#define SLEEP_IN_NANOS (10 * 1000) /* Sample the task every 10 microseconds  */

doca_error_t find_port_action_type_switch(int port_id, int *idx)
{
	int ret;
	uint16_t proxy_port_id;

	/* get the port ID which has the privilege to control the switch ("proxy port") */
	ret = rte_flow_pick_transfer_proxy(port_id, &proxy_port_id, NULL);
	if (ret < 0) {
		DOCA_LOG_ERR("Failed getting proxy port: %s", strerror(-ret));
		return DOCA_ERROR_DRIVER;
	}

	if (proxy_port_id == port_id)
		*idx = SECURED_IDX;
	else
		*idx = UNSECURED_IDX;

	return DOCA_SUCCESS;
}

/*
 * Compare between the input interface name and the device name
 *
 * @dev_info [in]: device info
 * @iface_name [in]: input interface name
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t compare_device_name(struct doca_devinfo *dev_info, const char *iface_name)
{
	char buf[DOCA_DEVINFO_IFACE_NAME_SIZE] = {};
	char val_copy[DOCA_DEVINFO_IFACE_NAME_SIZE] = {};
	doca_error_t result;

	if (strlen(iface_name) >= DOCA_DEVINFO_IFACE_NAME_SIZE)
		return DOCA_ERROR_INVALID_VALUE;

	memcpy(val_copy, iface_name, strlen(iface_name));

	result = doca_devinfo_get_iface_name(dev_info, buf, DOCA_DEVINFO_IFACE_NAME_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get device name: %s", doca_error_get_descr(result));
		return result;
	}

	if (memcmp(buf, val_copy, DOCA_DEVINFO_IFACE_NAME_SIZE) == 0)
		return DOCA_SUCCESS;

	return DOCA_ERROR_INVALID_VALUE;
}

/*
 * Compare between the input PCI address and the device address
 *
 * @dev_info [in]: device info
 * @pci_addr [in]: PCI address
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t compare_device_pci_addr(struct doca_devinfo *dev_info, const char *pci_addr)
{
	uint8_t is_addr_equal = 0;
	doca_error_t result;

	result = doca_devinfo_is_equal_pci_addr(dev_info, pci_addr, &is_addr_equal);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to compare device PCI address: %s", doca_error_get_descr(result));
		return result;
	}

	if (is_addr_equal)
		return DOCA_SUCCESS;

	return DOCA_ERROR_INVALID_VALUE;
}

doca_error_t find_port_action_type_vnf(const struct ipsec_security_gw_config *app_cfg,
				       int port_id,
				       struct doca_dev **connected_dev,
				       int *idx)
{
	struct doca_devinfo *dev_info;
	doca_error_t result;
	static bool is_secured_set, is_unsecured_set;

	result = doca_dpdk_port_as_dev(port_id, connected_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find DOCA device associated with port ID %d: %s",
			     port_id,
			     doca_error_get_descr(result));
		return result;
	}

	dev_info = doca_dev_as_devinfo(*connected_dev);
	if (dev_info == NULL) {
		DOCA_LOG_ERR("Failed to find DOCA device associated with port ID %d", port_id);
		return DOCA_ERROR_INITIALIZATION;
	}

	if (!is_secured_set && app_cfg->objects.secured_dev.open_by_pci) {
		if (compare_device_pci_addr(dev_info, app_cfg->objects.secured_dev.pci_addr) == DOCA_SUCCESS) {
			*idx = SECURED_IDX;
			is_secured_set = true;
			return DOCA_SUCCESS;
		}
	} else if (!is_secured_set && app_cfg->objects.secured_dev.open_by_name) {
		if (compare_device_name(dev_info, app_cfg->objects.secured_dev.iface_name) == DOCA_SUCCESS) {
			*idx = SECURED_IDX;
			is_secured_set = true;
			return DOCA_SUCCESS;
		}
	}
	if (!is_unsecured_set && app_cfg->objects.unsecured_dev.open_by_pci) {
		if (compare_device_pci_addr(dev_info, app_cfg->objects.unsecured_dev.pci_addr) == DOCA_SUCCESS) {
			*idx = UNSECURED_IDX;
			is_unsecured_set = true;
			return DOCA_SUCCESS;
		}
	} else if (!is_unsecured_set && app_cfg->objects.unsecured_dev.open_by_name) {
		if (compare_device_name(dev_info, app_cfg->objects.unsecured_dev.iface_name) == DOCA_SUCCESS) {
			*idx = UNSECURED_IDX;
			is_unsecured_set = true;
			return DOCA_SUCCESS;
		}
	}

	return DOCA_ERROR_INVALID_VALUE;
}

doca_error_t ipsec_security_gw_close_devices(const struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result = DOCA_SUCCESS;
	doca_error_t tmp_result;

	tmp_result = doca_dev_close(app_cfg->objects.secured_dev.doca_dev);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy secured DOCA dev: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		tmp_result = doca_dev_close(app_cfg->objects.unsecured_dev.doca_dev);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy unsecured DOCA dev: %s", doca_error_get_descr(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}
	return result;
}

/*
 * Open DOCA device by interface name or PCI address based on the application input
 *
 * @info [in]: ipsec_security_gw_dev_info struct
 * @func [in]: pointer to a function that checks if the device have some task capabilities
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t open_doca_device(struct ipsec_security_gw_dev_info *info, tasks_check func)
{
	doca_error_t result;

	if (info->open_by_pci) {
		result = open_doca_device_with_pci(info->pci_addr, func, &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			return result;
		}
	} else {
		result = open_doca_device_with_iface_name((uint8_t *)info->iface_name,
							  strlen(info->iface_name),
							  func,
							  &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			return result;
		}
	}
	return DOCA_SUCCESS;
}

doca_error_t ipsec_security_gw_init_devices(struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	result = open_doca_device(&app_cfg->objects.secured_dev, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device for the secured port: %s", doca_error_get_descr(result));
		return result;
	}
	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = open_doca_device(&app_cfg->objects.unsecured_dev, NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device for the unsecured port: %s",
				     doca_error_get_descr(result));
			return result;
		}
		/* probe the opened doca devices with 'dv_flow_en=2' for HWS mode */
		result = doca_dpdk_port_probe(app_cfg->objects.secured_dev.doca_dev, "dv_flow_en=2,dv_xmeta_en=4");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_error_get_descr(result));
			return result;
		}

		result = doca_dpdk_port_probe(app_cfg->objects.unsecured_dev.doca_dev, "dv_flow_en=2,dv_xmeta_en=4");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for unsecured port: %s", doca_error_get_descr(result));
			return result;
		}
	} else {
		result = doca_dpdk_port_probe(
			app_cfg->objects.secured_dev.doca_dev,
			"dv_flow_en=2,dv_xmeta_en=4,fdb_def_rule_en=0,vport_match=1,repr_matching_en=0,representor=pf[0-1]");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_error_get_descr(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}
