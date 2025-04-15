/*
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES.
 * Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Source file for host part of packet processing sample.
 * Contain functions for parsing input parameters, allocating and freeing resources,
 * initialization of a process and a event handler, and running event handler.
 */

/* Used for geteuid function. */
#include <unistd.h>

/* Used for host (x86/DPU) memory allocations. */
#include <malloc.h>

/* Used for IBV device operations. */
#include <infiniband/mlx5dv.h>

/* Flex IO SDK host side version API header. */
#include <libflexio/flexio_ver.h>

/* Set current version of FLEXIO_VER_USED. */
#define FLEXIO_VER_USED FLEXIO_VER(25, 1, 0)

/* Flex IO SDK host side API header. */
#include <libflexio/flexio.h>

/* Flow steering utilities helper header. */
#include "flow_steering_utils.h"

/* Common header for communication between host and DPA. */
#include "../flexio_packet_processor_com.h"

/* Flex IO packet processor device (DPA) side function stub. */
extern flexio_func_t flexio_pp_dev;

/* Application context struct holding necessary host side variables */
struct app_context {
	/* Flex IO process is used to load a program to the DPA. */
	struct flexio_process *flexio_process;
	/* Flex IO application to load to the process. */
	struct flexio_app *flexio_app;
	/* Flex IO message stream is used to get messages from the DPA. */
	struct flexio_msg_stream *stream;
	/* Flex IO event handler is used to execute code over the DPA. */
	struct flexio_event_handler *pp_eh;
	/* Flex IO SQ's CQ. */
	struct flexio_cq *flexio_sq_cq_ptr;
	/* Flex IO SQ. */
	struct flexio_sq *flexio_sq_ptr;
	/* Flex IO RQ's CQ. */
	struct flexio_cq *flexio_rq_cq_ptr;
	/* Flex IO RQ. */
	struct flexio_rq *flexio_rq_ptr;
	/* DPA user access register (DPA UAR) for all application's queues.
	 * Will be set to the Flex IO process UAR.
	 */
	struct flexio_uar *process_uar;
	/* Memory key (MKey) for SQ data. */
	struct flexio_mkey *sqd_mkey;
	/* MKey for RQ data. */
	struct flexio_mkey *rqd_mkey;

	/* Protection domain (PD) for all application's queues.
	 * Will be set to the Flex IO process PD.
	 */
	struct ibv_pd *process_pd;
	/* IBV context opened for the device name provided by the user. */
	struct ibv_context *ibv_ctx;

	/* RX flow matcher. */
	struct flow_matcher *rx_matcher;
	/* RX flow rule for matching incoming RX packets to the Flex IO RQ. */
	struct flow_rule *rx_rule;
	/* TX flow matcher. */
	struct flow_matcher *tx_matcher;
	/* TX flow rule for forwarding outgoing TX packets to the SWS rule table. */
	struct flow_rule *tx_rule_table;
	/* TX flow rule for forwarding outgoing TX packets to the vport (wire). */
	struct flow_rule *tx_rule_vport;

	/* Transfer structs with information to pass to DPA side.
	 * The structs are defined by a common header which both sides may use.
	 */
	/* SQ's CQ transfer information. */
	struct app_transfer_cq sq_cq_transf;
	/* SQ transfer information. */
	struct app_transfer_wq sq_transf;
	/* RQ's CQ transfer information. */
	struct app_transfer_cq rq_cq_transf;
	/* RQ transfer information. */
	struct app_transfer_wq rq_transf;

	/* DPA heap memory address of application information struct.
	 * Invoked event handler will get this as argument and parse it to the application
	 * information struct.
	 */
	flexio_uintptr_t app_data_daddr;
};

/* Open ibv device
 * Returns 0 on success and -1 if the destroy was failed.
 * app_ctx - app_ctx - pointer to app_context structure.
 * device - device name to open.
 */
static int app_open_ibv_ctx(struct app_context *app_ctx, char *device)
{
	/* Queried IBV device list. */
	struct ibv_device **dev_list;
	/* Fucntion return value. */
	int ret = 0;
	/* IBV device iterator. */
	int dev_i;

	/* Query IBV devices list. */
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		printf("Failed to get IB devices list\n");
		return -1;
	}

	/* Loop over found IBV devices. */
	for (dev_i = 0; dev_list[dev_i]; dev_i++) {
		/* Look for a device with the user provided name. */
		if (!strcmp(ibv_get_device_name(dev_list[dev_i]), device))
			break;
	}

	/* Check a device was found. */
	if (!dev_list[dev_i]) {
		printf("No IBV device found for device name '%s'\n", device);
		ret = -1;
		goto cleanup;
	}

	/* Open IBV device context for the requested device. */
	app_ctx->ibv_ctx = ibv_open_device(dev_list[dev_i]);
	if (!app_ctx->ibv_ctx) {
		printf("Couldn't open an IBV context for device '%s'\n", device);
		ret = -1;
	}

cleanup:
	/* Free queried IBV devices list. */
	ibv_free_device_list(dev_list);

	return ret;
}

/* Convert logarithm to value */
#define L2V(l) (1UL << (l))
/* Number of entries in each RQ/SQ/CQ is 2^LOG_Q_DEPTH. */
#define LOG_Q_DEPTH 7
#define Q_DEPTH L2V(LOG_Q_DEPTH)
/* SQ/RQ data entry byte size is 512B (enough for packet data in this case). */
#define LOG_Q_DATA_ENTRY_BSIZE 11
/* SQ/RQ data entry byte size log to value. */
#define Q_DATA_ENTRY_BSIZE L2V(LOG_Q_DATA_ENTRY_BSIZE)
/* SQ/RQ DATA byte size is queue depth times entry byte size. */
#define Q_DATA_BSIZE Q_DEPTH *Q_DATA_ENTRY_BSIZE

