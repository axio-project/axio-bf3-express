/*
 * Copyright (c) 2023-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_error.h>
#include <doca_argp.h>

#include "rdma_common.h"
#include "common.h"

DOCA_LOG_REGISTER(GPURDMA::SAMPLE);

struct rdma_resources resources = {0};
struct rdma_mmap_obj server_local_mmap_obj_A = {0};
struct rdma_mmap_obj server_local_mmap_obj_F = {0};
struct rdma_mmap_obj client_local_mmap_obj_B = {0};
struct rdma_mmap_obj client_local_mmap_obj_C = {0};
struct rdma_mmap_obj client_local_mmap_obj_F = {0};
struct doca_mmap *server_remote_mmap_F;
struct doca_mmap *client_remote_mmap_A;
const uint32_t access_params = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE | DOCA_ACCESS_FLAG_RDMA_WRITE;
uint8_t *server_local_buf_A_gpu;
uint8_t *server_local_buf_A_cpu;
uint8_t *client_local_buf_B_gpu;
uint8_t *client_local_buf_B_cpu;
uint8_t *client_local_buf_C_gpu;
uint8_t *client_local_buf_C_cpu;
uint8_t *server_local_buf_F;
uint8_t *client_local_buf_F;
struct buf_arr_obj server_local_buf_arr_A = {0};
struct buf_arr_obj server_local_buf_arr_F = {0};
struct buf_arr_obj server_remote_buf_arr_F = {0};
struct buf_arr_obj client_remote_buf_arr_A = {0};
struct buf_arr_obj client_local_buf_arr_B = {0};
struct buf_arr_obj client_local_buf_arr_C = {0};
struct buf_arr_obj client_local_buf_arr_F = {0};
cudaStream_t cstream, cstream_client;
int oob_sock_fd = -1;
int oob_client_sock = -1;

/*
 * Create local and remote mmap and buffer array for server
 *
 * @oob_sock_fd [in]: socket fd
 * @resources [in]: rdma resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_memory_local_remote_server(int oob_sock_fd, struct rdma_resources *resources)
{
	void *server_remote_export_F = NULL;
	size_t server_remote_export_F_len;
	doca_error_t result;
	cudaError_t cuda_err;

	/* Buffer A */
	/* Register local source buffer obtain an object representing the memory */
	result = doca_gpu_mem_alloc(resources->gpudev,
				    (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_A,
				    4096,
				    DOCA_GPU_MEM_TYPE_GPU_CPU,
				    (void **)&server_local_buf_A_gpu,
				    (void **)&server_local_buf_A_cpu);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc failed: %s", doca_error_get_descr(result));
		goto error;
	}

	cuda_err = cudaMemset(server_local_buf_A_gpu, 0x1, GPU_BUF_NUM * GPU_BUF_SIZE_A);
	if (cuda_err != cudaSuccess) {
		DOCA_LOG_ERR("Can't CUDA memset buffer A: %d", cuda_err);
		goto error;
	}

	server_local_mmap_obj_A.doca_device = resources->doca_device;
	server_local_mmap_obj_A.permissions = access_params;
	server_local_mmap_obj_A.memrange_addr = server_local_buf_A_gpu;
	server_local_mmap_obj_A.memrange_len = (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_A;

	DOCA_LOG_INFO("Create local server mmap A context");
	result = create_mmap(&server_local_mmap_obj_A);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_mmap failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Buffer F */
	/* Register local source buffer obtain an object representing the memory */
	result = doca_gpu_mem_alloc(resources->gpudev,
				    (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_F,
				    4096,
				    DOCA_GPU_MEM_TYPE_GPU,
				    (void **)&server_local_buf_F,
				    NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc failed: %s", doca_error_get_descr(result));
		goto error;
	}

	cuda_err = cudaMemset(server_local_buf_F, 0x1, GPU_BUF_NUM * GPU_BUF_SIZE_F);
	if (cuda_err != cudaSuccess) {
		DOCA_LOG_ERR("Can't CUDA memset buffer A: %d", cuda_err);
		goto error;
	}

	server_local_mmap_obj_F.doca_device = resources->doca_device;
	server_local_mmap_obj_F.permissions = access_params;
	server_local_mmap_obj_F.memrange_addr = server_local_buf_F;
	server_local_mmap_obj_F.memrange_len = (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_F;

	/* create local mmap object */
	DOCA_LOG_INFO("Create local server mmap A context");
	result = create_mmap(&server_local_mmap_obj_F);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_mmap failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Application does out-of-band passing of exported mmap to remote side and receiving exported mmap */
	DOCA_LOG_INFO("Send exported mmap A to remote client");
	if (send(oob_sock_fd, &server_local_mmap_obj_A.export_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to send exported mmap");
		goto error;
	}

	if (send(oob_sock_fd, server_local_mmap_obj_A.rdma_export, server_local_mmap_obj_A.export_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to send exported mmap");
		goto error;
	}

	DOCA_LOG_INFO("Receive client mmap F export");
	if (recv(oob_sock_fd, &server_remote_export_F_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto error;
	}

	server_remote_export_F = calloc(1, server_remote_export_F_len);
	if (server_remote_export_F == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for remote mmap export");
		goto error;
	}

	if (recv(oob_sock_fd, server_remote_export_F, server_remote_export_F_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto error;
	}

	result = doca_mmap_create_from_export(NULL,
					      server_remote_export_F,
					      server_remote_export_F_len,
					      resources->doca_device,
					      &server_remote_mmap_F);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_create_from_export failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* create local and remote buf arrays */
	server_local_buf_arr_A.gpudev = resources->gpudev;
	server_local_buf_arr_A.mmap = server_local_mmap_obj_A.mmap;
	server_local_buf_arr_A.num_elem = GPU_BUF_NUM;
	server_local_buf_arr_A.elem_size = GPU_BUF_SIZE_A;

	DOCA_LOG_INFO("Create local DOCA buf array context A");
	result = create_buf_arr_on_gpu(&server_local_buf_arr_A);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		goto error;
	}

	server_local_buf_arr_F.gpudev = resources->gpudev;
	server_local_buf_arr_F.mmap = server_local_mmap_obj_F.mmap;
	server_local_buf_arr_F.num_elem = 1;
	server_local_buf_arr_F.elem_size = (size_t)(GPU_BUF_NUM * GPU_BUF_SIZE_F);

	DOCA_LOG_INFO("Create local DOCA buf array context F");
	result = create_buf_arr_on_gpu(&server_local_buf_arr_F);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		goto error;
	}

	server_remote_buf_arr_F.gpudev = resources->gpudev;
	server_remote_buf_arr_F.mmap = server_remote_mmap_F;
	server_remote_buf_arr_F.num_elem = 1;
	server_remote_buf_arr_F.elem_size = (size_t)(GPU_BUF_NUM * GPU_BUF_SIZE_F);

	DOCA_LOG_INFO("Create remote DOCA buf array context F");
	result = create_buf_arr_on_gpu(&server_remote_buf_arr_F);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		doca_buf_arr_destroy(server_local_buf_arr_A.buf_arr);
		goto error;
	}

	free(server_remote_export_F);

	return DOCA_SUCCESS;

error:
	if (server_remote_export_F)
		free(server_remote_export_F);

	return result;
}

/*
 * Create local and remote mmap and buffer array for client
 *
 * @oob_sock_fd [in]: socket fd
 * @resources [in]: rdma resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_memory_local_remote_client(int oob_sock_fd, struct rdma_resources *resources)
{
	void *client_remote_export_A = NULL;
	size_t client_remote_export_A_len;
	doca_error_t result;
	cudaError_t cuda_err;

	/* Buffer B - 512B */
	/* Register local source buffer obtain an object representing the memory */
	result = doca_gpu_mem_alloc(resources->gpudev,
				    (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_B,
				    4096,
				    DOCA_GPU_MEM_TYPE_GPU_CPU,
				    (void **)&client_local_buf_B_gpu,
				    (void **)&client_local_buf_B_cpu);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc failed: %s", doca_error_get_descr(result));
		goto error;
	}

	cuda_err = cudaMemset(client_local_buf_B_gpu, 0x2, GPU_BUF_NUM * GPU_BUF_SIZE_B);
	if (cuda_err != cudaSuccess) {
		DOCA_LOG_ERR("Can't CUDA memset buffer B: %d", cuda_err);
		goto error;
	}

	client_local_mmap_obj_B.doca_device = resources->doca_device;
	client_local_mmap_obj_B.permissions = access_params;
	client_local_mmap_obj_B.memrange_addr = client_local_buf_B_gpu;
	client_local_mmap_obj_B.memrange_len = (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_B;

	/* create local mmap object */
	DOCA_LOG_INFO("Create local server mmap B context");
	result = create_mmap(&client_local_mmap_obj_B);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_mmap failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Buffer C - 512B */
	/* Register local source buffer obtain an object representing the memory */
	result = doca_gpu_mem_alloc(resources->gpudev,
				    (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_C,
				    4096,
				    DOCA_GPU_MEM_TYPE_GPU_CPU,
				    (void **)&client_local_buf_C_gpu,
				    (void **)&client_local_buf_C_cpu);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc failed: %s", doca_error_get_descr(result));
		goto error;
	}

	cuda_err = cudaMemset(client_local_buf_C_gpu, 0x3, GPU_BUF_NUM * GPU_BUF_SIZE_C);
	if (cuda_err != cudaSuccess) {
		DOCA_LOG_ERR("Can't CUDA memset buffer C: %d", cuda_err);
		goto error;
	}

	client_local_mmap_obj_C.doca_device = resources->doca_device;
	client_local_mmap_obj_C.permissions = access_params;
	client_local_mmap_obj_C.memrange_addr = client_local_buf_C_gpu;
	client_local_mmap_obj_C.memrange_len = (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_C;

	/* create local mmap object */
	DOCA_LOG_INFO("Create local server mmap C context");
	result = create_mmap(&client_local_mmap_obj_C);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_mmap failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Buffer F - 4B */
	/* Register local source buffer obtain an object representing the memory */
	result = doca_gpu_mem_alloc(resources->gpudev,
				    (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_F,
				    4096,
				    DOCA_GPU_MEM_TYPE_GPU,
				    (void **)&client_local_buf_F,
				    NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc failed: %s", doca_error_get_descr(result));
		goto error;
	}

	client_local_mmap_obj_F.doca_device = resources->doca_device;
	client_local_mmap_obj_F.permissions = access_params;
	client_local_mmap_obj_F.memrange_addr = client_local_buf_F;
	client_local_mmap_obj_F.memrange_len = (size_t)GPU_BUF_NUM * GPU_BUF_SIZE_F;

	/* create local mmap object */
	DOCA_LOG_INFO("Create local server mmap F context");
	result = create_mmap(&client_local_mmap_obj_F);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_mmap failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Application does out-of-band passing of exported mmap to remote side and receiving exported mmap */

	/* Receive server remote A */
	DOCA_LOG_INFO("Receive remote mmap A export from server");
	if (recv(oob_sock_fd, &client_remote_export_A_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto error;
	}

	client_remote_export_A = calloc(1, client_remote_export_A_len);
	if (client_remote_export_A == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for remote mmap export");
		goto error;
	}

	if (recv(oob_sock_fd, client_remote_export_A, client_remote_export_A_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto error;
	}

	result = doca_mmap_create_from_export(NULL,
					      client_remote_export_A,
					      client_remote_export_A_len,
					      resources->doca_device,
					      &client_remote_mmap_A);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_mmap_create_from_export failed: %s", doca_error_get_descr(result));
		goto error;
	}

	/* Send client local F */
	DOCA_LOG_INFO("Send exported mmap F to remote server");
	if (send(oob_sock_fd, &client_local_mmap_obj_F.export_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to send exported mmap");
		goto error;
	}

	if (send(oob_sock_fd, client_local_mmap_obj_F.rdma_export, client_local_mmap_obj_F.export_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to send exported mmap");
		goto error;
	}

	/* create local and remote buf arrays */
	client_local_buf_arr_B.gpudev = resources->gpudev;
	client_local_buf_arr_B.mmap = client_local_mmap_obj_B.mmap;
	client_local_buf_arr_B.num_elem = GPU_BUF_NUM;
	client_local_buf_arr_B.elem_size = GPU_BUF_SIZE_B;

	/* create local buf array object */
	DOCA_LOG_INFO("Create local DOCA buf array context B");
	result = create_buf_arr_on_gpu(&client_local_buf_arr_B);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		goto error;
	}

	client_local_buf_arr_C.gpudev = resources->gpudev;
	client_local_buf_arr_C.mmap = client_local_mmap_obj_C.mmap;
	client_local_buf_arr_C.num_elem = GPU_BUF_NUM;
	client_local_buf_arr_C.elem_size = GPU_BUF_SIZE_C;

	/* create local buf array object */
	DOCA_LOG_INFO("Create local DOCA buf array context C");
	result = create_buf_arr_on_gpu(&client_local_buf_arr_C);
	if (result != DOCA_SUCCESS) {
		doca_buf_arr_destroy(client_local_buf_arr_B.buf_arr);
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		goto error;
	}

	client_local_buf_arr_F.gpudev = resources->gpudev;
	client_local_buf_arr_F.mmap = client_local_mmap_obj_F.mmap;
	client_local_buf_arr_F.num_elem = 1;
	client_local_buf_arr_F.elem_size = (size_t)(GPU_BUF_NUM * GPU_BUF_SIZE_F);

	/* create local buf array object */
	DOCA_LOG_INFO("Create local DOCA buf array context F");
	result = create_buf_arr_on_gpu(&client_local_buf_arr_F);
	if (result != DOCA_SUCCESS) {
		doca_buf_arr_destroy(client_local_buf_arr_B.buf_arr);
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		goto error;
	}

	client_remote_buf_arr_A.gpudev = resources->gpudev;
	client_remote_buf_arr_A.mmap = client_remote_mmap_A;
	client_remote_buf_arr_A.num_elem = GPU_BUF_NUM;
	client_remote_buf_arr_A.elem_size = GPU_BUF_SIZE_A;

	/* create remote buf array object */
	DOCA_LOG_INFO("Create remote DOCA buf array context");
	result = create_buf_arr_on_gpu(&client_remote_buf_arr_A);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_buf_arr_on_gpu failed: %s", doca_error_get_descr(result));
		doca_buf_arr_destroy(client_local_buf_arr_B.buf_arr);
		doca_buf_arr_destroy(client_local_buf_arr_C.buf_arr);
		goto error;
	}

	free(client_remote_export_A);

	return DOCA_SUCCESS;

error:
	if (client_remote_export_A)
		free(client_remote_export_A);

	return result;
}

/*
 * Destroy local and remote mmap and buffer array, server side
 *
 * @resources [in]: rdma resources
 */
static void destroy_memory_local_remote_server(struct rdma_resources *resources)
{
	doca_error_t result = DOCA_SUCCESS;

	result = doca_mmap_destroy(server_local_mmap_obj_A.mmap);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_mmap_destroy(server_local_mmap_obj_F.mmap);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_mmap_destroy(server_remote_mmap_F);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_gpu_mem_free(resources->gpudev, server_local_buf_A_gpu);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_gpu_mem_free failed: %s", doca_error_get_descr(result));

	result = doca_gpu_mem_free(resources->gpudev, server_local_buf_F);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_gpu_mem_free failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(server_local_buf_arr_A.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(server_local_buf_arr_F.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(server_remote_buf_arr_F.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));
}

/*
 * Destroy local and remote mmap and buffer array, client side
 *
 * @resources [in]: rdma resources
 */
static void destroy_memory_local_remote_client(struct rdma_resources *resources)
{
	doca_error_t result = DOCA_SUCCESS;

	result = doca_mmap_destroy(client_local_mmap_obj_B.mmap);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_mmap_destroy(client_local_mmap_obj_C.mmap);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_mmap_destroy(client_local_mmap_obj_F.mmap);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_mmap_destroy(client_remote_mmap_A);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_mmap_destroy failed: %s", doca_error_get_descr(result));

	result = doca_gpu_mem_free(resources->gpudev, client_local_buf_B_gpu);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_gpu_mem_free failed: %s", doca_error_get_descr(result));

	result = doca_gpu_mem_free(resources->gpudev, client_local_buf_C_gpu);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_gpu_mem_free failed: %s", doca_error_get_descr(result));

	result = doca_gpu_mem_free(resources->gpudev, client_local_buf_F);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_gpu_mem_free failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(client_local_buf_arr_B.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(client_local_buf_arr_C.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(client_local_buf_arr_F.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));

	result = doca_buf_arr_destroy(client_remote_buf_arr_A.buf_arr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Function doca_buf_arr_destroy failed: %s", doca_error_get_descr(result));
}

/*
 * Server side of the RDMA write
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdma_write_server(struct rdma_config *cfg)
{
	const uint32_t rdma_permissions = access_params;
	doca_error_t result, tmp_result;
	void *remote_conn_details = NULL;
	size_t remote_conn_details_len = 0;
	cudaError_t cuda_ret;
	int ret = 0;

	/* Allocating resources */
	result = create_rdma_resources(cfg, rdma_permissions, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	/* get dpa rdma handle */
	result = doca_rdma_get_gpu_handle(resources.rdma, &(resources.gpu_rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get RDMA GPU handler: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	ret = oob_connection_server_setup(&oob_sock_fd, &oob_client_sock);
	if (ret < 0) {
		DOCA_LOG_ERR("Failed to setup OOB connection with remote peer");
		goto destroy_resources;
	}

	/* export connection details */
	result = doca_rdma_export(resources.rdma, &(resources.connection_details), &(resources.conn_det_len));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export RDMA with connection details");
		goto close_connection;
	}

	/* Application does out-of-band passing of rdma address to remote side and receiving remote address */
	DOCA_LOG_INFO("Send connection details to remote peer size %zd str %s",
		      resources.conn_det_len,
		      (char *)resources.connection_details);
	if (send(oob_client_sock, &resources.conn_det_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to send connection details");
		goto close_connection;
	}

	if (send(oob_client_sock, resources.connection_details, resources.conn_det_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to send connection details");
		return EXIT_FAILURE;
	}

	DOCA_LOG_INFO("Receive remote connection details");
	if (recv(oob_client_sock, &remote_conn_details_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto close_connection;
	}

	remote_conn_details = calloc(1, remote_conn_details_len);
	if (remote_conn_details == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for remote connection details");
		goto close_connection;
	}

	if (recv(oob_client_sock, remote_conn_details, remote_conn_details_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto close_connection;
	}

	/* Connect local rdma to the remote rdma */
	DOCA_LOG_INFO("Connect DOCA RDMA to remote RDMA");
	result = doca_rdma_connect(resources.rdma, remote_conn_details, remote_conn_details_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	free(remote_conn_details);
	remote_conn_details = NULL;

	result = create_memory_local_remote_server(oob_client_sock, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_memory_local_remote_server failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	cuda_ret = cudaStreamCreateWithFlags(&cstream, cudaStreamNonBlocking);
	if (cuda_ret != cudaSuccess) {
		DOCA_LOG_ERR("Function cudaStreamCreateWithFlags error %d", cuda_ret);
		goto close_connection;
	}

	DOCA_LOG_INFO("Before launching CUDA kernel, buffer array A is:");
	for (int idx = 0; idx < 4; idx++) {
		DOCA_LOG_INFO("Buffer %d -> offset 0: %x%x%x%x | offset %d: %x%x%x%x",
			      idx,
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 0],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 1],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 2],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 3],
			      GPU_BUF_SIZE_B,
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 0],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 1],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 2],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 3]);
	}

	result = kernel_write_server(cstream,
				     resources.gpu_rdma,
				     server_local_buf_arr_A.gpu_buf_arr,
				     server_local_buf_arr_F.gpu_buf_arr,
				     server_remote_buf_arr_F.gpu_buf_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function kernel_write_server failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	cudaStreamSynchronize(cstream);

	DOCA_LOG_INFO("After launching CUDA kernel, buffer array A is:");
	for (int idx = 0; idx < 4; idx++) {
		DOCA_LOG_INFO("Buffer %d -> offset 0: %x%x%x%x | offset %d: %x%x%x%x",
			      idx,
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 0],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 1],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 2],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + 3],
			      GPU_BUF_SIZE_B,
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 0],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 1],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 2],
			      server_local_buf_A_cpu[(GPU_BUF_SIZE_A * idx) + GPU_BUF_SIZE_B + 3]);
	}

	oob_connection_server_close(oob_sock_fd, oob_client_sock);

	destroy_memory_local_remote_server(&resources);

	tmp_result = destroy_rdma_resources(&resources);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(tmp_result));

	return DOCA_SUCCESS;

close_connection:
	oob_connection_server_close(oob_sock_fd, oob_client_sock);

destroy_resources:

	destroy_memory_local_remote_server(&resources);

	tmp_result = destroy_rdma_resources(&resources);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(tmp_result));

	if (remote_conn_details)
		free(remote_conn_details);

	return result;
}

