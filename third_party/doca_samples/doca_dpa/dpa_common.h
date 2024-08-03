/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef DPA_COMMON_H_
#define DPA_COMMON_H_

#include <doca_dpa.h>
#include <doca_argp.h>
#include <doca_sync_event.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLEEP(SECONDS) for (int i = 0; i < 1 + SECONDS * 30000; i++)	/* Macro for sleep (wait) in seconds */
#define MAX_USER_IB_DEVICE_NAME 256					/* Maximum user IB device name string length */
#define MAX_IB_DEVICE_NAME (MAX_USER_IB_DEVICE_NAME + 1)		/* Maximum IB device name string length */
#define IB_DEVICE_DEFAULT_NAME "NOT_SET"				/* IB device default name */
#define SYNC_EVENT_MASK_FFS (0xFFFFFFFFFFFFFFFF)			/* Mask for doca_sync_event_wait_gt() wait value */


/* A struct that includes all the resources needed for DPA */
struct dpa_resources {
	struct doca_dev *doca_device;			/* DOCA device for DPA */
	struct doca_dpa *doca_dpa;			/* DOCA DPA context */
};

/* Configuration struct */
struct dpa_config {
	char device_name[MAX_IB_DEVICE_NAME];	/* Buffer that holds the IB device name */
};

/*
 * Register the command line parameters for the sample.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_dpa_params(void);

/*
 * Create DOCA sync event to be published by the CPU and subscribed by the DPA
 *
 * @doca_dpa [in]: DOCA DPA context
 * @doca_device [in]: DOCA device
 * @wait_event [out]: Created DOCA sync event that is published by the CPU and subscribed by the DPA
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_doca_dpa_wait_sync_event(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, struct doca_sync_event **wait_event);

/*
 * Create DOCA sync event to be published by the DPA and subscribed by the CPU
 *
 * @doca_dpa [in]: DOCA DPA context
 * @doca_device [in]: DOCA device
 * @comp_event [out]: Created DOCA sync event that is published by the DPA and subscribed by the CPU
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_doca_dpa_completion_sync_event(struct doca_dpa *doca_dpa, struct doca_dev *doca_device, struct doca_sync_event **comp_event);

/*
 * Create DOCA sync event to be published and subscribed by the DPA
 *
 * @doca_dpa [in]: DOCA DPA context
 * @kernel_event [out]: Created DOCA sync event that is published and subscribed by the DPA
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_doca_dpa_kernel_sync_event(struct doca_dpa *doca_dpa, struct doca_sync_event **kernel_event);

/*
 * Allocate DOCA DPA resources
 *
 * @resources [in]: DOCA DPA resources to allocate
 * @cfg [in]: DOCA DPA configurations
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t allocate_dpa_resources(struct dpa_resources *resources, struct dpa_config *cfg);

/*
 * Destroy DOCA DPA resources
 *
 * @resources [in]: DOCA DPA resources to destroy
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t destroy_dpa_resources(struct dpa_resources *resources);

#ifdef __cplusplus
}
#endif

#endif /* DPA_COMMON_H_ */
