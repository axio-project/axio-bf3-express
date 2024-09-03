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
#include <doca_pe.h>

#include <utils.h>

#include "file_integrity_core.h"

#define MAX_MSG			(512)				/* Maximum number of messages in CC queue */
#define SLEEP_IN_NANOS		(10 * 1000)			/* Sample the task every 10 microseconds */
#define DEFAULT_TIMEOUT		(10)				/* default timeout for receiving messages */
#define SERVER_NAME		("file_integrity_server")	/* CC server name */
#define SHA_ALGORITHM		(DOCA_SHA_ALGORITHM_SHA256)	/* doca_sha_algorithm for the sample */
#define LOG_NUM_SHA_TASKS	(0)				/* Log of SHA tasks number */


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
 * Populate destination doca buffer for SHA tasks
 *
 * @state [in]: application configuration struct
 * @dst_doca_buf [out]: created doca buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
populate_dst_buf(struct program_core_objects *state, struct doca_buf **dst_doca_buf)
{
	char *dst_buffer = NULL;
	uint32_t min_dst_sha_buffer_size;
	doca_error_t result;

	result = doca_sha_cap_get_min_dst_buffer_size(doca_dev_as_devinfo(state->dev), SHA_ALGORITHM, &min_dst_sha_buffer_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get minimum destination buffer size for DOCA SHA: %s", doca_error_get_descr(result));
		return result;
	}

	dst_buffer = calloc(1, min_dst_sha_buffer_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, min_dst_sha_buffer_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range destination memory map: %s", doca_error_get_descr(result));
		free(dst_buffer);
		return result;
	}
	result = doca_mmap_set_free_cb(state->dst_mmap, &free_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of destination memory map: %s", doca_error_get_descr(result));
		free(dst_buffer);
		return result;
	}
	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start destination memory map: %s", doca_error_get_descr(result));
		free(dst_buffer);
		return result;
	}

	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->dst_mmap, dst_buffer, min_dst_sha_buffer_size,
						    dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_error_get_descr(result));
		return result;
	}
	return result;
}


/*
 * Construct DOCA SHA task, submit it and print the result
 *
 * @state [in]: application configuration struct
 * @sha_ctx [in]: context of SHA library
 * @dst_doca_buf [in]: destination doca buffer
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
calculate_sha(struct program_core_objects *state, struct doca_sha *sha_ctx, struct doca_buf **dst_doca_buf, char *file_data, size_t file_size)
{
	struct doca_buf *src_doca_buf;
	struct doca_sha_task_hash *sha_hash_task = NULL;
	struct doca_task *task = NULL;
	size_t num_remaining_tasks = 0;
	union doca_data ctx_user_data = {0};
	union doca_data task_user_data = {0};
	doca_error_t result, task_result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_error_get_descr(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_set_free_cb(state->src_mmap, &unmap_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of source memory map: %s",
			     doca_error_get_descr(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start source memory map: %s", doca_error_get_descr(result));
		munmap(file_data, file_size);
		return result;
	}

	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
			     doca_error_get_descr(result));
		return result;
	}

	result = doca_buf_set_data(src_doca_buf, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure");
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return result;
	}

	result = populate_dst_buf(state, dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return result;
	}

	/* Include tasks counter in user data of context to be decremented in callbacks */
	ctx_user_data.ptr = &num_remaining_tasks;
	doca_ctx_set_user_data(state->ctx, ctx_user_data);

	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return result;
	}

	/* Include result in user data of task to be used in the callbacks */
	task_user_data.ptr = &task_result;
	/* Allocate and construct SHA hash task */
	result = doca_sha_task_hash_alloc_init(sha_ctx, SHA_ALGORITHM, src_doca_buf, *dst_doca_buf, task_user_data, &sha_hash_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate SHA hash task: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return result;
	}

	task = doca_sha_task_hash_as_task(sha_hash_task);

	/* Submit SHA hash task */
	num_remaining_tasks++;
	result = doca_task_submit(task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit SHA hash task: %s", doca_error_get_descr(result));
		doca_task_free(task);
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return result;
	}

	/* Wait for all tasks to be completed */
	while (num_remaining_tasks > 0) {
		if (doca_pe_progress(state->pe) == 0)
			nanosleep(&ts, &ts);
	}

	result = task_result;

	/* Check result of task according to the result we update in the callbacks */
	if (task_result != DOCA_SUCCESS)
		DOCA_LOG_ERR("SHA hash task failed: %s", doca_error_get_descr(task_result));

	doca_task_free(task);
	doca_buf_dec_refcount(src_doca_buf, NULL);

	return result;
}

