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

#define SLEEP_IN_NANOS (10 * 1000)		/* Sample the task every 10 microseconds  */

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
static doca_error_t
compare_device_pci_addr(struct doca_devinfo *dev_info, const char *pci_addr)
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

doca_error_t
find_port_action_type_vnf(const struct ipsec_security_gw_config *app_cfg, int port_id, int *idx)
{
	struct doca_dev *dev;
	struct doca_devinfo *dev_info;
	doca_error_t result;
	static bool is_secured_set, is_unsecured_set;

	result = doca_dpdk_port_as_dev(port_id, &dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to find DOCA device associated with port ID %d: %s", port_id, doca_error_get_descr(result));
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
 * Callback for finishing create tasks
 *
 * @task [in]: task that has been finished
 * @task_user_data [in]: data set by the user for the task
 * @ctx_user_data [in]: data set by the user for ctx
 */
static void
create_task_completed_cb(struct doca_ipsec_task_sa_create *task, union doca_data task_user_data,
					      union doca_data ctx_user_data)
{
	DOCA_LOG_INFO("Task completed: task-%p, user_data=0x%lx, ctx_data=0x%lx", task, task_user_data.u64, ctx_user_data.u64);
}

/*
 * Callback for finishing destroy tasks
 *
 * @task [in]: task that has been finished
 * @task_user_data [in]: data set by the user for the task
 * @ctx_user_data [in]: data set by the user for ctx
 */
static void
destroy_task_completed_cb(struct doca_ipsec_task_sa_destroy *task, union doca_data task_user_data,
					      union doca_data ctx_user_data)
{
	DOCA_LOG_INFO("Task completed: task-%p, user_data=0x%lx, ctx_data=0x%lx", task, task_user_data.u64, ctx_user_data.u64);
}

doca_error_t
ipsec_security_gw_start_ipsec_ctxs(struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	if (app_cfg->objects.full_offload_ctx) {
		result = doca_ctx_start(doca_ipsec_as_ctx(app_cfg->objects.full_offload_ctx));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to start lib context: %s", doca_error_get_descr(result));
			doca_pe_destroy(app_cfg->objects.doca_pe);
			return result;
		}
	}
	if (app_cfg->objects.crypto_offload_ctx) {
		result = doca_ctx_start(doca_ipsec_as_ctx(app_cfg->objects.crypto_offload_ctx));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to start lib context: %s", doca_error_get_descr(result));
			doca_pe_destroy(app_cfg->objects.doca_pe);
			return result;
		}
	}

	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_ipsec_ctx_create(struct ipsec_security_gw_config *app_cfg, enum doca_ipsec_sa_offload offload)
{
	doca_error_t result;
	struct doca_ipsec *doca_ipsec_ctx;

	result = doca_ipsec_create(app_cfg->objects.secured_dev.doca_dev, &doca_ipsec_ctx);

	if (offload == DOCA_IPSEC_SA_OFFLOAD_FULL)
		app_cfg->objects.full_offload_ctx = doca_ipsec_ctx;
	else
		app_cfg->objects.crypto_offload_ctx = doca_ipsec_ctx;

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create IPSEC context: %s", doca_error_get_descr(result));
		return result;
	}

	if (doca_ipsec_set_sa_pool_size(doca_ipsec_ctx, 4096) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable set ipsec pool size");
		return false;
	}

	result = doca_ipsec_set_offload_type(doca_ipsec_ctx, offload);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set offload type: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_ipsec_task_sa_create_set_conf(doca_ipsec_ctx, create_task_completed_cb, create_task_completed_cb, 1);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set conf for sa create: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_ipsec_task_sa_destroy_set_conf(doca_ipsec_ctx, destroy_task_completed_cb, destroy_task_completed_cb, 1);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set conf for sa destroy: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_pe_connect_ctx(app_cfg->objects.doca_pe, doca_ipsec_as_ctx(doca_ipsec_ctx));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register pe queue with context: %s", doca_error_get_descr(result));
		doca_pe_destroy(app_cfg->objects.doca_pe);
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy DOCA pe and stop doca context
 *
 * @ipsec_ctx [in]: ipsec context
 * @pe [in]: doca pe
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
ipsec_security_gw_ipsec_ctx_destroy(struct doca_ipsec *ipsec_ctx, struct doca_pe *pe)
{
	doca_error_t tmp_result, result = DOCA_SUCCESS;

	tmp_result = request_stop_ctx(pe, doca_ipsec_as_ctx(ipsec_ctx));
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to stop context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_ctx_stop(doca_ipsec_as_ctx(ipsec_ctx));
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to stop context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_ipsec_destroy(ipsec_ctx);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy IPSec library context: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	return result;
}

doca_error_t
ipsec_security_gw_ipsec_destroy(const struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	if (app_cfg->objects.full_offload_ctx)
		result = ipsec_security_gw_ipsec_ctx_destroy(app_cfg->objects.full_offload_ctx, app_cfg->objects.doca_pe);

	if (app_cfg->objects.crypto_offload_ctx)
		result = ipsec_security_gw_ipsec_ctx_destroy(app_cfg->objects.crypto_offload_ctx, app_cfg->objects.doca_pe);

	result = doca_pe_destroy(app_cfg->objects.doca_pe);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy pe queue: %s", doca_error_get_descr(result));

	result = doca_dev_close(app_cfg->objects.secured_dev.doca_dev);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy secured DOCA dev: %s", doca_error_get_descr(result));

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = doca_dev_close(app_cfg->objects.unsecured_dev.doca_dev);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy unsecured DOCA dev: %s", doca_error_get_descr(result));
	}
	return result;
}

