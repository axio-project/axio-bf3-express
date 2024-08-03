/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <utils.h>

#include "file_integrity_core.h"

#define MAX_MSG 512					/* Maximum number of messages in CC queue */
#define SLEEP_IN_NANOS (10 * 1000)			/* Sample the job every 10 microseconds */
#define MAX_FILE_SIZE (2L * 1024 * 1024 * 1024)		/* 2Gbyte */
#define DEFAULT_TIMEOUT 10				/* default timeout for receiving messages */
#define SERVER_NAME "file_integrity_server"		/* CC server name */

DOCA_LOG_REGISTER(FILE_INTEGRITY::Core);

/*
 * Set Comm Channel properties
 *
 * @mode [in]: Running mode
 * @ep [in]: DOCA comm_channel endpoint
 * @dev [in]: DOCA device object to use
 * @dev_rep [in]: DOCA device representor object to use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_endpoint_properties(enum file_integrity_mode mode, struct doca_comm_channel_ep_t *ep, struct doca_dev *dev, struct doca_dev_rep *dev_rep)
{
	doca_error_t result;

	result = doca_comm_channel_ep_set_device(ep, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DOCA device property");
		return result;
	}

	result = doca_comm_channel_ep_set_max_msg_size(ep, MAX_MSG_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set max_msg_size property");
		return result;
	}

	result = doca_comm_channel_ep_set_send_queue_size(ep, MAX_MSG);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set snd_queue_size property");
		return result;
	}

	result = doca_comm_channel_ep_set_recv_queue_size(ep, MAX_MSG);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set rcv_queue_size property");
		return result;
	}

	if (mode == SERVER) {
		result = doca_comm_channel_ep_set_device_rep(ep, dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set DOCA device representor property");
			return result;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Free callback - free doca_buf allocated pointer
 *
 * @addr [in]: Memory range pointer
 * @len [in]: Memory range length
 * @opaque [in]: An opaque pointer passed to iterator
 */
static void
free_cb(void *addr, size_t len, void *opaque)
{
	(void)len;
	(void)opaque;

	if (addr != NULL)
		free(addr);
}

/*
 * Unmap callback - free doca_buf allocated pointer
 *
 * @addr [in]: Memory range pointer
 * @len [in]: Memory range length
 * @opaque [in]: An opaque pointer passed to iterator
 */
static void
unmap_cb(void *addr, size_t len, void *opaque)
{
	(void)opaque;

	if (addr != NULL)
		munmap(addr, len);
}

/*
 * Populate destination doca buffer for SHA jobs
 *
 * @state [in]: application configuration struct
 * @dst_doca_buf [out]: created doca buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
populate_dst_buf(struct program_core_objects *state, struct doca_buf **dst_doca_buf)
{
	char *dst_buffer = NULL;
	doca_error_t result;

	dst_buffer = calloc(1, DOCA_SHA256_BYTE_COUNT);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}
	result = doca_mmap_set_free_cb(state->dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}
	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}

	result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->dst_mmap, dst_buffer, DOCA_SHA256_BYTE_COUNT,
						dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_get_error_string(result));
		return result;
	}
	return result;
}

/*
 * Submit SHA job and retrieve the result
 *
 * @state [in]: application configuration struct
 * @sha_job [in]: job to submit
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
process_sha_job(struct program_core_objects *state, const struct doca_sha_job *sha_job)
{
	struct doca_event event = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	/* Enqueue sha job */
	result = doca_workq_submit(state->workq, (struct doca_job *)sha_job);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit sha job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve sha job: %s", doca_get_error_string(result));
	else if (event.result.u64 != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Sha job finished unsuccessfully");
		result = event.result.u64;
	} else
		result = DOCA_SUCCESS;

	return result;
}