/*
 * Receive messages from the client and calculate SHA using partial hash tasks on all the messages
 *
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @state [in]: application configuration struct
 * @sha_ctx [in]: context of SHA library
 * @dst_doca_buf [in]: destination doca buffer
 * @total_msgs [in]: The number of total messages that are expected to be sent by the client
 * @fd [in]: File descriptor of the file that we want to write on
 * @timeout [in]: Application timeout in seconds
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
receive_and_calculate_partial_sha(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
				  struct program_core_objects *state, struct doca_sha *sha_ctx,
				  struct doca_buf **dst_doca_buf, uint32_t total_msgs, int fd, int timeout)
{
	struct doca_buf *src_doca_buf = NULL;
	struct doca_sha_task_partial_hash *sha_partial_hash_task = NULL;
	struct doca_task *task = NULL;
	union doca_data ctx_user_data = {0};
	union doca_data task_user_data = {0};
	char received_msg[MAX_MSG_SIZE];
	size_t msg_len, num_remaining_tasks = 0;
	uint32_t i;
	uint64_t max_source_buffer_size;
	int counter;
	int num_of_iterations = (timeout * 1000 * 1000) / (SLEEP_IN_NANOS / 1000);
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result, task_result;

	result = doca_sha_cap_get_max_src_buffer_size(doca_dev_as_devinfo(state->dev), &max_source_buffer_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get maximum source buffer size for DOCA SHA: %s", doca_error_get_descr(result));
		return result;
	}

	if (max_source_buffer_size < MAX_MSG_SIZE) {
		DOCA_LOG_ERR("Comm Channel message buffer size %d is greater than DOCA SHA maximum buffer size: %lu",
			     MAX_MSG_SIZE, max_source_buffer_size);
		return DOCA_ERROR_BAD_STATE;
	}

	result = populate_dst_buf(state, dst_doca_buf);
	if (result != DOCA_SUCCESS)
		return result;

	/* Include tasks counter in user data of context to be decremented in callbacks */
	ctx_user_data.ptr = &num_remaining_tasks;
	doca_ctx_set_user_data(state->ctx, ctx_user_data);

	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DOCA context: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_mmap_set_memrange(state->src_mmap, received_msg, MAX_MSG_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start source memory map: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_buf_inventory_buf_get_by_data(state->buf_inv, state->src_mmap, received_msg, MAX_MSG_SIZE,
						    &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
			     doca_error_get_descr(result));
		return result;
	}

	/* Include result in user data of task to be used in the callbacks */
	task_user_data.ptr = &task_result;
	/* Allocate and construct SHA partial hash task. We will reuse this task for submitting all partial hash task */
	result = doca_sha_task_partial_hash_alloc_init(sha_ctx, SHA_ALGORITHM, src_doca_buf, *dst_doca_buf,
						       task_user_data, &sha_partial_hash_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate SHA partial hash task: %s", doca_error_get_descr(result));
		goto destroy_src_buf;
	}

	task = doca_sha_task_partial_hash_as_task(sha_partial_hash_task);
	if (task == NULL) {
		DOCA_LOG_ERR("Failed to get SHA partial hash task as DOCA task: %s", doca_error_get_descr(result));
		goto free_task;
	}

	/* receive the file and send partial hash task for each received segment */
	for (i = 0; i < total_msgs; i++) {
		memset(received_msg, 0, MAX_MSG_SIZE);
		msg_len = MAX_MSG_SIZE;
		counter = 0;
		while ((result = doca_comm_channel_ep_recvfrom(ep, received_msg, &msg_len, DOCA_CC_MSG_FLAG_NONE,
							       peer_addr)) == DOCA_ERROR_AGAIN) {
			msg_len = MAX_MSG_SIZE;
			nanosleep(&ts, &ts);
			counter++;
			if (counter == num_of_iterations) {
				DOCA_LOG_ERR("Message was not received at the given timeout");
				result = DOCA_ERROR_TIME_OUT;
				goto free_task;
			}
		}
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not received: %s", doca_error_get_descr(result));
			goto free_task;
		}

		DOCA_LOG_TRC("Received message #%d", i+1);

		/* Set data address and length in the doca_buf */
		result = doca_buf_set_data(src_doca_buf, received_msg, msg_len);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("doca_buf_set_data() for request doca_buf failure: %s", doca_error_get_descr(result));
			goto free_task;
		}

		result = doca_sha_task_partial_hash_set_src(sha_partial_hash_task, src_doca_buf);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set source for SHA partial hash task: %s", doca_error_get_descr(result));
			goto free_task;
		}

		if ((size_t)write(fd, received_msg, msg_len) != msg_len) {
			DOCA_LOG_ERR("Failed to write the received message into the input file");
			result = DOCA_ERROR_IO_FAILED;
			goto free_task;
		}

		/* If we got to final task then mark it as such */
		if (i == (total_msgs - 1)) {
			result = doca_sha_task_partial_hash_set_is_final_buffer(sha_partial_hash_task);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to set final buffer for SHA partial hash task: %s", doca_error_get_descr(result));
				goto free_task;
			}
		}

		num_remaining_tasks += 1;
		/* Submit SHA partial hash task */
		result = doca_task_submit(task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to submit SHA partial hash task: %s", doca_error_get_descr(result));
			goto free_task;
		}

		/* Wait for the task to be completed */
		while (num_remaining_tasks > 0) {
			if (doca_pe_progress(state->pe) == 0)
				nanosleep(&ts, &ts);
		}

		/* Check result of task according to the result we update in the callbacks */
		if (task_result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("SHA partial hash task failed: %s", doca_error_get_descr(task_result));
			goto free_task;
		}
	}

