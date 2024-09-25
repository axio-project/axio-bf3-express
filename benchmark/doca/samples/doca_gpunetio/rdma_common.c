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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_error.h>
#include <doca_argp.h>
#include <doca_pe.h>

#include "rdma_common.h"
#include "common.h"

DOCA_LOG_REGISTER(GPURDMA::COMMON);

/*
 * OOB connection to exchange RDMA info - server side
 *
 * @oob_sock_fd [out]: Socket FD
 * @oob_client_sock [out]: Client socket FD
 * @return: positive integer on success and -1 otherwise
 */
int oob_connection_server_setup(int *oob_sock_fd, int *oob_client_sock)
{
	struct sockaddr_in server_addr = {0}, client_addr = {0};
	unsigned int client_size = 0;
	int enable = 1;
	int oob_sock_fd_ = 0;
	int oob_client_sock_ = 0;

	/* Create socket */
	oob_sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (oob_sock_fd_ < 0) {
		DOCA_LOG_ERR("Error while creating socket %d", oob_sock_fd_);
		return -1;
	}
	DOCA_LOG_ERR("Socket created successfully");

	if (setsockopt(oob_sock_fd_, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable))) {
		DOCA_LOG_ERR("Error setting socket options");
		close(oob_sock_fd_);
		return -1;
	}

	/* Set port and IP: */
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(2000);
	server_addr.sin_addr.s_addr = INADDR_ANY; /* listen on any interface */

	/* Bind to the set port and IP: */
	if (bind(oob_sock_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		DOCA_LOG_ERR("Couldn't bind to the port");
		close(oob_sock_fd_);
		return -1;
	}
	DOCA_LOG_INFO("Done with binding");

	/* Listen for clients: */
	if (listen(oob_sock_fd_, 1) < 0) {
		DOCA_LOG_ERR("Error while listening");
		close(oob_sock_fd_);
		return -1;
	}
	DOCA_LOG_INFO("Listening for incoming connections");

	/* Accept an incoming connection: */
	client_size = sizeof(client_addr);
	oob_client_sock_ = accept(oob_sock_fd_, (struct sockaddr *)&client_addr, &client_size);
	if (oob_client_sock_ < 0) {
		DOCA_LOG_ERR("Can't accept socket connection %d", oob_client_sock_);
		close(oob_sock_fd_);
		return -1;
	}

	*(oob_sock_fd) = oob_sock_fd_;
	*(oob_client_sock) = oob_client_sock_;

	DOCA_LOG_INFO("Client connected at IP: %s and port: %i",
		      inet_ntoa(client_addr.sin_addr),
		      ntohs(client_addr.sin_port));

	return 0;
}

/*
 * OOB connection to exchange RDMA info - server side closure
 *
 * @oob_sock_fd [in]: Socket FD
 * @oob_client_sock [in]: Client socket FD
 * @return: positive integer on success and -1 otherwise
 */
void oob_connection_server_close(int oob_sock_fd, int oob_client_sock)
{
	if (oob_client_sock > 0)
		close(oob_client_sock);

	if (oob_sock_fd > 0)
		close(oob_sock_fd);
}

/*
 * OOB connection to exchange RDMA info - client side
 *
 * @server_ip [in]: Server IP address to connect
 * @oob_sock_fd [out]: Socket FD
 * @return: positive integer on success and -1 otherwise
 */