/*
 * Construct sha partial job, submit it and print the result if it is the final segment
 *
 * @state [in]: application configuration struct
 * @session [in]: doca sha_partial session object
 * @dst_doca_buf [in]: destination doca buffer
 * @src_doca_buf [in]: source doca buffer
 * @is_final [in]: true if this is the final partial job and false otherwise
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
calculate_partial_sha(struct program_core_objects *state, struct doca_sha_partial_session *session, struct doca_buf *dst_doca_buf, struct doca_buf *src_doca_buf, bool is_final)
{
	uint64_t flags = DOCA_SHA_JOB_FLAGS_NONE;
	char sha_output[DOCA_SHA256_BYTE_COUNT * 2 + 1] = {0};
	uint8_t *resp_head;
	doca_error_t result;
	int i;

	/* for the last segment, the DOCA_SHA_JOB_FLAGS_SHA_PARTIAL_FINAL flag must be set */
	if (is_final)
		flags = DOCA_SHA_JOB_FLAGS_SHA_PARTIAL_FINAL;

	/* Construct sha partial job */
	const struct doca_sha_job sha_job = {
		.base = (struct doca_job) {
			.type = DOCA_SHA_JOB_SHA256_PARTIAL,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state->ctx,
			},
		.resp_buf = dst_doca_buf,
		.req_buf = src_doca_buf,
		.flags = flags,
	};

	const struct doca_sha_partial_job sha_partial_job = {
		.sha_job = sha_job,
		.session = session,
	};

	result = process_sha_job(state, &(sha_partial_job.sha_job));
	if (result == DOCA_SUCCESS && is_final) {
		doca_buf_get_data(sha_job.resp_buf, (void **)&resp_head);
		for (i = 0; i < DOCA_SHA256_BYTE_COUNT; i++)
			snprintf(sha_output + (2 * i), 3, "%02x", resp_head[i]);
		DOCA_LOG_INFO("SHA256 output is: %s", sha_output);
	}
	return result;
}

