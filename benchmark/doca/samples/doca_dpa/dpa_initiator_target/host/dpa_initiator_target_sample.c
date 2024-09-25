/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include "dpa_common.h"
#include "../common/dpa_initiator_target_common_defs.h"

DOCA_LOG_REGISTER(DPA_INITIATOR_TARGET::SAMPLE);

/**
 * Initiator RPC declaration
 */
doca_dpa_func_t rdma_post_send_imm_rpc;

/**
 * Target kernels/RPC declaration
 */
doca_dpa_func_t thread1_kernel;
doca_dpa_func_t thread2_kernel;
doca_dpa_func_t rdma_post_receive_rpc;

/**
 * @brief Create and start Initiator DPA RDMA objects
 *
 * @resources [in]: DPA resources
 * @dpa_rdma1_obj [out]: Created Initiator DPA RDMA #1
 * @dpa_rdma2_obj [out]: Created Initiator DPA RDMA #2
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t initiator_rdma_objs_create(struct dpa_resources *resources,
					       struct dpa_rdma_obj *dpa_rdma1_obj,
					       struct dpa_rdma_obj *dpa_rdma2_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, tmp_doca_err = DOCA_SUCCESS;

	DOCA_LOG_INFO("Create Initiator DOCA RDMA #1");
	dpa_rdma1_obj->doca_device = resources->doca_device;
	dpa_rdma1_obj->doca_dpa = resources->doca_dpa;
	dpa_rdma1_obj->permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				     DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	doca_err = dpa_rdma_obj_init(NULL, dpa_rdma1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto exit;
	}

	DOCA_LOG_INFO("Start Initiator DOCA RDMA #1");
	doca_err = dpa_rdma_obj_start(dpa_rdma1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_start failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma1_obj;
	}

	DOCA_LOG_INFO("Create Initiator DOCA RDMA #2");
	dpa_rdma2_obj->doca_device = resources->doca_device;
	dpa_rdma2_obj->doca_dpa = resources->doca_dpa;
	dpa_rdma2_obj->permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				     DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	doca_err = dpa_rdma_obj_init(NULL, dpa_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma1_obj;
	}

	DOCA_LOG_INFO("Start Initiator DOCA RDMA #2");
	doca_err = dpa_rdma_obj_start(dpa_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function rdma_obj_start failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma2_obj;
	}

	return doca_err;

destroy_rdma2_obj:
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma2_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_rdma1_obj:
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma1_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

exit:
	return doca_err;
}

/**
 * @brief Create, attach and start Target DPA RDMA objects
 *
 * This function creates Target DPA RDMA objects, attaches to DPA completion and start them
 *
 * @resources [in]: DPA resources
 * @dpa_completion_obj [in]: DPA completion
 * @dpa_rdma1_obj [out]: Created Target DPA RDMA #1
 * @dpa_rdma2_obj [out]: Created Target DPA RDMA #2
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t target_rdma_objs_create(struct dpa_resources *resources,
					    struct dpa_completion_obj *dpa_completion_obj,
					    struct dpa_rdma_obj *dpa_rdma1_obj,
					    struct dpa_rdma_obj *dpa_rdma2_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS, tmp_doca_err = DOCA_SUCCESS;

	DOCA_LOG_INFO("Create Target DOCA RDMA #1");
	dpa_rdma1_obj->doca_device = resources->doca_device;
	dpa_rdma1_obj->doca_dpa = resources->doca_dpa;
	dpa_rdma1_obj->permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				     DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	dpa_rdma1_obj->user_data.u64 = TARGET_RDMA1_USER_DATA;
	doca_err = dpa_rdma_obj_init(NULL, dpa_rdma1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto exit;
	}

	DOCA_LOG_INFO("Create Target DOCA RDMA #2");
	dpa_rdma2_obj->doca_device = resources->doca_device;
	dpa_rdma2_obj->doca_dpa = resources->doca_dpa;
	dpa_rdma2_obj->permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				     DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	dpa_rdma2_obj->user_data.u64 = TARGET_RDMA2_USER_DATA;
	doca_err = dpa_rdma_obj_init(NULL, dpa_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma1_obj;
	}

	DOCA_LOG_INFO("Attach DOCA DPA Completion to Target DOCA RDMA #1");
	doca_err = doca_rdma_dpa_completion_attach(dpa_rdma1_obj->rdma, dpa_completion_obj->dpa_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_dpa_completion_attach failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma2_obj;
	}

	DOCA_LOG_INFO("Attach DOCA DPA Completion to Target DOCA RDMA #2");
	doca_err = doca_rdma_dpa_completion_attach(dpa_rdma2_obj->rdma, dpa_completion_obj->dpa_comp);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_dpa_completion_attach failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma2_obj;
	}

	DOCA_LOG_INFO("Start Target DOCA RDMA #1");
	doca_err = dpa_rdma_obj_start(dpa_rdma1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_start failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma2_obj;
	}

	DOCA_LOG_INFO("Start Target DOCA RDMA #2");
	doca_err = dpa_rdma_obj_start(dpa_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_start failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_rdma2_obj;
	}

	return doca_err;

destroy_rdma2_obj:
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma2_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_rdma1_obj:
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma1_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

exit:
	return doca_err;
}

/**
 * @brief Destroy Initiator/Target DPA RDMA objects
 *
 * @dpa_rdma1_obj [in]: Previously created Initiator/Target DPA RDMA #1
 * @dpa_rdma2_obj [in]: Previously created Initiator/Target DPA RDMA #2
 * @is_initiator [in]: true if objects are Initiator DPA RDMA objects
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_objs_destroy(struct dpa_rdma_obj *dpa_rdma1_obj,
				      struct dpa_rdma_obj *dpa_rdma2_obj,
				      uint8_t is_initiator)
{
	doca_error_t doca_err = DOCA_SUCCESS, tmp_doca_err = DOCA_SUCCESS;

	DOCA_LOG_INFO("Destroy %s DOCA RDMA #1", (is_initiator ? "Initiator" : "Target"));
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma1_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

	DOCA_LOG_INFO("Destroy %s DOCA RDMA #2", (is_initiator ? "Initiator" : "Target"));
	tmp_doca_err = dpa_rdma_obj_destroy(dpa_rdma2_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_rdma_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

	return doca_err;
}

/**
 * @brief RDMA Connect Initiator & Target DPA RDMA objects
 *
 * This function connect Initiator DPA RDMA #1 with Target DPA RDMA #1.
 * And Initiator DPA RDMA #2 with Target DPA RDMA #2
 *
 * @initiator_rdma1_obj [in]: Initiator DPA RDMA #1
 * @initiator_rdma2_obj [in]: Initiator DPA RDMA #2
 * @target_rdma1_obj [in]: Target DPA RDMA #1
 * @target_rdma2_obj [in]: Target DPA RDMA #2
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t rdma_objs_connect(struct dpa_rdma_obj *initiator_rdma1_obj,
				      struct dpa_rdma_obj *initiator_rdma2_obj,
				      struct dpa_rdma_obj *target_rdma1_obj,
				      struct dpa_rdma_obj *target_rdma2_obj)
{
	doca_error_t doca_err = DOCA_SUCCESS;

	DOCA_LOG_INFO("Connect Initiator DOCA RDMA #1 to Target DOCA RDMA #1");
	doca_err = doca_rdma_connect(initiator_rdma1_obj->rdma,
				     target_rdma1_obj->connection_details,
				     target_rdma1_obj->conn_det_len);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	DOCA_LOG_INFO("Connect Target DOCA RDMA #1 to Initiator DOCA RDMA #1");
	doca_err = doca_rdma_connect(target_rdma1_obj->rdma,
				     initiator_rdma1_obj->connection_details,
				     initiator_rdma1_obj->conn_det_len);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	DOCA_LOG_INFO("Connect Initiator DOCA RDMA #2 to Target DOCA RDMA #2");
	doca_err = doca_rdma_connect(initiator_rdma2_obj->rdma,
				     target_rdma2_obj->connection_details,
				     target_rdma2_obj->conn_det_len);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	DOCA_LOG_INFO("Connect Target DOCA RDMA #2 to Initiator DOCA RDMA #2");
	doca_err = doca_rdma_connect(target_rdma2_obj->rdma,
				     initiator_rdma2_obj->connection_details,
				     initiator_rdma2_obj->conn_det_len);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed (%s)", doca_error_get_descr(doca_err));
		return doca_err;
	}

	return doca_err;
}

/**
 * @brief Sample's Logic
 *
 * @resources [in]: DPA resources that the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t dpa_initiator_target(struct dpa_resources *resources)
{
	doca_error_t doca_err = DOCA_SUCCESS, tmp_doca_err = DOCA_SUCCESS;
	struct dpa_rdma_obj initiator_rdma1_obj = {0};
	struct dpa_rdma_obj initiator_rdma2_obj = {0};
	doca_dpa_dev_uintptr_t thread_arg_dev_ptr = 0;
	struct dpa_thread_obj target_thread1_obj = {0};
	struct dpa_completion_obj target_dpa_completion_obj = {0};
	struct dpa_rdma_obj target_rdma1_obj = {0};
	struct dpa_rdma_obj target_rdma2_obj = {0};
	doca_dpa_dev_uintptr_t thread2_arg_dev_ptr = 0;
	struct dpa_thread_obj target_thread2_obj = {0};
	struct dpa_notification_completion_obj target_notification_completion_obj = {0};
	struct doca_sync_event *comp_event = NULL;
	doca_dpa_dev_sync_event_t dpa_dev_se_handle = 0;
	const uint64_t wait_sync_ev_threshold = 9;
	struct dpa_thread2_arg thread2_arg = {0};
	doca_dpa_dev_uintptr_t received_buf1_dev_ptr = 0;
	struct doca_mmap_obj dpa_mmap1_obj = {0};
	doca_dpa_dev_uintptr_t received_buf2_dev_ptr = 0;
	struct doca_mmap_obj dpa_mmap2_obj = {0};
	struct dpa_thread1_arg thread1_arg = {0};
	uint64_t send_val = 1;
	struct doca_mmap_obj host_mmap_obj = {0};
	uint64_t retval = 0;
	uint32_t imm_val = 10;
	const uint32_t test_iter = 2;

	doca_err = initiator_rdma_objs_create(resources, &initiator_rdma1_obj, &initiator_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function initiator_rdma_objs_create failed (%s)", doca_error_get_descr(doca_err));
		goto exit;
	}

	DOCA_LOG_INFO("Allocate Target DPA thread #1 device argument");
	doca_err = doca_dpa_mem_alloc(resources->doca_dpa, sizeof(struct dpa_thread1_arg), &thread_arg_dev_ptr);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_alloc failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_initiator_rdma_objs;
	}

	DOCA_LOG_INFO("Create Target DOCA DPA Thread #1");
	target_thread1_obj.doca_dpa = resources->doca_dpa;
	target_thread1_obj.func = &thread1_kernel;
	target_thread1_obj.arg = thread_arg_dev_ptr;
	doca_err = dpa_thread_obj_init(&target_thread1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_thread_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_thread1_arg_dev_ptr;
	}

	DOCA_LOG_INFO("Create Target DOCA DPA Completion");
	target_dpa_completion_obj.doca_dpa = resources->doca_dpa;
	target_dpa_completion_obj.queue_size = 4;
	target_dpa_completion_obj.thread = target_thread1_obj.thread;
	doca_err = dpa_completion_obj_init(&target_dpa_completion_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_completion_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_thread1_obj;
	}

	doca_err = target_rdma_objs_create(resources, &target_dpa_completion_obj, &target_rdma1_obj, &target_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function target_rdma_objs_create failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_rdma_completion_obj;
	}

	doca_err = rdma_objs_connect(&initiator_rdma1_obj, &initiator_rdma2_obj, &target_rdma1_obj, &target_rdma2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function rdma_objs_connect failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_rdma_objs;
	}

	DOCA_LOG_INFO("Run Target DOCA DPA Thread #1");
	doca_err = doca_dpa_thread_run(target_thread1_obj.thread);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_run failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_rdma_objs;
	}

	DOCA_LOG_INFO("Allocate Target DPA thread #2 device argument");
	doca_err = doca_dpa_mem_alloc(resources->doca_dpa, sizeof(struct dpa_thread2_arg), &thread2_arg_dev_ptr);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_alloc failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_rdma_objs;
	}

	DOCA_LOG_INFO("Create Target DOCA DPA Thread #2");
	target_thread2_obj.doca_dpa = resources->doca_dpa;
	target_thread2_obj.func = &thread2_kernel;
	target_thread2_obj.arg = thread2_arg_dev_ptr;
	doca_err = dpa_thread_obj_init(&target_thread2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_thread_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_thread2_arg_dev_ptr;
	}

	DOCA_LOG_INFO("Create Target DOCA DPA Notification Completion");
	target_notification_completion_obj.doca_dpa = resources->doca_dpa;
	target_notification_completion_obj.thread = target_thread2_obj.thread;
	doca_err = dpa_notification_completion_obj_init(&target_notification_completion_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_notification_completion_obj_init failed (%s)",
			     doca_error_get_descr(doca_err));
		goto destroy_target_thread2_obj;
	}

	DOCA_LOG_INFO("Run Target DOCA DPA Thread #2");
	doca_err = doca_dpa_thread_run(target_thread2_obj.thread);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_thread_run failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_target_notification_completion_obj;
	}

	DOCA_LOG_INFO("Create completion DOCA sync event");
	doca_err = create_doca_dpa_completion_sync_event(resources->doca_dpa,
							 resources->doca_device,
							 &comp_event,
							 &dpa_dev_se_handle);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_doca_dpa_completion_sync_event failed (%s)",
			     doca_error_get_descr(doca_err));
		goto destroy_target_notification_completion_obj;
	}

	DOCA_LOG_INFO("Update Target DPA thread #2 device argument");
	thread2_arg.sync_event_handle = dpa_dev_se_handle;
	thread2_arg.completion_count = wait_sync_ev_threshold + 1;
	doca_err = doca_dpa_h2d_memcpy(resources->doca_dpa,
				       thread2_arg_dev_ptr,
				       &thread2_arg,
				       sizeof(struct dpa_thread2_arg));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_h2d_memcpy failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_completion_sync_event;
	}

	DOCA_LOG_INFO("Create Target DOCA MMAP on DPA for Target DOCA RDMA #1 received buffer");
	doca_err = doca_dpa_mem_alloc(resources->doca_dpa, sizeof(uint64_t), &received_buf1_dev_ptr);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_alloc failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_completion_sync_event;
	}

	doca_err = doca_dpa_memset(resources->doca_dpa, received_buf1_dev_ptr, 0, sizeof(uint64_t));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_memset failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_received_buf1_dev_ptr;
	}

	dpa_mmap1_obj.mmap_type = MMAP_TYPE_DPA;
	dpa_mmap1_obj.doca_dpa = resources->doca_dpa;
	dpa_mmap1_obj.doca_device = resources->doca_device;
	dpa_mmap1_obj.permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				    DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	dpa_mmap1_obj.memrange_addr = (void *)received_buf1_dev_ptr;
	dpa_mmap1_obj.memrange_len = sizeof(uint64_t);
	doca_err = doca_mmap_obj_init(&dpa_mmap1_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_received_buf1_dev_ptr;
	}

	DOCA_LOG_INFO("Create Target DOCA MMAP on DPA for Target DOCA RDMA #2 received buffer");
	doca_err = doca_dpa_mem_alloc(resources->doca_dpa, sizeof(uint64_t), &received_buf2_dev_ptr);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_alloc failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_dpa_mmap1_obj;
	}

	doca_err = doca_dpa_memset(resources->doca_dpa, received_buf2_dev_ptr, 0, sizeof(uint64_t));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_memset failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_received_buf2_dev_ptr;
	}

	dpa_mmap2_obj.mmap_type = MMAP_TYPE_DPA;
	dpa_mmap2_obj.doca_dpa = resources->doca_dpa;
	dpa_mmap2_obj.doca_device = resources->doca_device;
	dpa_mmap2_obj.permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				    DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	dpa_mmap2_obj.memrange_addr = (void *)received_buf2_dev_ptr;
	dpa_mmap2_obj.memrange_len = sizeof(uint64_t);
	doca_err = doca_mmap_obj_init(&dpa_mmap2_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_received_buf2_dev_ptr;
	}

	DOCA_LOG_INFO("Update Target DPA thread #1 device argument");
	thread1_arg.notification_comp_handle = target_notification_completion_obj.handle;
	thread1_arg.dpa_comp_handle = target_dpa_completion_obj.handle;
	thread1_arg.target_rdma1_handle = (doca_dpa_dev_uintptr_t)target_rdma1_obj.dpa_rdma;
	thread1_arg.local_buf1_addr = received_buf1_dev_ptr;
	thread1_arg.dpa_mmap1_handle = dpa_mmap1_obj.dpa_mmap_handle;
	thread1_arg.target_rdma2_handle = (doca_dpa_dev_uintptr_t)target_rdma2_obj.dpa_rdma;
	thread1_arg.local_buf2_addr = received_buf2_dev_ptr;
	thread1_arg.dpa_mmap2_handle = dpa_mmap2_obj.dpa_mmap_handle;
	thread1_arg.length = dpa_mmap1_obj.memrange_len;
	doca_err = doca_dpa_h2d_memcpy(resources->doca_dpa,
				       thread_arg_dev_ptr,
				       &thread1_arg,
				       sizeof(struct dpa_thread1_arg));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_h2d_memcpy failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_dpa_mmap2_obj;
	}

	DOCA_LOG_INFO("Create Initiator DOCA MMAP for send buffer on Host");
	host_mmap_obj.mmap_type = MMAP_TYPE_CPU;
	host_mmap_obj.doca_dpa = resources->doca_dpa;
	host_mmap_obj.doca_device = resources->doca_device;
	host_mmap_obj.permissions = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE |
				    DOCA_ACCESS_FLAG_RDMA_READ | DOCA_ACCESS_FLAG_RDMA_ATOMIC;
	host_mmap_obj.memrange_addr = (void *)&send_val;
	host_mmap_obj.memrange_len = sizeof(uint64_t);
	doca_err = doca_mmap_obj_init(&host_mmap_obj);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_init failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_dpa_mmap2_obj;
	}

	DOCA_LOG_INFO("Trigger an RPC to prepare Target DPA receive memory");
	doca_err = doca_dpa_rpc(resources->doca_dpa,
				&rdma_post_receive_rpc,
				&retval,
				(doca_dpa_dev_uintptr_t)target_rdma1_obj.dpa_rdma,
				received_buf1_dev_ptr,
				dpa_mmap1_obj.dpa_mmap_handle,
				(doca_dpa_dev_uintptr_t)target_rdma2_obj.dpa_rdma,
				received_buf2_dev_ptr,
				dpa_mmap2_obj.dpa_mmap_handle,
				sizeof(uint64_t));
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_rpc failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_host_mmap_obj;
	}

	for (uint32_t i = 0; i < test_iter; i++) {
		DOCA_LOG_INFO(
			"Trigger an RPC to post send on Initiator DOCA RDMA #1 with buffer %lu and immediate value %u",
			send_val,
			imm_val);
		doca_err = doca_dpa_rpc(resources->doca_dpa,
					&rdma_post_send_imm_rpc,
					&retval,
					initiator_rdma1_obj.dpa_rdma,
					&send_val,
					host_mmap_obj.dpa_mmap_handle,
					host_mmap_obj.memrange_len,
					imm_val++);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_dpa_rpc failed (%s)", doca_error_get_descr(doca_err));
			goto destroy_host_mmap_obj;
		}

		// update mmap value
		send_val++;

		DOCA_LOG_INFO(
			"Trigger an RPC to post send on Initiator DOCA RDMA #2 with buffer %lu and immediate value %u",
			send_val,
			imm_val);
		doca_err = doca_dpa_rpc(resources->doca_dpa,
					&rdma_post_send_imm_rpc,
					&retval,
					initiator_rdma2_obj.dpa_rdma,
					&send_val,
					host_mmap_obj.dpa_mmap_handle,
					host_mmap_obj.memrange_len,
					imm_val++);
		if (doca_err != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_dpa_rpc failed (%s)", doca_error_get_descr(doca_err));
			goto destroy_host_mmap_obj;
		}

		send_val++;
	}

	DOCA_LOG_INFO("Waiting for Target Thread #2 to set completion Sync Event value to be greater than %lu",
		      wait_sync_ev_threshold);
	doca_err = doca_sync_event_wait_gt(comp_event, wait_sync_ev_threshold, SYNC_EVENT_MASK_FFS);
	if (doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_sync_event_wait_gt failed (%s)", doca_error_get_descr(doca_err));
		goto destroy_host_mmap_obj;
	}

	DOCA_LOG_INFO("Tear down");

destroy_host_mmap_obj:
	tmp_doca_err = doca_mmap_obj_destroy(&host_mmap_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_dpa_mmap2_obj:
	tmp_doca_err = doca_mmap_obj_destroy(&dpa_mmap2_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_received_buf2_dev_ptr:
	tmp_doca_err = doca_dpa_mem_free(resources->doca_dpa, received_buf2_dev_ptr);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_free failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_dpa_mmap1_obj:
	tmp_doca_err = doca_mmap_obj_destroy(&dpa_mmap1_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_received_buf1_dev_ptr:
	tmp_doca_err = doca_dpa_mem_free(resources->doca_dpa, received_buf1_dev_ptr);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_free failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_completion_sync_event:
	tmp_doca_err = doca_sync_event_destroy(comp_event);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_sync_event_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_notification_completion_obj:
	tmp_doca_err = dpa_notification_completion_obj_destroy(&target_notification_completion_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_notification_completion_obj_destroy failed: %s",
			     doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_thread2_obj:
	tmp_doca_err = dpa_thread_obj_destroy(&target_thread2_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_thread_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_thread2_arg_dev_ptr:
	tmp_doca_err = doca_dpa_mem_free(resources->doca_dpa, thread2_arg_dev_ptr);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_free failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_rdma_objs:
	tmp_doca_err = rdma_objs_destroy(&target_rdma1_obj, &target_rdma2_obj, 0);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function rdma_objs_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_rdma_completion_obj:
	tmp_doca_err = dpa_completion_obj_destroy(&target_dpa_completion_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_completion_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_thread1_obj:
	tmp_doca_err = dpa_thread_obj_destroy(&target_thread1_obj);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function dpa_thread_obj_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_target_thread1_arg_dev_ptr:
	tmp_doca_err = doca_dpa_mem_free(resources->doca_dpa, thread_arg_dev_ptr);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_dpa_mem_free failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

destroy_initiator_rdma_objs:
	tmp_doca_err = rdma_objs_destroy(&initiator_rdma1_obj, &initiator_rdma2_obj, 1);
	if (tmp_doca_err != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function rdma_objs_destroy failed: %s", doca_error_get_descr(tmp_doca_err));
		DOCA_ERROR_PROPAGATE(doca_err, tmp_doca_err);
	}

exit:
	return doca_err;
}