/* Creates an MKey with proper permissions for access from DPA.
 * For this application, we only need memory write access.
 * Returns pointer to flexio_mkey structure on success. Otherwise, returns NULL.
 * app_ctx - pointer to app_context structure.
 * daddr - address of MKEY data.
 */
static struct flexio_mkey *create_dpa_mkey(struct app_context *app_ctx, flexio_uintptr_t daddr)
{
	/* Flex IO MKey attributes. */
	struct flexio_mkey_attr mkey_attr = {0};
	/* Flex IO MKey. */
	struct flexio_mkey *mkey;

	/* Set MKey protection domain (PD) to the Flex IO process PD. */
	mkey_attr.pd = app_ctx->process_pd;
	/* Set MKey address. */
	mkey_attr.daddr = daddr;
	/* Set MKey length. */
	mkey_attr.len = Q_DATA_BSIZE;
	/* Set MKey access to memory write (from DPA). */
	mkey_attr.access = IBV_ACCESS_LOCAL_WRITE;
	/* Create Flex IO MKey. */
	if (flexio_device_mkey_create(app_ctx->flexio_process, &mkey_attr, &mkey)) {
		printf("Failed to create Flex IO Mkey\n");
		return NULL;
	}

	return mkey;
}

/* Source MAC address to match for incoming packets. */
#define SMAC 0x02427e7feb02
/* Creates steering rules for application.
 * Returns 0 on success and -1 if the allocation was failed.
 * app_ctx - pointer to app_context structure.
 * nic_mode - if set to 1, the sample runs on ConnectX part.
 */
static int create_steering_rules(struct app_context *app_ctx, int nic_mode)
{
	/* Create RX flow matcher. */
	app_ctx->rx_matcher = create_matcher_rx(app_ctx->ibv_ctx);
	if (!app_ctx->rx_matcher) {
		printf("Failed to create RX matcher\n");
		return -1;
	}

	/* Create RX flow rule for the specific source MAC of incoming packets. */
	app_ctx->rx_rule =
		create_rule_rx_mac_match(app_ctx->rx_matcher,
					 flexio_rq_get_tir(app_ctx->flexio_rq_ptr), SMAC);
	if (!app_ctx->rx_rule) {
		printf("Failed to create RX steering rule\n");
		return -1;
	}

	/* If the sample runs on NIC, the outgoing rule is already configured.
	 * If the sample runs on DPU, the outgoing rule is configured as a DROP rule,
	 * so the sample needs to reconfigure the outgoing rule.
	 */
	if (!nic_mode) {
		/* Add a rule to steer outgoing traffic to the vport for it to exit from
		 * the DPU to the wire.
		 */
		/* Create TX flow matcher. */
		app_ctx->tx_matcher = create_matcher_tx(app_ctx->ibv_ctx);
		if (!app_ctx->tx_matcher) {
			printf("Failed to create TX matcher\n");
			return -1;
		}

		/* Create a TX flow rule to forward outgoing packets to SW steering table. */
		app_ctx->tx_rule_table = create_rule_tx_fwd_to_sws_table(app_ctx->tx_matcher, SMAC);
		if (!app_ctx->tx_rule_table) {
			printf("Failed to create TX table steering rule\n");
			return -1;
		}

		/* Create a TX flow rule to forward outgoing packets to vport (wire). */
		app_ctx->tx_rule_vport = create_rule_tx_fwd_to_vport(app_ctx->tx_matcher, SMAC);
		if (!app_ctx->tx_rule_vport) {
			printf("Failed to create TX vport steering rule\n");
			return -1;
		}
	}

	return 0;
}

/* CQE size is 64B */
#define CQE_BSIZE 64
#define CQ_BSIZE (Q_DEPTH * CQE_BSIZE)
/* Allocate and initialize DPA heap memory for CQ.
 * Returns 0 on success and -1 if the allocation fails.
 * process - pointer to the previously allocated process information.
 * cq_transf - structure with allocated DPA buffers for CQ.
 */
static int cq_mem_alloc(struct flexio_process *process, struct app_transfer_cq *cq_transf)
{
	/* Pointer to the CQ ring source memory on the host (to copy). */
	struct mlx5_cqe64 *cq_ring_src;
	/* Temp pointer to an iterator for CQE initialization. */
	struct mlx5_cqe64 *cqe;

	/* DBR source memory on the host (to copy). */
	__be32 dbr[2] = { 0, 0 };
	/* Function return value. */
	int ret = 0;
	/* Iterator for CQE initialization. */
	uint32_t i;

	/* Allocate and initialize CQ DBR memory on the DPA heap memory. */
	if (flexio_copy_from_host(process, dbr, sizeof(dbr), &cq_transf->cq_dbr_daddr)) {
		printf("Failed to allocate CQ DBR memory on DPA heap.\n");
		return -1;
	}

	/* Allocate memory for the CQ ring on the host. */
	cq_ring_src = calloc(Q_DEPTH, CQE_BSIZE);
	if (!cq_ring_src) {
		printf("Failed to allocate memory for cq_ring_src.\n");
		return -1;
	}

	/* Init CQEs and set ownership bit. */
	for (i = 0, cqe = cq_ring_src; i < Q_DEPTH; i++)
		mlx5dv_set_cqe_owner(cqe++, 1);

	/* Allocate and copy the initialized CQ ring from host to DPA heap memory. */
	if (flexio_copy_from_host(process, cq_ring_src, CQ_BSIZE, &cq_transf->cq_ring_daddr)) {
		printf("Failed to allocate CQ ring memory on DPA heap.\n");
		ret = -1;
	}

	/* Free CQ ring source memory from host once copied to DPA. */
	free(cq_ring_src);

	return ret;
}