free_task:
	doca_task_free(task);
destroy_src_buf:
	doca_buf_dec_refcount(src_doca_buf, NULL);

	return result;
}

/*
 * Send the input file with comm channel to the server in segments of MAX_MSG_SIZE
 *
 * @dev [in]: DOCA device
 * @ep [in]: handle for comm channel local endpoint
 * @peer_addr [in]: destination address handle of the send operation
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
send_file(struct doca_dev *dev, struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
	 char *file_data, size_t file_size)
{
	uint32_t total_msgs;
	uint32_t total_msgs_msg;
	size_t msg_len;
	uint32_t i, partial_block_size, min_partial_block_size;
	doca_error_t result;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* Get the partial block size */
	result = doca_sha_cap_get_partial_hash_block_size(doca_dev_as_devinfo(dev), SHA_ALGORITHM, &min_partial_block_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the partial hash block size for DOCA SHA: %s", doca_error_get_descr(result));
		return result;
	}

	/*
	 * Calculate the biggest partial block size that is smaller than the MAX_MSG_SIZE
	 * A valid partial block size must be a multiple of min_partial_block_size
	 */
	partial_block_size = MAX_MSG_SIZE - (MAX_MSG_SIZE % min_partial_block_size);

	/*
	 * Send to the server the number of messages needed for receiving the file
	 * The number of messages is equal to the size of the file divided by the size of the partial block size
	 * of the partial hash task, and rounding up
	 */
	total_msgs = 1 + ((file_size - 1) / partial_block_size);
	total_msgs_msg = htonl(total_msgs);

	while ((result = doca_comm_channel_ep_sendto(ep, &total_msgs_msg, sizeof(uint32_t), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not sent: %s", doca_error_get_descr(result));
		return result;
	}

	/* Send file to the server */
	for (i = 0; i < total_msgs; i++) {
		msg_len = MIN(file_size, partial_block_size);
		while ((result = doca_comm_channel_ep_sendto(ep, file_data, msg_len, DOCA_CC_MSG_FLAG_NONE,
							     *peer_addr)) == DOCA_ERROR_AGAIN)
			nanosleep(&ts, &ts);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not sent: %s", doca_error_get_descr(result));
			return result;
		}
		file_data += msg_len;
		file_size -= msg_len;
	}
	return DOCA_SUCCESS;
}

doca_error_t
file_integrity_client(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
		      struct file_integrity_config *app_cfg, struct program_core_objects *state, struct doca_sha *sha_ctx)
{
	struct doca_buf *dst_doca_buf;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	char *file_data, *sha_output;
	uint8_t *sha_msg;
	char msg[MAX_MSG_SIZE] = {0};
	size_t msg_len, hash_length, i;
	struct stat statbuf;
	int fd;
	uint64_t max_source_buffer_size;
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