doca_error_t
ipsec_security_gw_create_ipsec_sa(struct ipsec_security_gw_sa_attrs *app_sa_attrs, struct ipsec_security_gw_config *cfg,
	struct doca_ipsec_sa **sa)
{
	struct doca_ipsec_sa_attrs sa_attrs;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;
	struct doca_pe *pe = cfg->objects.doca_pe;
	struct doca_ipsec *doca_ipsec_ctx = cfg->objects.crypto_offload_ctx;
	struct doca_ipsec_task_sa_create *task;
	union doca_data user_data = {};

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
		doca_ipsec_ctx = cfg->objects.full_offload_ctx;
	} else if (app_sa_attrs->direction == DOCA_IPSEC_DIRECTION_EGRESS_ENCRYPT && !cfg->sw_sn_inc_enable) {
		sa_attrs.egress.sn_inc_enable = 1;
		doca_ipsec_ctx = cfg->objects.full_offload_ctx;
	}

	result = doca_ipsec_task_sa_create_allocate_init(doca_ipsec_ctx, &sa_attrs, user_data, &task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ipsec task: %s", doca_error_get_descr(result));
		return result;
	}

	/* Enqueue IPsec task */
	result = doca_task_submit(doca_ipsec_task_sa_create_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit ipsec task: %s", doca_error_get_descr(result));
		return result;
	}

	/* Wait for task completion */
	while (!doca_pe_progress(pe))
		nanosleep(&ts, &ts);

	result = doca_task_get_status(doca_ipsec_task_sa_create_as_task(task));
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve task: %s", doca_error_get_descr(result));

	/* if task succeed event.result.ptr will point to the new created sa object */
	*sa = (struct doca_ipsec_sa *)doca_ipsec_task_sa_create_get_sa(task);
	doca_task_free(doca_ipsec_task_sa_create_as_task(task));
	return result;
}