/* SQ WQE byte size is 64B. */
#define LOG_SQ_WQE_BSIZE 6
/* SQ WQE byte size log to value. */
#define SQ_WQE_BSIZE L2V(LOG_SQ_WQE_BSIZE)
/* SQ ring byte size is queue depth times WQE byte size. */
#define SQ_RING_BSIZE (Q_DEPTH * SQ_WQE_BSIZE)
/* Allocate DPA heap memory for SQ.
 * Returns 0 on success and -1 if the allocation fails.
 * process - pointer to the previously allocated process info.
 * sq_transf - structure with allocated DPA buffers for SQ.
 */
static int sq_mem_alloc(struct flexio_process *process, struct app_transfer_wq *sq_transf)
{
	/* Allocate DPA heap memory for SQ data. */
	flexio_buf_dev_alloc(process, Q_DATA_BSIZE, &sq_transf->wqd_daddr);
	if (!sq_transf->wqd_daddr)
		return -1;

	/* Allocate DPA heap memory for SQ ring. */
	flexio_buf_dev_alloc(process, SQ_RING_BSIZE, &sq_transf->wq_ring_daddr);
	if (!sq_transf->wq_ring_daddr)
		return -1;

	return 0;
}

/* Create an SQ over the DPA for sending packets from DPA to wire.
 * A CQ is also created for the SQ.
 * Returns 0 on success and -1 if the allocation fails.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int create_app_sq(struct app_context *app_ctx)
{
	/* Pointer to the application Flex IO process (ease of use). */
	struct flexio_process *app_fp = app_ctx->flexio_process;
	/* Attributes for the SQ's CQ. */
	struct flexio_cq_attr sqcq_attr = {0};
	/* Attributes for the SQ. */
	struct flexio_wq_attr sq_attr = {0};

	/* UAR ID for CQ/SQ from Flex IO process UAR. */
	uint32_t uar_id = flexio_uar_get_id(app_ctx->process_uar);
	/* SQ's CQ number. */
	uint32_t cq_num;

	/* Allocate CQ memory (ring and DBR) on DPA heap memory. */
	if (cq_mem_alloc(app_fp, &app_ctx->sq_cq_transf)) {
		printf("Failed to alloc memory for SQ's CQ.\n");
		return -1;
	}

	/* Set CQ depth (log) attribute. */
	sqcq_attr.log_cq_depth = LOG_Q_DEPTH;
	/* Set CQ element type attribute to 'non DPA CQ'.
	 * This means this CQ will not be attached to an event handler.
	 */
	sqcq_attr.element_type = FLEXIO_CQ_ELEMENT_TYPE_NON_DPA_CQ;
	/* Set CQ UAR ID attribute to the Flex IO process UAR ID.
	 * This will allow updating/arming the CQ from the DPA side.
	 */
	sqcq_attr.uar_id = uar_id;
	/* Set CQ DBR memory. DBR memory is on the DPA side in order to allow direct access from
	 * DPA.
	 */
	sqcq_attr.cq_dbr_daddr = app_ctx->sq_cq_transf.cq_dbr_daddr;
	/* Set CQ ring memory. Ring memory is on the DPA side in order to allow reading CQEs from
	 * DPA during packet forwarding.
	 */
	sqcq_attr.cq_ring_qmem.daddr = app_ctx->sq_cq_transf.cq_ring_daddr;
	/* Create CQ for SQ. */
	if (flexio_cq_create(app_fp, NULL, &sqcq_attr, &app_ctx->flexio_sq_cq_ptr)) {
		printf("Failed to create Flex IO CQ\n");
		return -1;
	}

	/* Fetch SQ's CQ number to communicate to DPA side. */
	cq_num = flexio_cq_get_cq_num(app_ctx->flexio_sq_cq_ptr);
	/* Set SQ's CQ number in communication struct. */
	app_ctx->sq_cq_transf.cq_num = cq_num;
	/* Set SQ's CQ depth in communication struct. */
	app_ctx->sq_cq_transf.log_cq_depth = LOG_Q_DEPTH;
	/* Allocate SQ memory (ring and data) on DPA heap memory. */
	if (sq_mem_alloc(app_fp, &app_ctx->sq_transf)) {
		printf("Failed to allocate memory for SQ\n");
		return -1;
	}

	/* Set SQ depth (log) attribute. */
	sq_attr.log_wq_depth = LOG_Q_DEPTH;
	/* Set SQ UAR ID attribute to the Flex IO process UAR ID.
	 * This will allow writing doorbells to the SQ from the DPA side.
	 */
	sq_attr.uar_id = uar_id;
	/* Set SQ ring memory. Ring memory is on the DPA side in order to allow writing WQEs from
	 * DPA during packet forwarding.
	 */
	sq_attr.wq_ring_qmem.daddr = app_ctx->sq_transf.wq_ring_daddr;

	/* Set SQ protection domain */
	sq_attr.pd = app_ctx->process_pd;

	/* Create SQ.
	 * Second argument is NULL as SQ is created on the same GVMI as the process.
	 */
	if (flexio_sq_create(app_fp, NULL, cq_num, &sq_attr, &app_ctx->flexio_sq_ptr)) {
		printf("Failed to create Flex IO SQ\n");
		return -1;
	}

	/* Fetch SQ's number to communicate to DPA side. */
	app_ctx->sq_transf.wq_num = flexio_sq_get_wq_num(app_ctx->flexio_sq_ptr);

	/* Create an MKey for SQ data buffer to send. */
	app_ctx->sqd_mkey = create_dpa_mkey(app_ctx, app_ctx->sq_transf.wqd_daddr);
	if (!app_ctx->sqd_mkey) {
		printf("Failed to create an MKey for SQ data buffer\n");
		return -1;
	}
	/* Set SQ's data buffer MKey ID in communication struct. */
	app_ctx->sq_transf.wqd_mkey_id = flexio_mkey_get_id(app_ctx->sqd_mkey);

	return 0;
}