	result = doca_sha_cap_get_max_src_buffer_size(doca_dev_as_devinfo(state->dev), &max_source_buffer_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get maximum source buffer size for DOCA SHA: %s", doca_error_get_descr(result));
		close(fd);
		return result;
	}

	if (statbuf.st_size <= 0 || (uint64_t)statbuf.st_size > max_source_buffer_size) {
		DOCA_LOG_ERR("Invalid file size. Should be greater then zero and smaller than %lu", max_source_buffer_size);
		close(fd);
		return DOCA_ERROR_INVALID_VALUE;
	}

	file_data = mmap(NULL, statbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (file_data == MAP_FAILED) {
		DOCA_LOG_ERR("Unable to map file content: %s", strerror(errno));
		close(fd);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Calculate SHA */
	result = calculate_sha(state, sha_ctx, &dst_doca_buf, file_data, statbuf.st_size);
	if (result != DOCA_SUCCESS) {
		close(fd);
		return result;
	}

	result = doca_buf_get_data_len(dst_doca_buf, &hash_length);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data length of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		close(fd);
		return result;
	}
	result = doca_buf_get_data(dst_doca_buf, (void **)&sha_msg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		close(fd);
		return result;
	}

	/* Engine outputs hex format. For char format output, we need double the length */
	sha_output = calloc(1, (hash_length * 2) + 1);
	if (sha_output == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		close(fd);
		return DOCA_ERROR_NO_MEMORY;
	}

	for (i = 0; i < hash_length; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", sha_msg[i]);
	DOCA_LOG_INFO("SHA256 output is: %s", sha_output);

	free(sha_output);

	/* Send file SHA to the server */
	while ((result = doca_comm_channel_ep_sendto(ep, sha_msg, hash_length, DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not sent: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		close(fd);
		return result;
	}

	doca_buf_dec_refcount(dst_doca_buf, NULL);

	/* Send the file content to the server */
	result = send_file(state->dev, ep, peer_addr, file_data, statbuf.st_size);
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
		DOCA_LOG_ERR("Finish message was not received: %s", doca_error_get_descr(result));
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
	struct doca_buf *dst_doca_buf = NULL;
	struct doca_buf *src_doca_buf = NULL;
	struct doca_task *task = NULL;
	uint8_t *received_sha = NULL;
	char received_msg[MAX_MSG_SIZE];
	uint32_t i, total_msgs, received_sha_msg_size, partial_block_size;
	size_t msg_len, hash_length;
	char *sha_output;
	int fd;
	uint8_t *file_sha;
	char finish_msg[] = "Server was done receiving messages";
	int counter;
	int num_of_iterations = (app_cfg->timeout * 1000 * 1000) / (SLEEP_IN_NANOS / 1000);
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result, tmp_result;

	/* Get size of SHA output */
	result = doca_sha_cap_get_min_dst_buffer_size(doca_dev_as_devinfo(state->dev), SHA_ALGORITHM, &received_sha_msg_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get minimum destination buffer size for DOCA SHA: %s", doca_error_get_descr(result));
		return result;
	}
	/* Get size of SHA output */
	result = doca_sha_cap_get_partial_hash_block_size(doca_dev_as_devinfo(state->dev), SHA_ALGORITHM, &partial_block_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get minimum destination buffer size for DOCA SHA: %s", doca_error_get_descr(result));
		return result;
	}
	received_sha = calloc(1, received_sha_msg_size);
	if (received_sha == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	/* receive file SHA from the client */
	msg_len = MAX_MSG_SIZE;
	while ((result = doca_comm_channel_ep_recvfrom(ep, received_sha, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       peer_addr)) == DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not received: %s", doca_error_get_descr(result));
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
		DOCA_LOG_ERR("Message was not received: %s", doca_error_get_descr(result));
		goto finish_msg;
	}

	if (msg_len != sizeof(uint32_t)) {
		DOCA_LOG_ERR("Received wrong message size, required %ld, got %ld", sizeof(uint32_t), msg_len);
		goto finish_msg;
	}
	total_msgs = ntohl(*(uint32_t *)received_msg);

	if (total_msgs < 1) {
		DOCA_LOG_ERR("Received wrong message, must be bigger than zero");
		result = DOCA_ERROR_UNEXPECTED;
		goto finish_msg;
	}

	fd = open(app_cfg->file_path, O_CREAT | O_WRONLY, S_IRUSR | S_IRGRP);
	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", app_cfg->file_path);
		result = DOCA_ERROR_IO_FAILED;
		goto finish_msg;
	}

	/*
	 * If the number of message is only 1, then receive one message and perform regular SHA.
	 * Else receive all the messages and perform partial SHA
	 */
	if (total_msgs == 1) {
		memset(received_msg, 0, MAX_MSG_SIZE);
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
				doca_buf_dec_refcount(dst_doca_buf, NULL);
				goto finish_msg;
			}
		}
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not received: %s", doca_error_get_descr(result));
			close(fd);
			doca_buf_dec_refcount(dst_doca_buf, NULL);
			goto finish_msg;
		}

		/* Write receive message to file */
		if ((size_t)write(fd, received_msg, msg_len) != msg_len) {
			DOCA_LOG_ERR("Failed to write the received message into the input file");
			close(fd);
			doca_buf_dec_refcount(dst_doca_buf, NULL);
			result = DOCA_ERROR_IO_FAILED;
			goto finish_msg;
		}

		/* Calculate SHA */
		result = calculate_sha(state, sha_ctx, &dst_doca_buf, received_msg, msg_len);
		if (result != DOCA_SUCCESS) {
			close(fd);
			doca_buf_dec_refcount(dst_doca_buf, NULL);
			goto finish_msg;
		}
	} else {
		/* Receive the messages and calculate partial SHA */
		result = receive_and_calculate_partial_sha(ep, peer_addr, state, sha_ctx, &dst_doca_buf, total_msgs, fd,
							   app_cfg->timeout);
		if (result != DOCA_SUCCESS) {
			close(fd);
			doca_buf_dec_refcount(dst_doca_buf, NULL);
			goto finish_msg;
		}
	}

	close(fd);

	/* compare received SHA with calculated SHA */
	result = doca_buf_get_data_len(dst_doca_buf, &hash_length);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data length of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		goto finish_msg;
	}

