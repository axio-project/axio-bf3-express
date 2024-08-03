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

#include <stdbool.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_mbuf_core.h>

#include <doca_buf.h>
#include <doca_dev.h>
#include <doca_dpi.h>
#include <doca_log.h>

#include "dpi_common.h"

DOCA_LOG_REGISTER(DPI_SCAN);

#define MAX_WORKQ_NUM 16

struct doca_dpi_worker {
	struct doca_dpi *dpi_ctx;			/* The DOCA DPI instance */
	struct doca_workq *workq[MAX_WORKQ_NUM];	/* The workq attached to dpi_ctx */
	uint8_t nb_workq_attached;			/* The actual workq count attached to dpi_ctx */
};

/*
 * Init doca_dpi internal objects
 *
 * @worker [in]: DOCA DPI worker containing all the necessary objects
 * @nb_workq [in]: Number of workq to be initialized
 * @max_sig_match_len [in]: The maximum signature match length
 * @per_workq_packet_pool_size [in]: The maximum inflight packets per queue
 * @dev [in]: The doca_dev initialized in DPDK bridge
 * @sig_file_path [in]: The signature file path to be used for programming regex engine
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
doca_dpi_worker_init(struct doca_dpi_worker *worker,
		    uint8_t nb_workq,
		    uint16_t max_sig_match_len,
		    uint32_t per_workq_packet_pool_size,
		    struct doca_dev *dev,
		    const char *sig_file_path)
{
	int i;
	doca_error_t result;
	uint32_t workq_depth = per_workq_packet_pool_size;

	if (worker == NULL || dev == NULL || sig_file_path == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	/* Create doca_dpi instance */
	result = doca_dpi_create(&worker->dpi_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to create doca_dpi instance. err = [%s]",
				doca_get_error_string(result));
		return result;
	}

	/* Add doca_dev into doca_dpi instance */
	result = doca_ctx_dev_add(doca_dpi_as_ctx(worker->dpi_ctx), dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to register device with dpi. err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Load signatures into doca_dpi's backend device */
	result = doca_dpi_set_signatures(worker->dpi_ctx, sig_file_path);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Loading DPI signatures failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Set per_workq_packet_pool_size */
	result = doca_dpi_set_per_workq_packet_pool_size(worker->dpi_ctx, per_workq_packet_pool_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Set per_workq_packet_pool_size failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Set max_sig_match_len */
	result = doca_dpi_set_max_sig_match_len(worker->dpi_ctx, max_sig_match_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Set max_sig_match_len failed, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Start doca_dpi */
	result = doca_ctx_start(doca_dpi_as_ctx(worker->dpi_ctx));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start doca_dpi, err = [%s]",
				doca_get_error_string(result));
		goto dpi_destroy;
	}

	/* Workq create and add */
	worker->nb_workq_attached = 0;
	for (i = 0; i < nb_workq; i++) {
		result = doca_workq_create(workq_depth, &worker->workq[i]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to create DOCA workq, err = [%s]",
				doca_get_error_string(result));
			goto workq_create_fail;
		}
		result = doca_ctx_workq_add(doca_dpi_as_ctx(worker->dpi_ctx), worker->workq[i]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to register workq with doca_dpi, err = [%s]",
					doca_get_error_string(result));
			goto workq_add_fail;
		}
		worker->nb_workq_attached++;
	}

	return result;

workq_add_fail:
	doca_workq_destroy(worker->workq[worker->nb_workq_attached - 1]);
	worker->workq[worker->nb_workq_attached - 1] = NULL;
workq_create_fail:
	for (i = 0; i < worker->nb_workq_attached - 1; i++) {
		doca_ctx_workq_rm(doca_dpi_as_ctx(worker->dpi_ctx), worker->workq[i]);
		doca_workq_destroy(worker->workq[i]);
		worker->workq[i] = NULL;
	}
	doca_ctx_stop(doca_dpi_as_ctx(worker->dpi_ctx));
dpi_destroy:
	doca_dpi_destroy(worker->dpi_ctx);
	worker->dpi_ctx = NULL;
	return result;
}

/*
 * Destroy doca_dpi internal objects
 *
 * @worker [in]: DOCA DPI worker containing all the necessary objects.
 */
static void
doca_dpi_worker_destroy(struct doca_dpi_worker *worker)
{
	int i;

	if (worker == NULL)
		return;

	for (i = 0; i < worker->nb_workq_attached; i++) {
		if (worker->workq[i] != NULL) {
			doca_ctx_workq_rm(doca_dpi_as_ctx(worker->dpi_ctx), worker->workq[i]);
			doca_workq_destroy(worker->workq[i]);
			worker->workq[i] = NULL;
		}
	}
	if (worker->dpi_ctx != NULL) {
		doca_ctx_stop(doca_dpi_as_ctx(worker->dpi_ctx));
		doca_dpi_destroy(worker->dpi_ctx);
		worker->dpi_ctx = NULL;
	}
}