/* RQ WQE byte size is 64B. */
#define LOG_RQ_WQE_BSIZE 4
/* RQ WQE byte size log to value. */
#define RQ_WQE_BSIZE L2V(LOG_RQ_WQE_BSIZE)
/* RQ ring byte size is queue depth times WQE byte size. */
#define RQ_RING_BSIZE Q_DEPTH *RQ_WQE_BSIZE
/* Allocate DPA heap memory for SQ.
 * Returns 0 on success and -1 if the allocation fails.
 * process - pointer to the previously allocated process info.
 * rq_transf - structure with allocated DPA buffers for RQ.
 */
static int rq_mem_alloc(struct flexio_process *process, struct app_transfer_wq *rq_transf)
{
	/* DBR source memory on the host (to copy). */
	__be32 dbr[2] = { 0, 0 };

	/* Allocate DPA heap memory for RQ data. */
	flexio_buf_dev_alloc(process, Q_DATA_BSIZE, &rq_transf->wqd_daddr);
	if (!rq_transf->wqd_daddr)
		return -1;

	/* Allocate DPA heap memory for RQ ring. */
	flexio_buf_dev_alloc(process, RQ_RING_BSIZE, &rq_transf->wq_ring_daddr);
	if (!rq_transf->wq_ring_daddr)
		return -1;

	/* Allocate and initialize RQ DBR memory on the DPA heap memory. */
	flexio_copy_from_host(process, dbr, sizeof(dbr), &rq_transf->wq_dbr_daddr);
	if (!rq_transf->wq_dbr_daddr)
		return -1;

	return 0;
}

/* Initialize an RQ ring memory over the DPA heap memory.
 * RQ WQEs need to be initialized (produced) by SW so they are ready for incoming packets.
 * The WQEs are initialized over temporary host memory and then copied to the DPA.
 * Returns 0 on success and -1 if the allocation fails.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int init_dpa_rq_ring(struct app_context *app_ctx)
{
	/* RQ WQE data iterator. */
	flexio_uintptr_t wqe_data_daddr = app_ctx->rq_transf.wqd_daddr;
	/* RQ ring MKey. */
	uint32_t mkey_id = app_ctx->rq_transf.wqd_mkey_id;
	/* Temporary host memory for RQ ring. */
	struct mlx5_wqe_data_seg *rx_wqes;
	/* RQ WQE iterator. */
	struct mlx5_wqe_data_seg *dseg;
	/* Function return value. */
	int retval = 0;
	/* RQ WQE index iterator. */
	uint32_t i;

	/* Allocate temporary host memory for RQ ring.*/
	rx_wqes = calloc(1, RQ_RING_BSIZE);
	if (!rx_wqes) {
		printf("Failed to allocate memory for rx_wqes\n");
		return -1;
	}

	/* Initialize RQ WQEs'. */
	for (i = 0, dseg = rx_wqes; i < Q_DEPTH; i++, dseg++) {
		/* Set WQE's data segment to point to the relevant RQ data segment. */
		mlx5dv_set_data_seg(dseg, Q_DATA_ENTRY_BSIZE, mkey_id, wqe_data_daddr);
		/* Advance data pointer to next segment. */
		wqe_data_daddr += Q_DATA_ENTRY_BSIZE;
	}

	/* Copy RX WQEs from host to RQ ring DPA heap memory. */
	if (flexio_host2dev_memcpy(app_ctx->flexio_process, rx_wqes, RQ_RING_BSIZE,
				   app_ctx->rq_transf.wq_ring_daddr)) {
		retval = -1;
	}

	/* Free temporary host memory. */
	free(rx_wqes);
	return retval;
}

/* Initialize RQ's DBR.
 * Recieve counter need to be set to number of produces WQEs.
 * Returns 0 on success and -1 if the allocation fails.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int init_rq_dbr(struct app_context *app_ctx)
{
	/* Temporary host memory for DBR value. */
	__be32 dbr[2];

	/* Set receiver counter to number of WQEs. */
	dbr[0] = htobe32(Q_DEPTH & 0xffff);
	/* Send counter is not used for RQ so it is nullified. */
	dbr[1] = htobe32(0);
	/* Copy DBR value to DPA heap memory.*/
	if (flexio_host2dev_memcpy(app_ctx->flexio_process, dbr, sizeof(dbr),
				   app_ctx->rq_transf.wq_dbr_daddr)) {
		return -1;
	}

	return 0;
}