/*
 * Construct sha job, submit it and print the result
 *
 * @state [in]: application configuration struct
 * @dst_doca_buf [in]: destination doca buffer
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
calculate_sha(struct program_core_objects *state, struct doca_buf **dst_doca_buf, char *file_data, size_t file_size)
{
	char sha_output[DOCA_SHA256_BYTE_COUNT * 2 + 1] = {0};
	struct doca_buf *src_doca_buf;
	uint8_t *resp_head;
	doca_error_t result;
	int i;

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_set_free_cb(state->src_mmap, &unmap_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of source memory map: %s",
			     doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}

	result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
			     doca_get_error_string(result));
		return result;
	}

	result = doca_buf_set_data(src_doca_buf, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure");
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	result = populate_dst_buf(state, dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}
	/* Construct sha partial job */
	const struct doca_sha_job sha_job = {
		.base = (struct doca_job) {
			.type = DOCA_SHA_JOB_SHA256,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state->ctx,
			},
		.resp_buf = *dst_doca_buf,
		.req_buf = src_doca_buf,
		.flags = DOCA_SHA_JOB_FLAGS_NONE,
	};

	result = process_sha_job(state, &sha_job);

	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(*dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	doca_buf_get_data(sha_job.resp_buf, (void **)&resp_head);
	for (i = 0; i < DOCA_SHA256_BYTE_COUNT; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", resp_head[i]);
	DOCA_LOG_INFO("SHA256 output is: %s", sha_output);

	doca_buf_refcount_rm(src_doca_buf, NULL);
	return DOCA_SUCCESS;
}

/*
 * Send the input file with comm channel to the server in segments of MAX_MSG_SIZE
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
send_file(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
	 char *file_data, size_t file_size)
{
	uint32_t total_msgs;
	uint32_t total_msgs_msg;
	size_t msg_len;
	uint32_t i;
	doca_error_t result;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* Send to the server the number of messages needed for receiving the file */
	total_msgs = (file_size + MAX_MSG_SIZE - 1) / MAX_MSG_SIZE;
	total_msgs_msg = htonl(total_msgs);

	while ((result = doca_comm_channel_ep_sendto(ep, &total_msgs_msg, sizeof(uint32_t), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not sent: %s", doca_get_error_string(result));
		return result;
	}

	/* Send file to the server */
	for (i = 0; i < total_msgs; i++) {
		msg_len = MIN(file_size, MAX_MSG_SIZE);
		while ((result = doca_comm_channel_ep_sendto(ep, file_data, msg_len, DOCA_CC_MSG_FLAG_NONE,
							     *peer_addr)) == DOCA_ERROR_AGAIN)
			nanosleep(&ts, &ts);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not sent: %s", doca_get_error_string(result));
			return result;
		}
		file_data += msg_len;
		file_size -= msg_len;
	}
	return DOCA_SUCCESS;
}

doca_error_t
file_integrity_client(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
		      struct file_integrity_config *app_cfg, struct program_core_objects *state)
{
	struct doca_buf *dst_doca_buf;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	char *file_data;
	uint8_t *sha_msg;
	char msg[MAX_MSG_SIZE] = {0};
	size_t msg_len;
	struct stat statbuf;
	int fd;
	doca_error_t result;

	fd = open(app_cfg->file_path, O_RDWR);
	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", app_cfg->file_path);
		return DOCA_ERROR_IO_FAILED;
	}

	if (fstat(fd, &statbuf) < 0) {
		DOCA_LOG_ERR("Failed to get file information");
		close(fd);
		return DOCA_ERROR_IO_FAILED;
	}

	if (statbuf.st_size == 0 || statbuf.st_size > MAX_FILE_SIZE) {
		DOCA_LOG_ERR("Invalid file size. Should be greater then zero and smaller then two Gbytes");
		close(fd);
		return DOCA_ERROR_INVALID_VALUE;
	}

	file_data = mmap(NULL, statbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (file_data == MAP_FAILED) {
		DOCA_LOG_ERR("Unable to map file content: %s", strerror(errno));
		close(fd);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Send SHA job */
	result = calculate_sha(state, &dst_doca_buf, file_data, statbuf.st_size);
	if (result != DOCA_SUCCESS) {
		close(fd);
		return result;
	}

	/* Send file SHA to the server */
	doca_buf_get_data(dst_doca_buf, (void **)&sha_msg);

	while ((result = doca_comm_channel_ep_sendto(ep, sha_msg, DOCA_SHA256_BYTE_COUNT, DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not sent: %s", doca_get_error_string(result));
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		close(fd);
		return result;
	}

	doca_buf_refcount_rm(dst_doca_buf, NULL);

	/* Send the file content to the server */
	result = send_file(ep, peer_addr, file_data, statbuf.st_size);
	if (result != DOCA_SUCCESS) {
		close(fd);
		return result;
	}

	close(fd);

	/* Receive finish message when file was completely read by the server */
	msg_len = MAX_MSG_SIZE;
	while ((result = doca_comm_channel_ep_recvfrom(ep, msg, &msg_len, DOCA_CC_MSG_FLAG_NONE, peer_addr)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
		msg_len = MAX_MSG_SIZE;
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Finish message was not received: %s", doca_get_error_string(result));
		return result;
	}
	msg[MAX_MSG_SIZE - 1] = '\0';
	DOCA_LOG_INFO("%s", msg);

	return result;
}

doca_error_t
file_integrity_server(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
		struct file_integrity_config *app_cfg, struct program_core_objects *state,
		struct doca_sha *sha_ctx)
{
	struct doca_buf *dst_doca_buf;
	struct doca_buf *src_doca_buf;
	struct doca_sha_partial_session *session;
	uint8_t received_sha[DOCA_SHA256_BYTE_COUNT] = {0};
	char received_msg[MAX_MSG_SIZE] = {0};
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	uint32_t i, total_msgs;
	size_t msg_len;
	bool is_final = false;
	int fd;
	uint8_t *file_sha;
	char finish_msg[] = "Server was done receiving messages";
	int counter;
	int num_of_iterations = (app_cfg->timeout * 1000 * 1000) / (SLEEP_IN_NANOS / 1000);
	doca_error_t result;

	/* receive file SHA from the client */
	msg_len = DOCA_SHA256_BYTE_COUNT;
	while ((result = doca_comm_channel_ep_recvfrom(ep, received_sha, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       peer_addr)) == DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
		msg_len = DOCA_SHA256_BYTE_COUNT;
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not received: %s", doca_get_error_string(result));
		goto finish_msg;
	}
	if (msg_len != DOCA_SHA256_BYTE_COUNT) {
		DOCA_LOG_ERR("Received only partial SHA message: %ld bytes were received", msg_len);
		result = DOCA_ERROR_IO_FAILED;
		goto finish_msg;
	}

	/* receive number of total msgs from the client */
	msg_len = MAX_MSG_SIZE;
	counter = 0;
	while ((result = doca_comm_channel_ep_recvfrom(ep, received_msg, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       peer_addr)) == DOCA_ERROR_AGAIN) {
		msg_len = MAX_MSG_SIZE;
		nanosleep(&ts, &ts);
		counter++;
		if (counter == num_of_iterations)
			goto finish_msg;
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not received: %s", doca_get_error_string(result));
		goto finish_msg;
	}

	if (msg_len != sizeof(uint32_t)) {
		DOCA_LOG_ERR("Received wrong message size, required %ld, got %ld", sizeof(uint32_t), msg_len);
		goto finish_msg;
	}

	total_msgs = ntohl(*(uint32_t *)received_msg);

	/* create sha session for all partial sha jobs */
	result = doca_sha_partial_session_create(sha_ctx, state->workq, &session);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create sha partial session: %s", doca_get_error_string(result));
		goto finish_msg;
	}

	result = doca_mmap_set_memrange(state->src_mmap, received_msg, MAX_MSG_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_get_error_string(result));
		goto finish_msg;
	}
	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start source memory map: %s", doca_get_error_string(result));
		goto finish_msg;
	}

	result = populate_dst_buf(state, &dst_doca_buf);
	if (result != DOCA_SUCCESS)
		goto finish_msg;

	fd = open(app_cfg->file_path, O_CREAT | O_WRONLY, S_IRUSR | S_IRGRP);
	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", app_cfg->file_path);
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		result = DOCA_ERROR_IO_FAILED;
		goto finish_msg;
	}

	/* receive the file and send partial sha job for each received segment */
	for (i = 0; i < total_msgs; i++) {
		memset(received_msg, 0, sizeof(received_msg));
		msg_len = MAX_MSG_SIZE;
		counter = 0;
		while ((result = doca_comm_channel_ep_recvfrom(ep, received_msg, &msg_len, DOCA_CC_MSG_FLAG_NONE,
							       peer_addr)) == DOCA_ERROR_AGAIN) {
			msg_len = MAX_MSG_SIZE;
			nanosleep(&ts, &ts);
			counter++;
			if (counter == num_of_iterations) {
				DOCA_LOG_ERR("Message was not received at the given timeout");
				close(fd);
				doca_buf_refcount_rm(dst_doca_buf, NULL);
				goto finish_msg;
			}
		}
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not received: %s", doca_get_error_string(result));
			close(fd);
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			goto finish_msg;
		}

		DOCA_DLOG_DBG("Received message #%d", i+1);

		result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->src_mmap, received_msg, msg_len,
							&src_doca_buf);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
				     doca_get_error_string(result));
			close(fd);
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			goto finish_msg;
		}
		result = doca_buf_set_data(src_doca_buf, received_msg, msg_len);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure");
			close(fd);
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_buf_refcount_rm(src_doca_buf, NULL);
			goto finish_msg;
		}

		if ((size_t)write(fd, received_msg, msg_len) != msg_len) {
			DOCA_LOG_ERR("Failed to write the received message into the input file");
			close(fd);
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_buf_refcount_rm(src_doca_buf, NULL);
			result = DOCA_ERROR_IO_FAILED;
			goto finish_msg;
		}

		/* send last segment flag to partial SHA calculation */
		is_final = i == (total_msgs - 1);

		result = calculate_partial_sha(state, session, dst_doca_buf, src_doca_buf, is_final);
		if (result != DOCA_SUCCESS) {
			close(fd);
			doca_buf_refcount_rm(dst_doca_buf, NULL);
			doca_buf_refcount_rm(src_doca_buf, NULL);
			goto finish_msg;
		}

		doca_buf_refcount_rm(src_doca_buf, NULL);
	}

	close(fd);

	/* compare received SHA with calculated SHA */
	doca_buf_get_data(dst_doca_buf, (void **)&file_sha);
	if (memcmp(file_sha, received_sha, DOCA_SHA256_BYTE_COUNT) == 0)
		DOCA_LOG_INFO("SUCCESS: file SHA is identical to received SHA");
	else {
		DOCA_LOG_ERR("ERROR: SHA is not identical, file was compromised");
		if (remove(app_cfg->file_path) < 0)
			DOCA_LOG_ERR("Failed to remove %s", app_cfg->file_path);
	}

	doca_buf_refcount_rm(dst_doca_buf, NULL);