doca_error_t
ipsec_security_gw_destroy_ipsec_sa(struct ipsec_security_gw_config *app_cfg, struct doca_ipsec_sa *sa, bool is_full_offload)
{
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	struct doca_pe *pe = app_cfg->objects.doca_pe;
	struct doca_ipsec *doca_ipsec_ctx;
	struct doca_ipsec_task_sa_destroy *task;
	union doca_data user_data = {};
	doca_error_t result;

	if (is_full_offload)
		doca_ipsec_ctx = app_cfg->objects.full_offload_ctx;
	else
		doca_ipsec_ctx = app_cfg->objects.crypto_offload_ctx;

	result = doca_ipsec_task_sa_destroy_allocate_init(doca_ipsec_ctx, sa, user_data, &task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ipsec task: %s", doca_error_get_descr(result));
		return result;
	}

	/* Enqueue IPsec task */
	result = doca_task_submit(doca_ipsec_task_sa_destroy_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit ipsec task: %s", doca_error_get_descr(result));
		return result;
	}

	/* Wait for task completion */
	while (!doca_pe_progress(pe))
		nanosleep(&ts, &ts);

	if (doca_task_get_status(doca_ipsec_task_sa_destroy_as_task(task)) != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve task: %s", doca_error_get_descr(result));
	doca_task_free(doca_ipsec_task_sa_destroy_as_task(task));
	return result;
}

/**
 * Check if given device is capable of executing a doca_ipsec_task_sa_create task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports doca_ipsec_task_sa_create and DOCA_ERROR otherwise.
 */
static doca_error_t
task_ipsec_create_is_supported(struct doca_devinfo *devinfo)
{
	doca_error_t result;

	result = doca_ipsec_cap_task_sa_create_is_supported(devinfo);
	if (result != DOCA_SUCCESS)
		return result;
	result = doca_ipsec_cap_task_sa_destroy_is_supported(devinfo);
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
 * @func [in]: pointer to a function that checks if the device have some task capabilities
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
open_doca_device(struct ipsec_security_gw_dev_info *info, tasks_check func)
{
	doca_error_t result;

	if (info->open_by_pci) {
		result = open_doca_device_with_pci(info->pci_addr, func, &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			return result;
		}
	} else {
		result = open_doca_device_with_iface_name((uint8_t *)info->iface_name, strlen(info->iface_name), func, &info->doca_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
			return result;
		}
	}
	return DOCA_SUCCESS;
}

doca_error_t
ipsec_security_gw_init_devices(struct ipsec_security_gw_config *app_cfg)
{
	doca_error_t result;

	result = open_doca_device(&app_cfg->objects.secured_dev, &task_ipsec_create_is_supported);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device for the secured port: %s", doca_error_get_descr(result));
		return result;
	}

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF) {
		result = open_doca_device(&app_cfg->objects.unsecured_dev, NULL);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device for the unsecured port: %s", doca_error_get_descr(result));
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
		result = doca_dpdk_port_probe(app_cfg->objects.secured_dev.doca_dev, "dv_flow_en=2,dv_xmeta_en=4,fdb_def_rule_en=0,representor=pf[0-1]");
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to probe dpdk port for secured port: %s", doca_error_get_descr(result));
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

	if (app_cfg->app_rules.dummy_encrypt_sa != NULL) {
		result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, app_cfg->app_rules.dummy_encrypt_sa, !app_cfg->sw_sn_inc_enable);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy dummy encrypt SA");
	}

	if (app_cfg->app_rules.dummy_decrypt_sa != NULL) {
		result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, app_cfg->app_rules.dummy_decrypt_sa, !app_cfg->sw_antireplay);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy dummy decrypt SA");
	}

	for (i = 0; i < app_cfg->app_rules.nb_encrypted_rules; i++) {
		sa = app_cfg->app_rules.encrypt_rules[i].sa;
		if (sa != NULL) {
			result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, sa, !app_cfg->sw_sn_inc_enable);
			if (result != DOCA_SUCCESS)
				DOCA_LOG_ERR("Failed to destroy the SA for encrypt rule with index [%d]", i);
		}
	}

	for (i = 0; i < app_cfg->app_rules.nb_decrypted_rules; i++) {
		sa = app_cfg->app_rules.decrypt_rules[i].sa;
		if (sa != NULL) {
			result = ipsec_security_gw_destroy_ipsec_sa(app_cfg, sa, !app_cfg->sw_antireplay);
			if (result != DOCA_SUCCESS)
				DOCA_LOG_ERR("Failed to destroy the SA for decrypt rule with index [%d]", i);
		}
	}
}