/* Create an RQ over the DPA for receiving packets on DPA.
 * A CQ is also created for the RQ.
 * Returns 0 on success and -1 if the allocation fails.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int create_app_rq(struct app_context *app_ctx)
{
	/* Pointer to the application Flex IO process (ease of use). */
	struct flexio_process *app_fp = app_ctx->flexio_process;
	/* Attributes for the RQ's CQ. */
	struct flexio_cq_attr rqcq_attr = {0};
	/* Attributes for the RQ. */
	struct flexio_wq_attr rq_attr = {0};

	/* UAR ID for CQ/SQ from Flex IO process UAR. */
	uint32_t uar_id = flexio_uar_get_id(app_ctx->process_uar);
	/* RQ's CQ number. */
	uint32_t cq_num;

	/* Allocate CQ memory (ring and DBR) on DPA heap memory. */
	if (cq_mem_alloc(app_fp, &app_ctx->rq_cq_transf)) {
		printf("Failed to alloc memory for RQ's CQ.\n");
		return -1;
	}

	/* Set CQ depth (log) attribute. */
	rqcq_attr.log_cq_depth = LOG_Q_DEPTH;
	/* Set CQ element type attribute to 'DPA thread'.
	 * This means that a CQE on this CQ will trigger the connetced DPA thread.
	 * This will be used for running the DPA program for each incoming packet on the RQ.
	 */
	rqcq_attr.element_type = FLEXIO_CQ_ELEMENT_TYPE_DPA_THREAD;
	/* Set CQ thread to the application event handler's thread. */
	rqcq_attr.thread = flexio_event_handler_get_thread(app_ctx->pp_eh);
	/* Set CQ UAR ID attribute to the Flex IO process UAR ID.
	 * This will allow updating/arming the CQ from the DPA side.
	 */
	rqcq_attr.uar_id = uar_id;
	/* Set CQ DBR memory. DBR memory is on the DPA side in order to allow direct access from
	 * DPA.
	 */
	rqcq_attr.cq_dbr_daddr = app_ctx->rq_cq_transf.cq_dbr_daddr;
	/* Set CQ ring memory. Ring memory is on the DPA side in order to allow reading CQEs from
	 * DPA during packet forwarding.
	 */
	rqcq_attr.cq_ring_qmem.daddr = app_ctx->rq_cq_transf.cq_ring_daddr;
	/* Create CQ for RQ. */
	if (flexio_cq_create(app_fp, NULL, &rqcq_attr, &app_ctx->flexio_rq_cq_ptr)) {
		printf("Failed to create Flex IO CQ\n");
		return -1;
	}

	/* Fetch SQ's CQ number to communicate to DPA side. */
	cq_num = flexio_cq_get_cq_num(app_ctx->flexio_rq_cq_ptr);
	/* Set RQ's CQ number in communication struct. */
	app_ctx->rq_cq_transf.cq_num = cq_num;
	/* Set RQ's CQ depth in communication struct. */
	app_ctx->rq_cq_transf.log_cq_depth = LOG_Q_DEPTH;
	/* Allocate RQ memory (ring and data) on DPA heap memory. */
	if (rq_mem_alloc(app_fp, &app_ctx->rq_transf)) {
		printf("Failed to allocate memory for RQ.\n");
		return -1;
	}

	/* Create an MKey for RX buffer */
	app_ctx->rqd_mkey = create_dpa_mkey(app_ctx, app_ctx->rq_transf.wqd_daddr);
	if (!app_ctx->rqd_mkey) {
		printf("Failed to create an MKey for RQ data buffer.\n");
		return -1;
	}
	/* Set SQ's data buffer MKey ID in communication struct. */
	app_ctx->rq_transf.wqd_mkey_id = flexio_mkey_get_id(app_ctx->rqd_mkey);
	/* Initialize RQ ring. */
	if (init_dpa_rq_ring(app_ctx)) {
		printf("Failed to init RQ ring.\n");
		return -1;
	}

	/* Set RQ depth (log) attribute. */
	rq_attr.log_wq_depth = LOG_Q_DEPTH;
	/* Set RQ protection domain attribute to be the same as the Flex IO process. */
	rq_attr.pd = app_ctx->process_pd;
	/* Set RQ DBR memory type to DPA heap memory. */
	rq_attr.wq_dbr_qmem.memtype = FLEXIO_MEMTYPE_DPA;
	/* Set RQ DBR memory address. */
	rq_attr.wq_dbr_qmem.daddr = app_ctx->rq_transf.wq_dbr_daddr;
	/* Set RQ ring memory address. */
	rq_attr.wq_ring_qmem.daddr = app_ctx->rq_transf.wq_ring_daddr;
	/* Create the Flex IO RQ.
	 * Second argument is NULL as RQ is created on the same GVMI as the process.
	 */
	if (flexio_rq_create(app_fp, NULL, cq_num, &rq_attr, &app_ctx->flexio_rq_ptr)) {
		printf("Failed to create Flex IO RQ.\n");
		return -1;
	}

	/* Fetch RQ's number to communicate to DPA side. */
	app_ctx->rq_transf.wq_num = flexio_rq_get_wq_num(app_ctx->flexio_rq_ptr);
	if (init_rq_dbr(app_ctx)) {
		printf("Failed to init RQ DBR.\n");
		return -1;
	}

	return 0;
}