finish_msg:
	/* Send finish message to the client */
	while ((result = doca_comm_channel_ep_sendto(ep, finish_msg, sizeof(finish_msg), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to send finish message: %s", doca_get_error_string(result));
	return result;
}

/*
 * Check if given device is capable of executing a DOCA_SHA_JOB_SHA256 with HW.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_SHA_JOB_SHA256 and DOCA_ERROR otherwise.
 */
static doca_error_t
sha_jobs_sha256_is_supported(struct doca_devinfo *devinfo)
{
	doca_error_t result;

	result = doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256);
	if (result != DOCA_SUCCESS)
		return result;
	return doca_sha_get_hardware_supported(devinfo);
}

doca_error_t
file_integrity_init(struct doca_comm_channel_ep_t **ep, struct doca_comm_channel_addr_t **peer_addr,
		struct file_integrity_config *app_cfg, struct program_core_objects *state,
		struct doca_sha **sha_ctx)
{
	struct doca_dev *cc_doca_dev;
	struct doca_dev_rep *cc_doca_dev_rep = NULL;
	struct timespec ts = {0};
	uint32_t workq_depth = 1; /* The app will run 1 sha job at a time */
	uint32_t max_bufs = 2;    /* The app will use 2 doca buffers */
	doca_error_t result;

	/* set default timeout */
	if (app_cfg->timeout == 0)
		app_cfg->timeout = DEFAULT_TIMEOUT;

	/* Create Comm Channel endpoint */
	result = doca_comm_channel_ep_create(ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create Comm Channel endpoint: %s", doca_get_error_string(result));
		return result;
	}

	result = doca_sha_create(sha_ctx);
	if (result != DOCA_SUCCESS) {
		doca_comm_channel_ep_destroy(*ep);
		DOCA_LOG_ERR("Failed to init sha library: %s", doca_get_error_string(result));
		return result;
	}

	state->ctx = doca_sha_as_ctx(*sha_ctx);

	result = open_doca_device_with_pci(app_cfg->cc_dev_pci_addr, NULL, &cc_doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init sha library: %s", doca_get_error_string(result));
		goto sha_destroy;
	}

	if (app_cfg->mode == SERVER) {
		result = open_doca_device_rep_with_pci(cc_doca_dev, DOCA_DEV_REP_FILTER_NET,
						       app_cfg->cc_dev_rep_pci_addr, &cc_doca_dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open representor device: %s", doca_get_error_string(result));
			goto dev_close;
		}
	}

	/* Open device for sha jobs */
	result = open_doca_device_with_capabilities(&sha_jobs_sha256_is_supported, &state->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA device with SHA capabilities: %s", doca_get_error_string(result));
		goto rep_dev_close;
	}

	/* Set ep attributes */
	result = set_endpoint_properties(app_cfg->mode, *ep, cc_doca_dev, cc_doca_dev_rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CC ep attributes: %s", doca_get_error_string(result));
		goto destroy_core_objs;
	}

	result = init_core_objects(state, workq_depth, max_bufs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA core objects: %s", doca_get_error_string(result));
		goto destroy_core_objs;
	}

	if (app_cfg->mode == CLIENT) {
		result = doca_comm_channel_ep_connect(*ep, SERVER_NAME, peer_addr);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Couldn't establish a connection with the server node: %s", doca_get_error_string(result));
			goto destroy_core_objs;
		}

		while ((result = doca_comm_channel_peer_addr_update_info(*peer_addr)) == DOCA_ERROR_CONNECTION_INPROGRESS)
			nanosleep(&ts, &ts);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to validate the connection with the DPU: %s", doca_get_error_string(result));
			goto destroy_core_objs;
		}

		DOCA_LOG_INFO("Connection to DPU was established successfully");
	} else {
		result = doca_comm_channel_ep_listen(*ep, SERVER_NAME);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Comm channel server couldn't start listening: %s", doca_get_error_string(result));
			goto destroy_core_objs;
		}

		DOCA_LOG_INFO("Started Listening, waiting for new connection");
	}

	return DOCA_SUCCESS;