	result = doca_buf_get_data(dst_doca_buf, (void **)&file_sha);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get the data of DOCA buffer: %s", doca_error_get_descr(result));
		doca_buf_dec_refcount(dst_doca_buf, NULL);
		goto finish_msg;
	}

	/* Engine outputs hex format. For char format output, we need double the length */
	sha_output = calloc(1, (hash_length * 2) + 1);
	if (sha_output == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		doca_task_free(task);
		doca_buf_dec_refcount(src_doca_buf, NULL);
		return DOCA_ERROR_NO_MEMORY;
	}

	for (i = 0; i < hash_length; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", file_sha[i]);
	DOCA_LOG_INFO("SHA256 output is: %s", sha_output);

	free(sha_output);

	if (memcmp(file_sha, received_sha, hash_length) == 0)
		DOCA_LOG_INFO("SUCCESS: file SHA is identical to received SHA");
	else {
		DOCA_LOG_ERR("ERROR: SHA is not identical, file was compromised");
		if (remove(app_cfg->file_path) < 0)
			DOCA_LOG_ERR("Failed to remove %s", app_cfg->file_path);
	}

	doca_buf_dec_refcount(dst_doca_buf, NULL);

finish_msg:
	/* Send finish message to the client */
	while ((tmp_result = doca_comm_channel_ep_sendto(ep, finish_msg, sizeof(finish_msg), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);

	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to send finish message: %s", doca_error_get_descr(result));
		if (result == DOCA_SUCCESS)
			result = tmp_result;
	}
	free(received_sha);

	return result;
}

/*
 * SHA hash task completed callback
 *
 * @sha_hash_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
sha_hash_completed_callback(struct doca_sha_task_hash *sha_hash_task, union doca_data task_user_data,
			  union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	(void)sha_hash_task;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Assign success to the result */
	*result = DOCA_SUCCESS;
}

/*
 * SHA hash task error callback
 *
 * @sha_hash_task [in]: Failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
sha_hash_error_callback(struct doca_sha_task_hash *sha_hash_task, union doca_data task_user_data,
			union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	struct doca_task *task = doca_sha_task_hash_as_task(sha_hash_task);
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Get the result of the task */
	*result = doca_task_get_status(task);
}