/* Creates a Flex IO SDK event handler.
 * The event handler is used for setting a function in the loaded program to run once
 * a proper trigger happens (CQE on the relevant CQ).
 * Returns 0 on success and -1 if the allocation fails.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int create_app_event_handler(struct app_context *app_ctx)
{
	/* Event handler creation attributes. */
	struct flexio_event_handler_attr eh_attr = {0};

	/* Set function stub to the stub created by DPACC and declared in the host application. */
	eh_attr.host_stub_func = flexio_pp_dev;
	/* Set execution unit affinity to 'none'.
	 * This will cause the event handler thread to trigger on any free execution unit.
	 * This assumes there's at least one available execution unit in the device default
	 * execution unit group.
	 */
	eh_attr.affinity.type = FLEXIO_AFFINITY_NONE;
	/* Create the Flex IO event handler object. */
	if (flexio_event_handler_create(app_ctx->flexio_process, &eh_attr, &app_ctx->pp_eh)) {
		printf("Failed to create Flex IO event handler\n");
		return -1;
	}

	return 0;
}

/* Copy application information to DPA.
 * DPA side needs queue information in order to process the packets.
 * The DPA heap memory address will be passed as the event handler argument.
 * Returns 0 if success and -1 if the copy failed.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int copy_app_data_to_dpa(struct app_context *app_ctx)
{
	/* Size of application information struct. */
	uint64_t struct_bsize = sizeof(struct host2dev_packet_processor_data);
	/* Temporary application information struct to copy. */
	struct host2dev_packet_processor_data *h2d_data;
	/* Function return value. */
	int ret = 0;

	/* Allocate memory for temporary struct to copy. */
	h2d_data = calloc(1, struct_bsize);
	if (!h2d_data) {
		printf("Failed to allocate memory for h2d_data\n");
		return -1;
	}

	/* Set SQ's CQ information. */
	h2d_data->sq_cq_transf = app_ctx->sq_cq_transf;
	/* Set SQ's information. */
	h2d_data->sq_transf = app_ctx->sq_transf;
	/* Set RQ's CQ information. */
	h2d_data->rq_cq_transf = app_ctx->rq_cq_transf;
	/* Set RQ's information. */
	h2d_data->rq_transf = app_ctx->rq_transf;
	/* Set APP data info for first run. */
	h2d_data->not_first_run = 0;

	/* Copy to DPA heap memory.
	 * Allocated DPA heap memory address will be kept in app_data_daddr.
	 */
	if (flexio_copy_from_host(app_ctx->flexio_process, h2d_data, struct_bsize,
				  &app_ctx->app_data_daddr)) {
		printf("Failed to copy application information to DPA.\n");
		ret = -1;
	}

	/* Free temporary host memory. */
	free(h2d_data);
	return ret;
}

/* Clean up previously allocated rules.
 * Returns 0 on success and -1 if the destroy failed.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int clean_up_rules(struct app_context *app_ctx)
{
	int err = 0;

	/* Clean up rx rule if created */
	if (app_ctx->rx_rule && destroy_rule(app_ctx->rx_rule)) {
		printf("Failed to destroy rx rule\n");
		err = -1;
	}

	/* Clean up rx matcher if created */
	if (app_ctx->rx_matcher && destroy_matcher(app_ctx->rx_matcher)) {
		printf("Failed to destroy rx matcher\n");
		err = -1;
	}

	/* Clean up tx rule for vport if created */
	if (app_ctx->tx_rule_vport && destroy_rule(app_ctx->tx_rule_vport)) {
		printf("Failed to destroy tx rule vport\n");
		err = -1;
	}

	/* Clean up tx rule for table if created */
	if (app_ctx->tx_rule_table && destroy_rule(app_ctx->tx_rule_table)) {
		printf("Failed to destroy tx rule\n");
		err = -1;
	}

	/* Clean up tx matcher if created */
	if (app_ctx->tx_matcher && destroy_matcher(app_ctx->tx_matcher)) {
		printf("Failed to destroy tx matcher\n");
		err = -1;
	}

	return err;
}

/* Clean up previously allocated RQ
 * Returns 0 on success and -1 if the destroy failed.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int clean_up_app_rq(struct app_context *app_ctx)
{
	int err = 0;

	/* Clean up rq pointer if created */
	if (app_ctx->flexio_rq_ptr && flexio_rq_destroy(app_ctx->flexio_rq_ptr)) {
		printf("Failed to destroy RQ\n");
		err = -1;
	}

	/* Clean up memory key for rqd if created */
	if (app_ctx->rqd_mkey && flexio_device_mkey_destroy(app_ctx->rqd_mkey)) {
		printf("Failed to destroy mkey RQD\n");
		err = -1;
	}

	/* Clean up app data daddr if created */
	if (app_ctx->rq_transf.wq_dbr_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->rq_transf.wq_dbr_daddr)) {
		printf("Failed to free rq_transf.wq_dbr_daddr\n");
		err = -1;
	}

	/* Clean up wq_ring_daddr for rq_transf if created */
	if (app_ctx->rq_transf.wq_ring_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->rq_transf.wq_ring_daddr)) {
		printf("Failed to free rq_transf.wq_ring_daddr\n");
		err = -1;
	}

	if (app_ctx->rq_transf.wqd_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->rq_transf.wqd_daddr)) {
		printf("Failed to free rq_transf.wqd_daddr\n");
		err = -1;
	}

	if (app_ctx->flexio_rq_cq_ptr && flexio_cq_destroy(app_ctx->flexio_rq_cq_ptr)) {
		printf("Failed to destroy RQ' CQ\n");
		err = -1;
	}

	if (app_ctx->rq_cq_transf.cq_ring_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->rq_cq_transf.cq_ring_daddr)) {
		printf("Failed to free rq_cq_transf.cq_ring_daddr\n");
		err = -1;
	}

	if (app_ctx->rq_cq_transf.cq_dbr_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->rq_cq_transf.cq_dbr_daddr)) {
		printf("Failed to free rq_cq_transf.cq_dbr_daddr\n");
		err = -1;
	}

	return err;
}

