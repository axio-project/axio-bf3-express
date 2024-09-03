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
#include <unistd.h>
#include <assert.h>

#include <rte_ethdev.h>

#include <doca_dpdk.h>
#include <doca_flow.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_eth_rxq.h>
#include <doca_eth_rxq_cpu_data_path.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"
#include "eth_common.h"
#include "eth_rxq_common.h"

DOCA_LOG_REGISTER(ETH_RXQ_REGULAR_RECEIVE);

#define SLEEP_IN_NANOS (10 * 1000)				/* sample the task every 10 microseconds  */
#define MAX_BURST_SIZE 256					/* Max burst size to set for eth_rxq */
#define MAX_PKT_SIZE 1600					/* Max packet size to set for eth_rxq */
#define BUFS_NUM 1						/* Number of DOCA buffers */
#define LOG_TASKS_NUM 1						/* log of tasks number */
#define RECV_TASK_USER_DATA 0x43210				/* User data for receive task */

struct eth_rxq_sample_objects {
	struct eth_core_resources core_resources;		/* A struct to hold ETH core resources */
	struct eth_rxq_flow_resources flow_resources;		/* A struct to hold DOCA flow resources */
	struct doca_eth_rxq *eth_rxq;				/* DOCA ETH RXQ context */
	struct doca_buf *packet_buf;				/* DOCA buffer to contain received packet */
	struct doca_eth_rxq_task_recv *recv_task;		/* Receive task */
	uint32_t inflight_tasks;				/* Inflight tasks count */
	uint16_t rxq_flow_queue_id;				/* DOCA ETH RXQ's flow queue ID */
	uint8_t is_dpdk_initalized;				/* Indicator if DPDK is initalized */
};

/*
 * ETH RXQ receive task common callback
 *
 * @task_recv [in]: Completed task
 * @task_user_data [in]: User provided data, used for identifying the task
 * @ctx_user_data [in]: User provided data, used to store sample state
 */
static void
task_recv_common_cb(struct doca_eth_rxq_task_recv *task_recv,
		    union doca_data task_user_data, union doca_data ctx_user_data)
{
	doca_error_t status, task_status;
	struct doca_buf *pkt;
	size_t packet_size;
	uint32_t *inflight_tasks;

	inflight_tasks = ctx_user_data.ptr;
	(*inflight_tasks)--;
	DOCA_LOG_INFO("Receive task user data is 0x%lx", task_user_data.u64);

	status = doca_eth_rxq_task_recv_get_pkt(task_recv, &pkt);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get packet of a receive task, err: %s", doca_error_get_name(status));
		doca_task_free(doca_eth_rxq_task_recv_as_doca_task(task_recv));
		return;
	}

	task_status = doca_task_get_status(doca_eth_rxq_task_recv_as_doca_task(task_recv));

	if (task_status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to receive a packet, err: %s", doca_error_get_name(task_status));
	} else {
		status = doca_buf_get_data_len(pkt, &packet_size);
		if (status != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to get receive packet size, err: %s", doca_error_get_name(status));
		else
			DOCA_LOG_INFO("Received a packet of size %lu succesfully", packet_size);
	}

	status = doca_buf_dec_refcount(pkt, NULL);
	if (status != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to free packet buf, err: %s", doca_error_get_name(status));

	doca_task_free(doca_eth_rxq_task_recv_as_doca_task(task_recv));
}

/*
 * Destroy ETH RXQ context related resources
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
destroy_eth_rxq_ctx(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;
	enum doca_ctx_states ctx_state;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	status = doca_ctx_stop(state->core_resources.core_objs.ctx);
	if (status == DOCA_ERROR_IN_PROGRESS) {
		while (state->inflight_tasks != 0) {
			(void)doca_pe_progress(state->core_resources.core_objs.pe);
			nanosleep(&ts, &ts);
		}

		status = doca_ctx_get_state(state->core_resources.core_objs.ctx, &ctx_state);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed get status of context, err: %s", doca_error_get_name(status));
			return status;
		}

		status = ctx_state == DOCA_CTX_STATE_IDLE ? DOCA_SUCCESS : DOCA_ERROR_BAD_STATE;
	}

	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop DOCA context, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_eth_rxq_destroy(state->eth_rxq);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy DOCA ETH RXQ context, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy DOCA buffers for the packets
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its packet DOCA buffers
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
destroy_eth_rxq_packet_buffers(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_buf_dec_refcount(state->packet_buf, NULL);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy packet_buf buffer, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to destroy its tasks
 */
static void
destroy_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_task_free(doca_eth_rxq_task_recv_as_doca_task(state->recv_task));
}