/*
 * SHA partial hash task completed callback
 *
 * @sha_partial_hash_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
sha_partial_hash_completed_callback(struct doca_sha_task_partial_hash *sha_partial_hash_task, union doca_data task_user_data,
				    union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	(void)sha_partial_hash_task;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Assign success to the result */
	*result = DOCA_SUCCESS;
}

/*
 * SHA partial hash task error callback
 *
 * @sha_partial_hash_task [in]: Failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void
sha_partial_hash_error_callback(struct doca_sha_task_partial_hash *sha_partial_hash_task, union doca_data task_user_data,
				union doca_data ctx_user_data)
{
	size_t *num_remaining_tasks = (size_t *)ctx_user_data.ptr;
	struct doca_task *task = doca_sha_task_partial_hash_as_task(sha_partial_hash_task);
	doca_error_t *result = (doca_error_t *)task_user_data.ptr;

	/* Decrement number of remaining tasks */
	--*num_remaining_tasks;
	/* Get the result of the task */
	*result = doca_task_get_status(task);
}

/*
 * Check if given device is capable of executing a SHA partial hash task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports SHA hash task and DOCA_ERROR otherwise.
 */
static doca_error_t
sha_partial_hash_is_supported(struct doca_devinfo *devinfo)
{
	return doca_sha_cap_task_partial_hash_get_supported(devinfo, SHA_ALGORITHM);
}

doca_error_t
file_integrity_init(struct doca_comm_channel_ep_t **ep, struct doca_comm_channel_addr_t **peer_addr,
		struct file_integrity_config *app_cfg, struct program_core_objects *state,
		struct doca_sha **sha_ctx)
{
	struct doca_dev *cc_doca_dev;
	struct doca_dev_rep *cc_doca_dev_rep = NULL;
	struct timespec ts = {0};
	uint32_t max_bufs = 2;    /* The app will use 2 doca buffers */
	doca_error_t result;

	/* set default timeout */
	if (app_cfg->timeout == 0)
		app_cfg->timeout = DEFAULT_TIMEOUT;

	/* Create Comm Channel endpoint */
	result = doca_comm_channel_ep_create(ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create Comm Channel endpoint: %s", doca_error_get_descr(result));
		return result;
	}

	result = open_doca_device_with_pci(app_cfg->cc_dev_pci_addr, NULL, &cc_doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init sha library: %s", doca_error_get_descr(result));
		goto cc_ep_destroy;
	}

	if (app_cfg->mode == SERVER) {
		result = open_doca_device_rep_with_pci(cc_doca_dev, DOCA_DEVINFO_REP_FILTER_NET,
						       app_cfg->cc_dev_rep_pci_addr, &cc_doca_dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open representor device: %s", doca_error_get_descr(result));
			goto dev_close;
		}
	}

	/* Set ep attributes */
	result = set_endpoint_properties(app_cfg->mode, *ep, cc_doca_dev, cc_doca_dev_rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CC ep attributes: %s", doca_error_get_descr(result));
		goto rep_dev_close;
	}

	/* Open device for partial SHA tasks */
	result = open_doca_device_with_capabilities(&sha_partial_hash_is_supported, &state->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA device with SHA capabilities: %s", doca_error_get_descr(result));
		goto rep_dev_close;
	}

	result = doca_sha_create(state->dev, sha_ctx);
	if (result != DOCA_SUCCESS) {
		doca_comm_channel_ep_destroy(*ep);
		DOCA_LOG_ERR("Failed to init sha library: %s", doca_error_get_descr(result));
		goto destroy_core_objs;
	}

	state->ctx = doca_sha_as_ctx(*sha_ctx);