/* Clean up previously allocated SQ
 * Returns 0 on success and -1 if the destroy failed.
 * app_ctx - app_ctx - pointer to app_context structure.
 */
static int clean_up_app_sq(struct app_context *app_ctx)
{
	int err = 0;

	if (app_ctx->flexio_sq_ptr && flexio_sq_destroy(app_ctx->flexio_sq_ptr)) {
		printf("Failed to destroy SQ\n");
		err = -1;
	}

	if (app_ctx->sqd_mkey && flexio_device_mkey_destroy(app_ctx->sqd_mkey)) {
		printf("Failed to destroy mkey SQD\n");
		err = -1;
	}

	if (app_ctx->sq_transf.wq_ring_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->sq_transf.wq_ring_daddr)) {
		printf("Failed to free sq_transf.wq_ring_daddr\n");
		err = -1;
	}

	if (app_ctx->sq_transf.wqd_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->sq_transf.wqd_daddr)) {
		printf("Failed to free sq_transf.wqd_daddr\n");
		err = -1;
	}

	if (app_ctx->flexio_sq_cq_ptr && flexio_cq_destroy(app_ctx->flexio_sq_cq_ptr)) {
		printf("Failed to destroy SQ' CQ\n");
		err = -1;
	}

	if (app_ctx->sq_cq_transf.cq_ring_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->sq_cq_transf.cq_ring_daddr)) {
		printf("Failed to free sq_cq_transf.cq_ring_daddr\n");
		err = -1;
	}

	if (app_ctx->sq_cq_transf.cq_dbr_daddr &&
	    flexio_buf_dev_free(app_ctx->flexio_process, app_ctx->sq_cq_transf.cq_dbr_daddr)) {
		printf("Failed to free sq_cq_transf.cq_dbr_daddr\n");
		err = -1;
	}

	return err;
}

/* dev msg stream buffer built from chunks of 2^FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE each */
#define MSG_HOST_BUFF_BSIZE (512 * L2V(FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE))

/* Application name in string format for Flex IO app get. */
#define DEV_APP_NAME_STR(_app_name) #_app_name
#define DEV_APP_NAME_XSTR(_app_name) DEV_APP_NAME_STR(_app_name)

/* Main host side function.
 * Responsible for allocating resources and making preparations for DPA side envocatin.
 */