/*
 * Retrieve ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to retrieve tasks from
 */
static void
retrieve_rxq_recv_tasks(struct eth_rxq_sample_objects *state)
{
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	while (state->inflight_tasks != 0) {
		(void)doca_pe_progress(state->core_resources.core_objs.pe);
		nanosleep(&ts, &ts);
	}
}

/*
 * Create ETH RXQ context related resources
 *
 * @state [in]: eth_rxq_sample_objects struct to create its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
create_eth_rxq_ctx(struct eth_rxq_sample_objects *state)
{
	doca_error_t status, clean_status;
	union doca_data user_data;

	status = doca_eth_rxq_create(state->core_resources.core_objs.dev, MAX_BURST_SIZE, MAX_PKT_SIZE, &(state->eth_rxq));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ETH RXQ context, err: %s", doca_error_get_name(status));
		return status;
	}

	status = doca_eth_rxq_set_type(state->eth_rxq, DOCA_ETH_RXQ_TYPE_REGULAR);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set type, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_task_recv_set_conf(state->eth_rxq, task_recv_common_cb,
						 task_recv_common_cb, LOG_TASKS_NUM);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set receive task configuration, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	state->core_resources.core_objs.ctx = doca_eth_rxq_as_doca_ctx(state->eth_rxq);
	if (state->core_resources.core_objs.ctx == NULL) {
		DOCA_LOG_ERR("Failed to retrieve DOCA ETH RXQ context as DOCA context, err: %s",
			doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_pe_connect_ctx(state->core_resources.core_objs.pe, state->core_resources.core_objs.ctx);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect PE, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	user_data.ptr = &(state->inflight_tasks);
	status = doca_ctx_set_user_data(state->core_resources.core_objs.ctx, user_data);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set user data for DOCA context, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_ctx_start(state->core_resources.core_objs.ctx);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA context, err: %s", doca_error_get_name(status));
		goto destroy_eth_rxq;
	}

	status = doca_eth_rxq_get_flow_queue_id(state->eth_rxq, &(state->rxq_flow_queue_id));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get flow queue ID of RXQ, err: %s", doca_error_get_name(status));
		goto stop_ctx;
	}

	return DOCA_SUCCESS;
stop_ctx:
	clean_status = doca_ctx_stop(state->core_resources.core_objs.ctx);
	state->core_resources.core_objs.ctx = NULL;

	if (clean_status != DOCA_SUCCESS)
		return status;
destroy_eth_rxq:
	clean_status = doca_eth_rxq_destroy(state->eth_rxq);
	state->eth_rxq = NULL;

	if (clean_status != DOCA_SUCCESS)
		return status;

	return status;
}

/*
 * Initalize DPDK
 *
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
init_dpdk_eal(void)
{
	int res;

	const char *eal_param[3] = {"", "-a", "00:00.0"};

	res = rte_eal_init(3, (char **)eal_param);
	if (res < 0) {
		DOCA_LOG_ERR("Failed to init dpdk port: %s", rte_strerror(-res));
		return DOCA_ERROR_DRIVER;
	}

	return DOCA_SUCCESS;
}

/*
 * Submit ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to submit its tasks
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
submit_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_task_submit(doca_eth_rxq_task_recv_as_doca_task(state->recv_task));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit receive task, err: %s", doca_error_get_name(status));
		return status;
	}

	state->inflight_tasks++;

	return DOCA_SUCCESS;
}

/*
 * Create ETH RXQ tasks
 *
 * @state [in]: eth_rxq_sample_objects struct to create tasks with its ETH RXQ context
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
create_eth_rxq_tasks(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;
	union doca_data user_data;

	user_data.u64 = RECV_TASK_USER_DATA;
	status = doca_eth_rxq_task_recv_allocate_init(state->eth_rxq, user_data, state->packet_buf,
						      &(state->recv_task));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate receive task, err: %s", doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA buffers for the packet
 *
 * @state [in]: eth_rxq_sample_objects struct to create its packet DOCA buffers
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
static doca_error_t
create_eth_rxq_packet_buffer(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;

	status = doca_buf_inventory_buf_get_by_addr(state->core_resources.core_objs.buf_inv, state->core_resources.core_objs.src_mmap,
						    state->core_resources.mmap_addr, MAX_PKT_SIZE, &(state->packet_buf));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create DOCA bufer for ethernet frame, err: %s",
			     doca_error_get_name(status));
		return status;
	}

	return DOCA_SUCCESS;
}

/*
 * Clean sample resources
 *
 * @state [in]: eth_rxq_sample_objects struct to clean
 */