	result = create_core_objects(state, max_bufs);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA core objects: %s", doca_error_get_descr(result));
		goto destroy_core_objs;
	}

	/* Connect context to progress engine */
	result = doca_pe_connect_ctx(state->pe, state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to connect progress engine to context: %s", doca_error_get_descr(result));
		goto destroy_core_objs;
	}

	if (app_cfg->mode == CLIENT) {
		/* Set SHA hash task configuration for the client */
		result = doca_sha_task_hash_set_conf(*sha_ctx, sha_hash_completed_callback, sha_hash_error_callback,
						     LOG_NUM_SHA_TASKS);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set configuration for SHA hash task: %s", doca_error_get_descr(result));
			goto destroy_core_objs;
		}

		result = doca_comm_channel_ep_connect(*ep, SERVER_NAME, peer_addr);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Couldn't establish a connection with the server node: %s", doca_error_get_descr(result));
			goto destroy_core_objs;
		}

		while ((result = doca_comm_channel_peer_addr_update_info(*peer_addr)) == DOCA_ERROR_CONNECTION_INPROGRESS)
			nanosleep(&ts, &ts);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to validate the connection with the DPU: %s", doca_error_get_descr(result));
			goto destroy_core_objs;
		}

		DOCA_LOG_INFO("Connection to DPU was established successfully");
	} else {
		result = doca_sha_task_hash_set_conf(*sha_ctx, sha_hash_completed_callback, sha_hash_error_callback,
						     LOG_NUM_SHA_TASKS);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set configuration for SHA hash task: %s", doca_error_get_descr(result));
			goto destroy_core_objs;
		}

		/* Set SHA partial hash task configuration for the client */
		result = doca_sha_task_partial_hash_set_conf(*sha_ctx, sha_partial_hash_completed_callback,
							sha_partial_hash_error_callback, LOG_NUM_SHA_TASKS);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set configuration for SHA partial hash task: %s", doca_error_get_descr(result));
			goto destroy_core_objs;
		}

		result = doca_comm_channel_ep_listen(*ep, SERVER_NAME);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Comm channel server couldn't start listening: %s", doca_error_get_descr(result));
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
cc_ep_destroy:
	doca_comm_channel_ep_destroy(*ep);
	return result;
}

void
file_integrity_cleanup(struct program_core_objects *state, struct doca_sha *sha_ctx,
		struct doca_comm_channel_ep_t *ep, enum file_integrity_mode mode, struct doca_comm_channel_addr_t **peer_addr)
{
	doca_error_t result;
	struct doca_dev *cc_doca_dev;
	struct doca_dev_rep *cc_doca_dev_rep = NULL;

	result = doca_comm_channel_ep_disconnect(ep, *peer_addr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to disconnect channel: %s", doca_error_get_descr(result));

	if (mode == SERVER) {
		result = doca_comm_channel_ep_get_device_rep(ep, &cc_doca_dev_rep);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to get DOCA device representor property");
		else
			doca_dev_rep_close(cc_doca_dev_rep);
	}

	result = doca_comm_channel_ep_get_device(ep, &cc_doca_dev);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to get channel device: %s", doca_error_get_descr(result));
	else
		doca_dev_close(cc_doca_dev);

	result = doca_comm_channel_ep_destroy(ep);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy channel: %s", doca_error_get_descr(result));

	if (state->pe != NULL && state->ctx != NULL) {
		result = request_stop_ctx(state->pe, state->ctx);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy DOCA SHA: %s", doca_error_get_descr(result));
		state->ctx = NULL;
	}

	if (sha_ctx != NULL) {
		result = doca_sha_destroy(sha_ctx);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy DOCA SHA: %s", doca_error_get_descr(result));
	}

	result = destroy_core_objects(state);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy core objects: %s", doca_error_get_descr(result));
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
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
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
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register Comm Channel DOCA device representor PCI address */
	result = doca_argp_param_create(&rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(rep_pci_addr_param, "r");
	doca_argp_param_set_long_name(rep_pci_addr_param, "rep-pci");
	doca_argp_param_set_description(rep_pci_addr_param, "DOCA Comm Channel device representor PCI address");
	doca_argp_param_set_callback(rep_pci_addr_param, rep_pci_addr_callback);
	doca_argp_param_set_type(rep_pci_addr_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register message to send param */
	result = doca_argp_param_create(&file_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
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
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Create and register timeout */
	result = doca_argp_param_create(&timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_error_get_descr(result));
		return result;
	}
	doca_argp_param_set_short_name(timeout_param, "t");
	doca_argp_param_set_long_name(timeout_param, "timeout");
	doca_argp_param_set_description(timeout_param, "Application timeout for receiving file content messages, default is 5 sec");
	doca_argp_param_set_callback(timeout_param, timeout_callback);
	doca_argp_param_set_type(timeout_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(timeout_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_error_get_descr(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_error_get_descr(result));
		return result;
	}

	/* Register application callback */
	result = doca_argp_register_validation_callback(args_validation_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program validation callback: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}