int oob_connection_client_setup(const char *server_ip, int *oob_sock_fd)
{
	struct sockaddr_in server_addr = {0};
	int oob_sock_fd_;

	/* Create socket */
	oob_sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (oob_sock_fd_ < 0) {
		DOCA_LOG_ERR("Unable to create socket");
		return -1;
	}
	DOCA_LOG_INFO("Socket created successfully");

	/* Set port and IP the same as server-side: */
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(2000);
	server_addr.sin_addr.s_addr = inet_addr(server_ip);

	/* Send connection request to server: */
	if (connect(oob_sock_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		close(oob_sock_fd_);
		DOCA_LOG_ERR("Unable to connect to server at %s", server_ip);
		return -1;
	}
	DOCA_LOG_INFO("Connected with server successfully");

	*oob_sock_fd = oob_sock_fd_;
	return 0;
}

/*
 * OOB connection to exchange RDMA info - client side closure
 *
 * @oob_sock_fd [in]: Socket FD
 * @return: positive integer on success and -1 otherwise
 */
void oob_connection_client_close(int oob_sock_fd)
{
	if (oob_sock_fd > 0)
		close(oob_sock_fd);
}

/*
 * Wrapper to fix const type of doca_rdma_cap_task_write_is_supported
 *
 * @devinfo [in]: RDMA device info
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t wrapper_doca_rdma_cap_task_write_is_supported(struct doca_devinfo *devinfo)
{
	return doca_rdma_cap_task_write_is_supported((const struct doca_devinfo *)devinfo);
}

/*
 * Create and initialize DOCA RDMA resources
 *
 * @cfg [in]: Configuration parameters
 * @rdma_permissions [in]: Access permission flags for DOCA RDMA
 * @resources [in/out]: DOCA RDMA resources to create
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_rdma_resources(struct rdma_config *cfg,
				   const uint32_t rdma_permissions,
				   struct rdma_resources *resources)
{
	doca_error_t result, tmp_result;
	int ret;
	/* The --in-memory option allows to run DPDK in non-privileged mode */
	char *eal_param[4] = {"", "-a", "00:00.0", "--in-memory"};

	resources->cfg = cfg;

	/* Open DOCA device */
	result = open_doca_device_with_ibdev_name((const uint8_t *)(cfg->device_name),
						  strlen(cfg->device_name),
						  wrapper_doca_rdma_cap_task_write_is_supported,
						  &(resources->doca_device));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_error_get_descr(result));
		return result;
	}

	ret = rte_eal_init(4, eal_param);
	if (ret < 0) {
		DOCA_LOG_ERR("DPDK init failed: %d", ret);
		return DOCA_ERROR_DRIVER;
	}

	if (strcmp(cfg->gpu_pcie_addr, "")) {
		result = doca_gpu_create(cfg->gpu_pcie_addr, &(resources->gpudev));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Function doca_gpu_create returned %s", doca_error_get_descr(result));
			goto close_doca_dev;
		}
	} else {
		resources->gpudev = NULL;
	}

	/* Create DOCA RDMA instance */
	result = doca_rdma_create(resources->doca_device, &(resources->rdma));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA RDMA: %s", doca_error_get_descr(result));
		goto destroy_doca_gpu;
	}

	/* Convert DOCA RDMA to general DOCA context */
	resources->rdma_ctx = doca_rdma_as_ctx(resources->rdma);
	if (resources->rdma_ctx == NULL) {
		result = DOCA_ERROR_UNEXPECTED;
		DOCA_LOG_ERR("Failed to convert DOCA RDMA to DOCA context: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* Set permissions to DOCA RDMA */
	result = doca_rdma_set_permissions(resources->rdma, rdma_permissions);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set permissions to DOCA RDMA: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* Set gid_index to DOCA RDMA if it's provided */
	if (cfg->is_gid_index_set) {
		/* Set gid_index to DOCA RDMA */
		result = doca_rdma_set_gid_index(resources->rdma, cfg->gid_index);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set gid_index to DOCA RDMA: %s", doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}
	}

	result = doca_rdma_set_send_queue_size(resources->rdma, RDMA_SEND_QUEUE_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_rdma_set_send_queue_size on GPU: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* setup datapath of rdma ctx on gpu */
	if (resources->gpudev) {
		result = doca_ctx_set_datapath_on_gpu(resources->rdma_ctx, resources->gpudev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set datapath on GPU: %s", doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}

		resources->pe = NULL;

		result = doca_rdma_set_recv_queue_size(resources->rdma, RDMA_RECV_QUEUE_SIZE);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set doca_rdma_set_recv_queue_size on GPU: %s",
				     doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}
	} else {
		/* If datapath is not on GPU, DOCA imposes the creation of a PE */
		result = doca_pe_create(&(resources->pe));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set permissions to DOCA RDMA: %s", doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}

		result = doca_pe_connect_ctx(resources->pe, resources->rdma_ctx);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set progress engine for RDMA: %s", doca_error_get_descr(result));
			goto destroy_doca_rdma;
		}
	}

	result = doca_rdma_set_grh_enabled(resources->rdma, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set ghr for RDMA: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	/* Start RDMA context */
	result = doca_ctx_start(resources->rdma_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start RDMA context: %s", doca_error_get_descr(result));
		goto destroy_doca_rdma;
	}

	return DOCA_SUCCESS;

destroy_doca_rdma:
	/* Destroy DOCA RDMA */
	tmp_result = doca_rdma_destroy(resources->rdma);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA: %s", doca_error_get_descr(tmp_result));

	if (resources->pe) {
		/* Destroy DOCA progress engine */
		tmp_result = doca_pe_destroy(resources->pe);
		if (tmp_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy DOCA progress engine: %s", doca_error_get_descr(tmp_result));
			DOCA_ERROR_PROPAGATE(result, tmp_result);
		}
	}

destroy_doca_gpu:
	/* Close DOCA GPU device */
	if (resources->gpudev) {
		tmp_result = doca_gpu_destroy(resources->gpudev);
		if (tmp_result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to close DOCA GPU device: %s", doca_error_get_descr(tmp_result));
	}

close_doca_dev:
	/* Close DOCA device */
	tmp_result = doca_dev_close(resources->doca_device);
	if (tmp_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(tmp_result));

	return result;
}

/*
 * Destroy DOCA RDMA resources
 *
 * @resources [in]: DOCA RDMA resources to destroy
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_rdma_resources(struct rdma_resources *resources)
{
	doca_error_t result;

	result = doca_ctx_stop(resources->rdma_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to stop RDMA context: %s", doca_error_get_descr(result));

	/* Destroy DOCA RDMA */
	result = doca_rdma_destroy(resources->rdma);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy DOCA RDMA: %s", doca_error_get_descr(result));

	/* Destroy DOCA progress engine */
	if (resources->pe) {
		result = doca_pe_destroy(resources->pe);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy DOCA progress engine: %s", doca_error_get_descr(result));
	}

	/* Close DOCA device */
	result = doca_dev_close(resources->doca_device);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(result));

	if (resources->gpudev) {
		result = doca_gpu_destroy(resources->gpudev);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to close DOCA device: %s", doca_error_get_descr(result));
	}

	return result;
}

/*
 * Create a DOCA mmap object
 *
 * @mmap_obj [in]: mmap object to populate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_mmap(struct rdma_mmap_obj *mmap_obj)
{
	/* setup mmap */
	doca_error_t result, result2;

	result = doca_mmap_create(&(mmap_obj->mmap));
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_mmap_set_permissions(mmap_obj->mmap, mmap_obj->permissions);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_mmap_set_memrange(mmap_obj->mmap, mmap_obj->memrange_addr, mmap_obj->memrange_len);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_mmap_add_dev(mmap_obj->mmap, mmap_obj->doca_device);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_mmap_start(mmap_obj->mmap);
	if (result != DOCA_SUCCESS)
		goto error;

	/* export mmap for rdma */
	result = doca_mmap_export_rdma(mmap_obj->mmap,
				       mmap_obj->doca_device,
				       &(mmap_obj->rdma_export),
				       &(mmap_obj->export_len));
	if (result != DOCA_SUCCESS)
		goto error;

	return result;

error:
	result2 = doca_mmap_destroy(mmap_obj->mmap);
	if (result2 != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to call doca_mmap_destroy: %s", doca_error_get_descr(result2));

	return result;
}

/*
 * Create a buffer array on GPU
 *
 * @buf_arr_obj [in]: buffer array object to populate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t create_buf_arr_on_gpu(struct buf_arr_obj *buf_arr_obj)
{
	/* setup buf array */
	doca_error_t result, result2;

	result = doca_buf_arr_create(buf_arr_obj->num_elem, &(buf_arr_obj->buf_arr));
	if (result != DOCA_SUCCESS)
		return result;

	result = doca_buf_arr_set_params(buf_arr_obj->buf_arr, buf_arr_obj->mmap, buf_arr_obj->elem_size, 0);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_buf_arr_set_target_gpu(buf_arr_obj->buf_arr, buf_arr_obj->gpudev);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_buf_arr_start(buf_arr_obj->buf_arr);
	if (result != DOCA_SUCCESS)
		goto error;

	result = doca_buf_arr_get_gpu_handle(buf_arr_obj->buf_arr, &(buf_arr_obj->gpu_buf_arr));
	if (result != DOCA_SUCCESS)
		goto error;

	return result;
error:
	result2 = doca_buf_arr_destroy(buf_arr_obj->buf_arr);
	if (result2 != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to call doca_buf_arr_destroy: %s", doca_error_get_descr(result2));

	return result;
}