static void
eth_rxq_cleanup(struct eth_rxq_sample_objects *state)
{
	doca_error_t status;
	int res;

	if (state->flow_resources.df_port != NULL) {
		status = destroy_eth_rxq_flow_resources(&(state->flow_resources));
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy DOCA flow resources, err: %s",
				doca_error_get_name(status));
			return;
		}
	}

	if (state->eth_rxq != NULL) {
		status = destroy_eth_rxq_ctx(state);
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy eth_rxq_ctx, err: %s", doca_error_get_name(status));
			return;
		}
	}

	if (state->core_resources.core_objs.dev != NULL) {
		status = destroy_eth_core_resources(&(state->core_resources));
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy core_resources, err: %s", doca_error_get_name(status));
			return;
		}
	}

	if (state->is_dpdk_initalized) {
		res = rte_eal_cleanup();
		if (res != 0) {
			DOCA_LOG_ERR("Failed to destroy dpdk: %s", rte_strerror(-res));
			return;
		}
	}
}

/*
 * Check if device supports needed capabilities
 *
 * @devinfo [in]: Device info for device to check
 * @return: DOCA_SUCCESS in case the device supports needed capabilities and DOCA_ERROR otherwise
 */
static doca_error_t
check_device(struct doca_devinfo *devinfo)
{
	doca_error_t status;
	uint32_t max_supported_burst_size;
	uint16_t max_supported_packet_size;
	uint8_t type_supported;

	status = doca_eth_rxq_get_max_burst_size_supported(devinfo, &max_supported_burst_size);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get supported max burst size, err: %s", doca_error_get_name(status));
		return status;
	}

	if (max_supported_burst_size < MAX_BURST_SIZE)
		return DOCA_ERROR_NOT_SUPPORTED;

	status = doca_eth_rxq_get_max_packet_size_supported(devinfo, &max_supported_packet_size);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get supported max packet size, err: %s", doca_error_get_name(status));
		return status;
	}

	if (max_supported_packet_size < MAX_PKT_SIZE)
		return DOCA_ERROR_NOT_SUPPORTED;

	status = doca_eth_rxq_get_type_supported(devinfo, DOCA_ETH_RXQ_TYPE_REGULAR,
						 DOCA_ETH_RXQ_DATA_PATH_TYPE_CPU, &type_supported);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to check supported type, err: %s", doca_error_get_name(status));
		return status;
	}

	if (type_supported == 0)
		return DOCA_ERROR_NOT_SUPPORTED;

	return DOCA_SUCCESS;
}

/*
 * Run ETH RXQ regular mode receive
 *
 * @ib_dev_name [in]: IB device name of a doca device
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise
 */
doca_error_t
eth_rxq_regular_receive(const char *ib_dev_name)
{
	doca_error_t status, clean_status;
	struct eth_rxq_sample_objects state;
	struct eth_core_config cfg = {
		.mmap_size = MAX_PKT_SIZE * BUFS_NUM,
		.inventory_num_elements = BUFS_NUM,
		.check_device = check_device,
		.ibdev_name = ib_dev_name
	};
	struct eth_rxq_flow_config flow_cfg = {
		.dpdk_port_id = 0,
		.rxq_flow_queue_id = 0
	};

	memset(&state, 0, sizeof(state));
	status = init_dpdk_eal();
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DPDK, err: %s", doca_error_get_name(status));
		return status;
	}

	state.is_dpdk_initalized = true;

	status = allocate_eth_core_resources(&cfg, &(state.core_resources));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed allocate core resources, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = init_dpdk_port(state.core_resources.core_objs.dev, &(flow_cfg.dpdk_port_id));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DPDK port, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_ctx(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create/start ETH RXQ context, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	flow_cfg.rxq_flow_queue_id = state.rxq_flow_queue_id;

	status = allocate_eth_rxq_flow_resources(&flow_cfg, &(state.flow_resources));
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate flow resources, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_packet_buffer(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create packer buffer, err: %s", doca_error_get_name(status));
		goto rxq_cleanup;
	}

	status = create_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create tasks, err: %s", doca_error_get_name(status));
		goto destroy_packet_buffers;
	}

	status = submit_eth_rxq_tasks(&state);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit tasks, err: %s", doca_error_get_name(status));
		goto destroy_rxq_tasks;
	}

	retrieve_rxq_recv_tasks(&state);

	goto rxq_cleanup;

destroy_rxq_tasks:
	destroy_eth_rxq_tasks(&state);
destroy_packet_buffers:
	clean_status = destroy_eth_rxq_packet_buffers(&state);
	if (clean_status != DOCA_SUCCESS)
		return status;
rxq_cleanup:
	eth_rxq_cleanup(&state);

	return status;
}
