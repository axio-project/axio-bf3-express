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

#ifndef COMMON_COMMON_H
#define COMMON_COMMON_H

#include <stdbool.h>

#include <doca_ctx.h>
#include <doca_sync_event.h>

#define SYNC_EVENT_CC_MAX_MSG_SIZE 1024		   /* DOCA comm_channel maximum message size */
#define SYNC_EVENT_CC_MAX_QUEUE_SIZE 8		   /* DOCA comm_channel maximum queue size */
#define SYNC_EVENT_CC_SERVICE_NAME "sync_event_cc" /* DOCA comm_channel service name */
#define SYNC_EVENT_CC_TIMEOUT_SEC 30		   /* DOCA comm_channel timeout in seconds */

/* user input */
struct sync_event_config {
	char dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	   /* Device PCI address */
	char rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* DPU representor PCI address */
	bool is_async_mode;				   /* Start DOCA Sync Event in asynchronous or synchronous mode */
	bool is_update_atomic;				   /* Update DOCA Sync Event using Set or atomic Add operation */
	uint32_t async_num_tasks;			   /* Num tasks for asynchronous mode */
};

/* runtime objects */
struct sync_event_runtime_objects {
	struct doca_dev *dev;			    /* DOCA device */
	struct doca_dev_rep *rep;		    /* DOCA representor */
	struct doca_sync_event *se;		    /* DOCA Sync Event */
	struct doca_ctx *se_ctx;		    /* DOCA Sync Event Context */
	struct doca_pe *se_pe;			    /* DOCA Progress Engine */
	struct doca_comm_channel_ep_t *ep;	    /* comm_channel endpoint */
	struct doca_comm_channel_addr_t *peer_addr; /* comm_channel handle for peer address */
	doca_error_t se_task_result;		    /* Last completed Sync Event Tasks's status */
};

/*
 * Register command line parameters for DOCA Sync Event sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_params_register(void);

/*
 * DOCA device with export-to-dpu capability filter callback
 *
 * @devinfo [in]: doca_devinfo
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_get_export_to_dpu_supported(struct doca_devinfo *devinfo);

/*
 * Validate configured flow by user input
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_config_validate(const struct sync_event_config *se_cfg, const struct sync_event_runtime_objects *se_rt_objs);

/*
 * Start Sample's DOCA Sync Event in asynchronous operation mode
 *
 * @se_cfg [in]: user configuration represents command line arguments
 * @se_rt_objs [in]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_start_async(const struct sync_event_config *se_cfg, struct sync_event_runtime_objects *se_rt_objs);

/*
 * Initialize Sample's DOCA comm_channel
 *
 * @se_rt_objs [in/out]: sample's runtime resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_cc_init(struct sync_event_runtime_objects *se_rt_objs);

/*
 * Submit asynchronous DOCA Task on Sample's DOCA Sync Event Context
 *
 * @se_rt_objs [in]: sample's runtime resources
 * @se_task [in]: DOCA Task to submit
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
sync_event_async_task_submit(struct sync_event_runtime_objects *se_rt_objs, struct doca_task *se_task);

/*
 * Sample's tear down flow
 *
 * @se_rt_objs [in]: sample's runtime resources
 */
void
sync_event_tear_down(struct sync_event_runtime_objects *se_rt_objs);

#endif /* COMMON_COMMON_H */
