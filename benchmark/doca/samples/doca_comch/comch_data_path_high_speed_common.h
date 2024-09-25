/*
 * Copyright (c) 2023 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#ifndef COMCH_DATA_PATH_HIGH_SPEED_COMMON_H_
#define COMCH_DATA_PATH_HIGH_SPEED_COMMON_H_

#include <doca_buf_inventory.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#define CC_DATA_PATH_TASK_NUM 2			/* Maximum amount of CC consumer and producer task number */
#define CC_DATA_PATH_MAX_MSG_SIZE (1024 * 1024) /* CC DATA PATH maximum message size */

#define STR_START_DATA_PATH_TEST "start_data_path_test" /* The negotiation message between client and server */
#define STR_STOP_DATA_PATH_TEST "stop_data_path_test"	/* The negotiation message between client and server */

#define INVALID_CONSUMER_ID 0xffff

struct local_mem_bufs {
	void *mem;			    /* Memory address for DOCA buf mmap */
	struct doca_mmap *mmap;		    /* DOCA mmap object */
	struct doca_buf_inventory *buf_inv; /* DOCA buf inventory object */
	bool need_alloc_mem;		    /* Whether need to allocate memory */
};

struct comch_producer_cb_config {
	/* User specified callback when task completed successfully */
	doca_comch_producer_task_send_completion_cb_t send_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_comch_producer_task_send_completion_cb_t send_task_comp_err_cb;
	/* User specified context data */
	void *ctx_user_data;
	/* User specified PE context state changed event callback */
	doca_ctx_state_changed_callback_t ctx_state_changed_cb;
};

struct comch_consumer_cb_config {
	/* User specified callback when task completed successfully */
	doca_comch_consumer_task_post_recv_completion_cb_t recv_task_comp_cb;
	/* User specified callback when task completed with error */
	doca_comch_consumer_task_post_recv_completion_cb_t recv_task_comp_err_cb;
	/* User specified context data */
	void *ctx_user_data;
	/* User specified PE context state changed event callback */
	doca_ctx_state_changed_callback_t ctx_state_changed_cb;
};

struct comch_data_path_objects {
	struct doca_dev *hw_dev;		  /* Device used in the data path */
	struct doca_pe *pe;			  /* Connection PE object used in the sample */
	struct doca_comch_connection *connection; /* CC connection object used in the sample */
	struct doca_comch_consumer *consumer;	  /* CC consumer object used in the sample */
	struct doca_pe *consumer_pe;		  /* CC consumer's PE object used in the sample */
	struct local_mem_bufs consumer_mem;	  /* Mmap and DOCA buf objects for consumer */
	struct doca_comch_producer *producer;	  /* CC producer object used in the sample */
	struct doca_pe *producer_pe;		  /* CC producer's PE object used in the sample */
	struct local_mem_bufs producer_mem;	  /* Mmap and DOCA buf objects for producer */
	uint32_t remote_consumer_id;		  /* Consumer ID on the peer side */
	const char *text;			  /* Message to send to the server */
	doca_error_t producer_result;		  /* Holds result will be updated in producer callbacks */
	bool producer_finish;			  /* Controls whether producer progress loop should be run */
	doca_error_t consumer_result;		  /* Holds result will be updated in consumer callbacks */
	bool consumer_finish;			  /* Controls whether consumer progress loop should be run */
};

/**
 * Clean a local memory object
 *
 * @local [in]: The local memory object to clean
 */
void clean_local_mem_bufs(struct local_mem_bufs *local);

/**
 * Initialize a local memory mmap
 *
 * @local [in]: Local_memory object to initialize
 * @dev [in]: Device to add for this memory mmap
 * @buf_len [in]: Length of each DOCA buf
 * @max_bufs [in]: Number of DOCA buf
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_local_mem_bufs(struct local_mem_bufs *local, struct doca_dev *dev, size_t buf_len, size_t max_bufs);

/**
 * Clean producer and its PE
 *
 * @producer [in]: Producer object to clean
 * @pe [in]: Producer PE object to clean
 */
void clean_comch_producer(struct doca_comch_producer *producer, struct doca_pe *pe);

/**
 * Initialize a cc producer and its PE
 *
 * @connection [in]: CC connection the producer is built on
 * @cb_cfg [in]: Producer callback configuration
 * @producer [out]: Producer objects struct to initialize
 * @pe [out]: Producer PE objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_comch_producer(struct doca_comch_connection *connection,
				 struct comch_producer_cb_config *cb_cfg,
				 struct doca_comch_producer **producer,
				 struct doca_pe **pe);

/**
 * Clean consumer and its PE
 *
 * @consumer [in]: Consumer object to clean
 * @pe [in]: Consumer PE object to clean
 */
void clean_comch_consumer(struct doca_comch_consumer *consumer, struct doca_pe *pe);

/**
 * Initialize a cc producer and its PE
 *
 * @connection [in]: CC connection the consumer is built on
 * @user_mmap [in]: The local memory mmap required by consumer
 * @cb_cfg [in]: Consumer callback configuration
 * @consumer [out]: Consumer objects struct to initialize
 * @pe [out]: Consumer PE objects struct to initialize
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t init_comch_consumer(struct doca_comch_connection *connection,
				 struct doca_mmap *user_mmap,
				 struct comch_consumer_cb_config *cb_cfg,
				 struct doca_comch_consumer **consumer,
				 struct doca_pe **pe);

/**
 * Use cc high speed data path to send a msg
 *
 * @data_path [in]: CC data path resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t comch_data_path_send_msg(struct comch_data_path_objects *data_path);

/**
 * Use cc high speed data path to recv a msg
 *
 * @data_path [in]: CC data path resources
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t comch_data_path_recv_msg(struct comch_data_path_objects *data_path);

#endif // COMCH_DATA_PATH_HIGH_SPEED_COMMON_H_