/*
 * Run DOCA DPI scan sample
 *
 * @sig_file [in]: Signatures file path
 * @dev [in]: The initialized DOCA device
 * @pkt_doca_buf [in]: The to be scanned packet represented by a DOCA buf
 * @parsing_info [in]: Packet parsing informations
 * @payload_offset [in]: Packet payload offset
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
dpi_scan(const char *sig_file, struct doca_dev *dev, struct doca_buf *pkt_doca_buf,
	 struct doca_dpi_parsing_info *parsing_info, uint32_t *payload_offset)
{
	bool to_server = true;
	doca_error_t result;
	int packets_to_process = 0;
	struct doca_dpi_sig_data sig_data;
	struct doca_dpi_flow_ctx *flow_ctx = NULL;
	struct doca_dpi_result dpi_res = {0};
	struct doca_dpi_stat_info stats = {0};
	struct doca_dpi_worker worker = {0};
	struct doca_event event = {0};

	/* Initialization of DPI library */
	result = doca_dpi_worker_init(&worker,
				/* Total number of DPI queues */
				1,
				/* Maximum job size in bytes for regex scan match */
				5000,
				/* Max amount of FIDS per DPI queue */
				100,
				/* DOCA dvice */
				dev,
				/* Signature files */
				sig_file
		);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("DPI init failed, error = %s", doca_get_error_string(result));
		return result;
	}

	/* Create DPI flow according to packet info */
	result = doca_dpi_flow_create(worker.dpi_ctx, worker.workq[0], parsing_info, &flow_ctx);
	if (result != DOCA_SUCCESS) {
		doca_dpi_worker_destroy(&worker);
		DOCA_LOG_ERR("DPI flow creation failed, %s", doca_get_error_string(result));
		return result;
	}

	/* Create a DPI job */
	struct doca_dpi_job job = (struct doca_dpi_job) {
		.base.type = DOCA_DPI_JOB,
		.base.flags = DOCA_JOB_FLAGS_NONE,
		.base.ctx = doca_dpi_as_ctx(worker.dpi_ctx),
		.base.user_data.ptr = NULL,
		.pkt = pkt_doca_buf,
		.initiator = to_server,
		.payload_offset = *payload_offset,
		.flow_ctx = flow_ctx,
		.result = &dpi_res
	};

retry_job_submit:
	result = doca_workq_submit(worker.workq[0], &(job.base));
	if (result == DOCA_SUCCESS)
		packets_to_process = 1;
	else if (result == DOCA_ERROR_NO_MEMORY)
		goto retry_job_submit;
	else {
		DOCA_LOG_ERR("DPI job submission failed, error = %s", doca_get_error_string(result));
		doca_dpi_flow_destroy(flow_ctx);
		doca_dpi_worker_destroy(&worker);
		return result;
	}

	while (packets_to_process > 0) {
		result = doca_workq_progress_retrieve(worker.workq[0], &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE);
		if (result == DOCA_SUCCESS) {
			packets_to_process -= 1;
			if (dpi_res.matched) {
				result = doca_dpi_get_signature(worker.dpi_ctx, dpi_res.info.sig_id, &sig_data);
				if (result != DOCA_SUCCESS) {
					DOCA_LOG_ERR("Failed to get signatures - error = %s",
							doca_get_error_string(result));
					doca_dpi_flow_destroy(flow_ctx);
					doca_dpi_worker_destroy(&worker);
					return result;
				}
				DOCA_LOG_INFO(
					"DPI found a match on signature with ID: %u and URL MSG: %s",
					dpi_res.info.sig_id, sig_data.name);
			}
		} else if (result != DOCA_ERROR_AGAIN) {
			DOCA_LOG_ERR("DPI response recv failed, error = %s", doca_get_error_string(result));
			doca_dpi_flow_destroy(flow_ctx);
			doca_dpi_worker_destroy(&worker);
			return result;
		}
	}

	doca_dpi_get_stats(worker.dpi_ctx, true, &stats);

	DOCA_LOG_INFO("------------- DPI STATISTICS --------------");
	DOCA_LOG_INFO("Packets scanned:%d", stats.nb_scanned_pkts);
	DOCA_LOG_INFO("Matched signatures:%d", stats.nb_matches);
	DOCA_LOG_INFO("TCP matches:%d", stats.nb_tcp_based);
	DOCA_LOG_INFO("UDP matches:%d", stats.nb_udp_based);
	DOCA_LOG_INFO("HTTP matches:%d", stats.nb_http_parser_based);

	doca_dpi_flow_destroy(flow_ctx);
	doca_dpi_worker_destroy(&worker);

	return DOCA_SUCCESS;
}
