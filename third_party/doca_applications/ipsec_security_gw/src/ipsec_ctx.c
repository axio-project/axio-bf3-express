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
#include <time.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_dpdk.h>
#include <doca_ctx.h>

#include <samples/common.h>

#include "ipsec_ctx.h"
#include "flow_common.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW::ipsec_ctx);

#define SLEEP_IN_NANOS (10 * 1000)		/* Sample the job every 10 microseconds  */

doca_error_t
find_port_action_type_switch(int port_id, int *idx)
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
static doca_error_t
compare_device_name(struct doca_devinfo *dev_info, const char *iface_name)
{
	char buf[DOCA_DEVINFO_IFACE_NAME_SIZE] = {};
	char val_copy[DOCA_DEVINFO_IFACE_NAME_SIZE] = {};
	doca_error_t result;

	if (strlen(iface_name) >= DOCA_DEVINFO_IFACE_NAME_SIZE)
		return DOCA_ERROR_INVALID_VALUE;

	memcpy(val_copy, iface_name, strlen(iface_name));

	result = doca_devinfo_get_iface_name(dev_info, buf, DOCA_DEVINFO_IFACE_NAME_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get device name: %s", doca_get_error_string(result));
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
static doca_error_t
compare_device_pci_addr(struct doca_devinfo *dev_info, const char *pci_addr)
{
	uint8_t is_addr_equal = 0;
	doca_error_t result;

	result = doca_devinfo_get_is_pci_addr_equal(dev_info, pci_addr, &is_addr_equal);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to compare device PCI address: %s", doca_get_error_string(result));
		return result;
	}

	if (is_addr_equal)
		return DOCA_SUCCESS;

	return DOCA_ERROR_INVALID_VALUE;
}

doca_error_t
find_port_action_type_vnf(const struct ipsec_security_gw_config *app_cfg, int port_id, int *idx)
{
	struct doca_dev *dev;
	struct doca_devinfo *dev_info;
	doca_error_t result;
	static bool is_secured_set, is_unsecured_set;

	result = doca_dpdk_port_as_dev(port_id, &dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find DOCA device associated with port ID %d: %s", port_id, doca_get_error_string(result));
		return result;
	}

	dev_info = doca_dev_as_devinfo(dev);
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

/*
 * Initialized DOCA workq with ipsec context
 *
 * @dev [in]: doca device to connect to context
 * @ctx [in]: ipsec context
 * @workq [out]: created workq
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
ipsec_security_gw_init_workq(struct doca_dev *dev, struct doca_ctx *ctx, struct doca_workq **workq)
{
	doca_error_t result;

	result = doca_ctx_dev_add(ctx, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register device with lib context: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_ctx_start(ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start lib context: %s", doca_get_error_string(result));
		doca_ctx_dev_rm(ctx, dev);
		return result;
	}

	result = doca_workq_create(1, workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create work queue: %s", doca_get_error_string(result));
		doca_ctx_stop(ctx);
		doca_ctx_dev_rm(ctx, dev);
		return result;
	}

	result = doca_ctx_workq_add(ctx, *workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register work queue with context: %s", doca_get_error_string(result));
		doca_workq_destroy(*workq);
		doca_ctx_stop(ctx);
		doca_ctx_dev_rm(ctx, dev);
		return result;
	}
	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_ipsec_ctx_create(struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	result = doca_ipsec_create(&app_cfg->objects.ipsec_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create IPSEC context: %s", doca_get_error_string(result));
		return result;
	}

	app_cfg->objects.doca_ctx = doca_ipsec_as_ctx(app_cfg->objects.ipsec_ctx);

	result = ipsec_security_gw_init_workq(app_cfg->objects.secured_dev.doca_dev, app_cfg->objects.doca_ctx, &app_cfg->objects.doca_workq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to initialize DOCA workq: %s", doca_get_error_string(result));
		doca_ipsec_destroy(app_cfg->objects.ipsec_ctx);
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy DOCA workq and stop doca context
 *
 * @dev [in]: doca device to connect to context
 * @ctx [in]: ipsec context
 * @workq [in]: doca workq
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
ipsec_security_gw_destroy_workq(struct doca_dev *dev, struct doca_ctx *ctx, struct doca_workq *workq)
{
	doca_error_t tmp_result, result = DOCA_SUCCESS;

	tmp_result = doca_ctx_workq_rm(ctx, workq);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to remove work queue from ctx: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_ctx_stop(ctx);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to stop context: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_workq_destroy(workq);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy work queue: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_ctx_dev_rm(ctx, dev);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to remove device from ctx: %s", doca_get_error_string(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

doca_error_t
ipsec_security_gw_ipsec_ctx_destroy(const struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	result = ipsec_security_gw_destroy_workq(app_cfg->objects.secured_dev.doca_dev, app_cfg->objects.doca_ctx, app_cfg->objects.doca_workq);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy context resources: %s", doca_get_error_string(result));

	result = doca_ipsec_destroy(app_cfg->objects.ipsec_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy IPSec library context: %s", doca_get_error_string(result));

	result = doca_dev_close(app_cfg->objects.secured_dev.doca_dev);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy secured DOCA dev: %s", doca_get_error_string(result));

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = doca_dev_close(app_cfg->objects.unsecured_dev.doca_dev);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy unsecured DOCA dev: %s", doca_get_error_string(result));
	}
	return result;
}

doca_error_t
ipsec_security_gw_create_ipsec_sa(struct ipsec_security_gw_sa_attrs *app_sa_attrs, struct ipsec_security_gw_config *cfg,
	struct doca_ipsec_sa **sa)
{
	struct doca_ipsec_sa_attrs sa_attrs;
	struct doca_event event = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;
	struct doca_workq *doca_workq = cfg->objects.doca_workq;
	struct doca_ctx *doca_ctx = cfg->objects.doca_ctx;

	memset(&sa_attrs, 0, sizeof(sa_attrs));

	sa_attrs.icv_length = app_sa_attrs->icv_length;
	sa_attrs.key.type = app_sa_attrs->key_type;
	sa_attrs.key.aes_gcm.implicit_iv = 0;
	sa_attrs.key.aes_gcm.salt = app_sa_attrs->salt;
	sa_attrs.key.aes_gcm.raw_key = (void *)&app_sa_attrs->enc_key_data;
	sa_attrs.direction = app_sa_attrs->direction;
	sa_attrs.sn_attr.sn_initial = cfg->sn_initial;
	if (app_sa_attrs->direction == DOCA_IPSEC_DIRECTION_INGRESS_DECRYPT && !cfg->sw_antireplay) {
		sa_attrs.ingress.antireplay_enable = 1;
		sa_attrs.ingress.replay_win_sz = DOCA_IPSEC_REPLAY_WIN_SIZE_128;
	} else if (app_sa_attrs->direction == DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT && !cfg->sw_sn_inc_enable)
		sa_attrs.egress.sn_inc_enable = 1;

	const struct doca_ipsec_sa_create_job sa_create = {
		.base = (struct doca_job) {
			.type = DOCA_IPSEC_JOB_SA_CREATE,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = doca_ctx,
			.user_data.u64 = DOCA_IPSEC_JOB_SA_CREATE,
		},
		.sa_attrs = sa_attrs,
	};

	/* Enqueue IPsec job */
	result = doca_workq_submit(doca_workq, &sa_create.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit ipsec job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(doca_workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve job: %s", doca_get_error_string(result));

	/* if job succeed event.result.ptr will point to the new created sa object */
	*sa = event.result.ptr;
	return result;
}

doca_error_t
ipsec_security_gw_destroy_ipsec_sa(struct ipsec_security_gw_config *app_cfg, struct doca_ipsec_sa *sa)
{
	struct doca_event event = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	const struct doca_ipsec_sa_destroy_job sa_destroy = {
		.base = (struct doca_job) {
			.type = DOCA_IPSEC_JOB_SA_DESTROY,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = app_cfg->objects.doca_ctx,
		},
		.sa = sa,
	};

	/* Enqueue IPsec job */
	result = doca_workq_submit(app_cfg->objects.doca_workq, &sa_destroy.base);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit ipsec job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(app_cfg->objects.doca_workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve job: %s", doca_get_error_string(result));

	return result;
}

/**
 * Check if given device is capable of executing a DOCA_IPSEC_JOB_SA_CREATE job.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_IPSEC_JOB_SA_CREATE and DOCA_ERROR otherwise.
 */
static doca_error_t
job_ipsec_create_is_supported(struct doca_devinfo *devinfo)
{
	doca_error_t result;

	result = doca_ipsec_job_get_supported(devinfo, DOCA_IPSEC_JOB_SA_CREATE);
	if (result != DOCA_SUCCESS)
		return result;
	result = doca_ipsec_sequence_number_get_supported(devinfo);
	if (result != DOCA_SUCCESS)
		return result;
	return doca_ipsec_antireplay_get_supported(devinfo);
}

/*
 * Open DOCA device by interface name or PCI address based on the application input
 *
 * @info [in]: ipsec_security_gw_dev_info struct
 * @func [in]: pointer to a function that checks if the device have some job capabilities
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
open_doca_device(struct ipsec_security_gw_dev_info *info, jobs_check func)
{
	doca_error_t result;

	if (info->open_by_pci) {
		result = open_doca_device_with_pci(info->pci_addr, func, &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_get_error_string(result));
			return result;
		}
	} else {
		result = open_doca_device_with_iface_name((uint8_t *)info->iface_name, strlen(info->iface_name), func, &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_get_error_string(result));
			return result;
		}
	}
	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_init_devices(struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	result = open_doca_device(&app_cfg->objects.secured_dev, &job_ipsec_create_is_supported);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device for the secured port: %s", doca_get_error_string(result));
		return result;
	}

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = open_doca_device(&app_cfg->objects.unsecured_dev, NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device for the unsecured port: %s", doca_get_error_string(result));
			return result;
		}
		/* probe the opened doca devices with 'dv_flow_en=2' for HWS mode */
		result = doca_dpdk_port_probe(app_cfg->objects.secured_dev.doca_dev, "dv_flow_en=2");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_get_error_string(result));
			return result;
		}

		result = doca_dpdk_port_probe(app_cfg->objects.unsecured_dev.doca_dev, "dv_flow_en=2");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for unsecured port: %s", doca_get_error_string(result));
			return result;
		}
	} else {
		result = doca_dpdk_port_probe(app_cfg->objects.secured_dev.doca_dev, "dv_flow_en=2,dv_xmeta_en=4,fdb_def_rule_en=0,representor=pf[0-1]");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_get_error_string(result));
			return result;
		}
	}

	return DOCA_SUCCESS;
}

void
ipsec_security_gw_destroy_sas(struct ipsec_security_gw_config *app_cfg)
{
	int i;
	doca_error_t result;
	struct doca_ipsec_sa *sa;

	for (i = 0; i < app_cfg->app_rules.nb_encrypted_rules; i++) {
		sa = app_cfg->app_rules.encrypt_rules[i].sa;
		result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, sa);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy the SA for encrypt rule with index [%d]", i);
	}

	for (i = 0; i < app_cfg->app_rules.nb_decrypted_rules; i++) {
		sa = app_cfg->app_rules.decrypt_rules[i].sa;
		result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, sa);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy the SA for decrypt rule with index [%d]", i);
	}
}