/*
 * Client side of the RDMA write
 *
 * @cfg [in]: Configuration parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdma_write_client(struct rdma_config *cfg)
{
	const uint32_t rdma_permissions = access_params;
	doca_error_t result;
	void *remote_conn_details = NULL;
	size_t remote_conn_details_len = 0;
	int ret = 0;

	/* Allocating resources */
	result = create_rdma_resources(cfg, rdma_permissions, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate RDMA Resources: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("Function create_rdma_resources completed correctly");

	/* get dpa rdma handle */
	result = doca_rdma_get_gpu_handle(resources.rdma, &(resources.gpu_rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get RDMA GPU handler: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	DOCA_LOG_INFO("Got GPU handle at %p", resources.gpu_rdma);

	ret = oob_connection_client_setup(cfg->server_ip_addr, &oob_sock_fd);
	if (ret < 0) {
		DOCA_LOG_ERR("Failed to setup OOB connection with remote peer");
		goto destroy_resources;
	}

	/* export connection details */
	result = doca_rdma_export(resources.rdma, &(resources.connection_details), &(resources.conn_det_len));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to export RDMA with connection details");
		goto close_connection;
	}

	/* Application does out-of-band passing of rdma address to remote side and receiving remote address */

	DOCA_LOG_INFO("Receive remote connection details");
	if (recv(oob_sock_fd, &remote_conn_details_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto close_connection;
	}

	remote_conn_details = calloc(1, remote_conn_details_len);
	if (remote_conn_details == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for remote connection details");
		goto close_connection;
	}

	if (recv(oob_sock_fd, remote_conn_details, remote_conn_details_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to receive remote connection details");
		goto close_connection;
	}

	DOCA_LOG_INFO("Send connection details to remote peer size %zd str %s",
		      resources.conn_det_len,
		      (char *)resources.connection_details);
	if (send(oob_sock_fd, &resources.conn_det_len, sizeof(size_t), 0) < 0) {
		DOCA_LOG_ERR("Failed to send connection details");
		goto close_connection;
	}

	if (send(oob_sock_fd, resources.connection_details, resources.conn_det_len, 0) < 0) {
		DOCA_LOG_ERR("Failed to send connection details");
		goto close_connection;
	}

	/* Connect local rdma to the remote rdma */
	DOCA_LOG_INFO("Connect DOCA RDMA to remote RDMA");
	result = doca_rdma_connect(resources.rdma, remote_conn_details, remote_conn_details_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_rdma_connect failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	free(remote_conn_details);
	remote_conn_details = NULL;

	result = create_memory_local_remote_client(oob_sock_fd, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_memory_local_remote_client failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	result = kernel_write_client(cstream,
				     resources.gpu_rdma,
				     client_local_buf_arr_B.gpu_buf_arr,
				     client_local_buf_arr_C.gpu_buf_arr,
				     client_local_buf_arr_F.gpu_buf_arr,
				     client_remote_buf_arr_A.gpu_buf_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function kernel_write_client failed: %s", doca_error_get_descr(result));
		goto close_connection;
	}

	cudaStreamSynchronize(cstream);
	oob_connection_client_close(oob_sock_fd);

	destroy_memory_local_remote_client(&resources);

	result = destroy_rdma_resources(&resources);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(result));

	return DOCA_SUCCESS;

close_connection:
	oob_connection_client_close(oob_sock_fd);

destroy_resources:

	destroy_memory_local_remote_client(&resources);

	result = destroy_rdma_resources(&resources);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA resources: %s", doca_error_get_descr(result));

	if (remote_conn_details)
		free(remote_conn_details);

	return result;
}