destroy_core_objs:
	destroy_core_objects(state);
rep_dev_close:
	if (app_cfg->mode == SERVER)
		doca_dev_rep_close(cc_doca_dev_rep);
dev_close:
	doca_dev_close(cc_doca_dev);
sha_destroy:
	doca_sha_destroy(*sha_ctx);
	doca_comm_channel_ep_destroy(*ep);
	return result;
}

void
file_integrity_cleanup(struct program_core_objects *state, struct doca_sha *sha_ctx,
		struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr)
{
	doca_error_t result;

	result = doca_comm_channel_ep_disconnect(ep, *peer_addr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to disconnect channel: %s", doca_get_error_string(result));

	result = doca_comm_channel_ep_destroy(ep);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy channel: %s", doca_get_error_string(result));

	result = destroy_core_objects(state);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy core objects: %s", doca_get_error_string(result));

	result = doca_sha_destroy(sha_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy sha: %s", doca_get_error_string(result));
}

/*
 * ARGP Callback - Handle file parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
file_callback(void *param, void *config)
{
	struct file_integrity_config *app_cfg = (struct file_integrity_config *)config;
	char *file_path = (char *)param;

	if (strnlen(file_path, MAX_FILE_NAME) == MAX_FILE_NAME) {
		DOCA_LOG_ERR("File name is too long - MAX=%d", MAX_FILE_NAME - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strlcpy(app_cfg->file_path, file_path, MAX_FILE_NAME);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
dev_pci_addr_callback(void *param, void *config)
{
	struct file_integrity_config *app_cfg = (struct file_integrity_config *)config;
	char *dev_pci_addr = (char *)param;

	if (strnlen(dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE) == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strlcpy(app_cfg->cc_dev_pci_addr, dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE);
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device representor PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
rep_pci_addr_callback(void *param, void *config)
{
	struct file_integrity_config *app_cfg = (struct file_integrity_config *)config;
	const char *rep_pci_addr = (char *)param;

	if (app_cfg->mode == SERVER) {
		if (strnlen(rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE) == DOCA_DEVINFO_REP_PCI_ADDR_SIZE) {
			DOCA_LOG_ERR("Entered device representor PCI address exceeding the maximum size of %d",
				     DOCA_DEVINFO_REP_PCI_ADDR_SIZE - 1);
			return DOCA_ERROR_INVALID_VALUE;
		}

		strlcpy(app_cfg->cc_dev_rep_pci_addr, rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE);
	}

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle timeout parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
timeout_callback(void *param, void *config)
{
	struct file_integrity_config *app_cfg = (struct file_integrity_config *)config;
	int *timeout = (int *)param;

	if (*timeout <= 0) {
		DOCA_LOG_ERR("Timeout parameter must be positive value");
		return DOCA_ERROR_INVALID_VALUE;
	}
	app_cfg->timeout = *timeout;
	return DOCA_SUCCESS;
}

/*
 * ARGP validation Callback - check if the running mode is valid and that the input file exists in client mode
 *
 * @cfg [in]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
args_validation_callback(void *cfg)
{
	struct file_integrity_config *app_cfg = (struct file_integrity_config *)cfg;

	if (app_cfg->mode == CLIENT && (access(app_cfg->file_path, F_OK) == -1)) {
		DOCA_LOG_ERR("File was not found %s", app_cfg->file_path);
		return DOCA_ERROR_NOT_FOUND;
	} else if (app_cfg->mode == SERVER && strlen(app_cfg->cc_dev_rep_pci_addr) == 0) {
		DOCA_LOG_ERR("Missing PCI address for server");
		return DOCA_ERROR_NOT_FOUND;
	}
	return DOCA_SUCCESS;
}

doca_error_t
register_file_integrity_params(void)
{
	doca_error_t result;

	struct doca_argp_param *dev_pci_addr_param, *rep_pci_addr_param, *file_param, *timeout_param;

	/* Create and register Comm Channel DOCA device PCI address */
	result = doca_argp_param_create(&dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(dev_pci_addr_param, "p");
	doca_argp_param_set_long_name(dev_pci_addr_param, "pci-addr");
	doca_argp_param_set_description(dev_pci_addr_param, "DOCA Comm Channel device PCI address");
	doca_argp_param_set_callback(dev_pci_addr_param, dev_pci_addr_callback);
	doca_argp_param_set_type(dev_pci_addr_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(dev_pci_addr_param);
	result = doca_argp_register_param(dev_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register Comm Channel DOCA device representor PCI address */
	result = doca_argp_param_create(&rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(rep_pci_addr_param, "r");
	doca_argp_param_set_long_name(rep_pci_addr_param, "rep-pci");
	doca_argp_param_set_description(rep_pci_addr_param, "DOCA Comm Channel device representor PCI address");
	doca_argp_param_set_callback(rep_pci_addr_param, rep_pci_addr_callback);
	doca_argp_param_set_type(rep_pci_addr_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register message to send param */
	result = doca_argp_param_create(&file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(file_param, "f");
	doca_argp_param_set_long_name(file_param, "file");
	doca_argp_param_set_description(file_param, "File to send by the client / File to write by the server");
	doca_argp_param_set_callback(file_param, file_callback);
	doca_argp_param_set_type(file_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(file_param);
	result = doca_argp_register_param(file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register timeout */
	result = doca_argp_param_create(&timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(timeout_param, "t");
	doca_argp_param_set_long_name(timeout_param, "timeout");
	doca_argp_param_set_description(timeout_param, "Application timeout for receiving file content messages, default is 5 sec");
	doca_argp_param_set_callback(timeout_param, timeout_callback);
	doca_argp_param_set_type(timeout_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_get_error_string(result));
		return result;
	}

	/* Register application callback */
	result = doca_argp_register_validation_callback(args_validation_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program validation callback: %s", doca_get_error_string(result));
		return result;
	}

	return result;
}