int main(int argc, char **argv)
{
	/* Flex IO app get selection attributes. */
	struct flexio_app_select_attr flexio_app_sel_attr = {0};
	/* Message stream attributes. */
	flexio_msg_stream_attr_t stream_fattr = {0};
	/* Pointer to the application Flex IO process (ease of use). */
	struct flexio_process *app_fp = NULL;
	/* Application context. */
	struct app_context app_ctx = {0};
	/* Debug token */
	uint64_t udbg_token;
	/* Mode of working - for nic (host) or for dpu (default) */
	int nic_mode = 0;
	/* Buffer for fread */
	char buf[2];
	/* Execution status value. */
	int err;

	printf("Welcome to 'Flex IO SDK packet processing' sample app.\n");

	/* Check input includes a device name. */
	if ((argc < 2) || (argc > 3)) {
		printf("Usage: %s <mlx5 device> [--nic-mode]\n", argv[0]);
		return -1;
	}

	if (argc == 3) {
		if (strcmp(argv[2], "--nic-mode")) {
			printf("Invalid second parameter %s\n", argv[2]);
			return -1;
		}
		nic_mode = 1;
	}

	/* Check if the application run with root privileges */
	if (geteuid()) {
		printf("Failed - the application must run with root privileges\n");
		return -1;
	}

	/* Create an IBV device context by opening the provided IBV device. */
	err = app_open_ibv_ctx(&app_ctx, argv[1]);
	if (err)
		return -1;

	/* Set current version for API. */
	if (flexio_version_set(FLEXIO_VER_USED)) {
		printf("Failed to set version in FlexIO API.\n");
		err = -1;
		goto cleanup;
	}

	/* Get Flex IO application struct for used device. */
	/* Set app name to match. */
	flexio_app_sel_attr.app_name = DEV_APP_NAME_XSTR(DEV_APP_NAME);
	/* Set HW platform to default - this will auto-select the appropriate program. */
	flexio_app_sel_attr.hw_model_id = FLEXIO_HW_MODEL_DEF;
	/* Set IBV device to use for HW model query. */
	flexio_app_sel_attr.ibv_ctx = app_ctx.ibv_ctx;

	/* Get a Flex IO application.
	 * DPACC created Flex IO application per HW model. Match the select attributes to the
	 * found applications and return the matching one. Name must match. HW model is matched
	 * According to the queried HW model for the device. If no exact match is found a program
	 * built to an older HW model will be selected.
	 */
	err = flexio_app_get(&flexio_app_sel_attr, &app_ctx.flexio_app);
	if (err) {
		printf("Failed to get Flex IO app\n");
		goto cleanup;
	}

	/* Create a Flex IO process.
	 * The flexio_app struct is passed to load the program.
	 * No process creation attributes are needed for this application (default outbox).
	 * Created SW struct will be returned through the given pointer.
	 */
	if (flexio_process_create(app_ctx.ibv_ctx, app_ctx.flexio_app, NULL, &app_fp)) {
		printf("Failed to create Flex IO process.\n");
		err = -1;
		goto cleanup;
	}
	app_ctx.flexio_process = app_fp;

	/* Get the token for user debug access to the Flex IO process. */
	udbg_token = flexio_process_udbg_token_get(app_ctx.flexio_process);

	/* If the token is 0, user debug access for the process is not allowed.
	 * If the token is not 0, the user can attach the FlexIO debugger to the process,
	 * set breakpoints, and debug the device application.
	 */
	if (udbg_token)
		printf("Use the token >>> %#lx <<< for debugging\n", udbg_token);

	/* Create a Flex IO message stream for process.
	 * Size of single message stream is MSG_HOST_BUFF_BSIZE.
	 * Working mode is synchronous.
	 * Level of debug in INFO.
	 * Transport mode - QP RC (possible alternatives - QP UC or QP UD)
	 * Output is stdout.
	 */
	stream_fattr.data_bsize = MSG_HOST_BUFF_BSIZE;
	stream_fattr.sync_mode = FLEXIO_MSG_DEV_SYNC_MODE_SYNC;
	stream_fattr.level = FLEXIO_MSG_DEV_INFO;
	stream_fattr.transport_mode = FLEXIO_MSG_TRANSPORT_QP_RC;

	if (flexio_msg_stream_create(app_fp, &stream_fattr, stdout, NULL,
				     &app_ctx.stream)) {
		printf("Failed to init device messaging environment, exiting App\n");
		err = -1;
		goto cleanup;
	}

	app_ctx.process_pd = flexio_process_get_pd(app_fp);
	app_ctx.process_uar = flexio_process_get_uar(app_fp);

	/* Create an event handler. */
	if (create_app_event_handler(&app_ctx)) {
		printf("Failed to create Flex IO event handler.\n");
		err = -1;
		goto cleanup;
	}

	/* Create a Flex IO SQ to send packets from the DPA. */
	if (create_app_sq(&app_ctx)) {
		printf("Failed to create Flex SQ.\n");
		err = -1;
		goto cleanup;
	}

	/* Create a Flex IO RQ to receive packets on the DPA.
	 * CQEs for received packets will trigger the packet processing event handler.
	 */
	if (create_app_rq(&app_ctx)) {
		printf("Failed to create Flex EQ.\n");
		err = -1;
		goto cleanup;
	}

	/* Create steering rules. */
	if (create_steering_rules(&app_ctx, nic_mode)) {
		printf("Failed to create Flex IO steering rules.\n");
		err = -1;
		goto cleanup;
	}

	/* Copy the relevant information to DPA. */
	if (copy_app_data_to_dpa(&app_ctx)) {
		printf("Failed to copy application data to DPA.\n");
		err = -1;
		goto cleanup;
	}

	printf("Ready to receive messages\n");

	/* Start event handler - move from the init state to the running state.
	 * Event handlers in the running state may be invoked by an incoming CQE.
	 * On other states, the invocation is blocked and lost.
	 * Pass the address of common information as a user argument to be used on the DPA side.
	 */
	if (flexio_event_handler_run(app_ctx.pp_eh, app_ctx.app_data_daddr)) {
		printf("Failed to run event handler.\n");
		err = -1;
		goto cleanup;
	}

	/* Wait for Enter - the DPA sample is running in the meanwhile */
	if (!fread(buf, 1, 1, stdin)) {
		printf("Failed in fread\n");
	}

cleanup:
	/* Clean up flow is done in reverse order of creation as there's a refernce system
	 * that won't allow destroying resources that has references to existing resources.
	 */

	/* Clean up app data daddr if created */
	if (app_ctx.app_data_daddr &&
	    flexio_buf_dev_free(app_ctx.flexio_process, app_ctx.app_data_daddr)) {
		printf("Failed to dealloc application data memory on Flex IO heap\n");
		err = -1;
	}

	/* Clean up previously created rules */
	if (clean_up_rules(&app_ctx)) {
		err = -1;
	}

	/* Clean up previously allocated SQ */
	if (clean_up_app_sq(&app_ctx)) {
		err = -1;
	}

	/* Clean up previously allocated RQ */
	if (clean_up_app_rq(&app_ctx)) {
		err = -1;
	}

	/* Destroy event handler if created */
	if (app_ctx.pp_eh &&
	    flexio_event_handler_destroy(app_ctx.pp_eh)) {
		printf("Failed to destroy event handler\n");
		err = -1;
	}

	/* Destroy message stream if created */
	if (app_fp && flexio_msg_stream_destroy(app_ctx.stream)) {
		printf("Failed to destroy device messaging environment\n");
		err = -1;
	}

	/* Destroy the Flex IO process */
	if (flexio_process_destroy(app_fp)) {
		printf("Failed to destroy process.\n");
		err = -1;
	}

	/* Close the IBV device */
	if (ibv_close_device(app_ctx.ibv_ctx)) {
		printf("Failed to close ibv context.\n");
		err = -1;
	}

	return err;
}
