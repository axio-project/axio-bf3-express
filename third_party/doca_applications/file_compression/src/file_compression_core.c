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
#include <zlib.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <pack.h>
#include <utils.h>

#include "file_compression_core.h"

#define MAX_MSG 512				/* Maximum number of messages in CC queue */
#define SLEEP_IN_NANOS (10 * 1000)		/* Sample the job every 10 microseconds */
#define MAX_FILE_SIZE (128 * 1024 * 1024)	/* 128 Mbytes */
#define DECOMPRESS_RATIO 1032			/* Maximal decompress ratio size */
#define DEFAULT_TIMEOUT 10			/* default timeout for receiving messages */
#define SERVER_NAME "file_compression_server"	/* CC server name */

DOCA_LOG_REGISTER(FILE_COMPRESSION::Core);

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
set_endpoint_properties(enum file_compression_mode mode, struct doca_comm_channel_ep_t *ep, struct doca_dev *dev, struct doca_dev_rep *dev_rep)
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
 * Submit compress job and retrieve the result
 *
 * @state [in]: application configuration struct
 * @job [in]: job to submit
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
process_job(struct program_core_objects *state, const struct doca_job *job)
{
	struct doca_event event = {0};
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	/* Enqueue job */
	result = doca_workq_submit(state->workq, job);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit doca job: %s", doca_get_error_string(result));
		return result;
	}

	/* Wait for job completion */
	while ((result = doca_workq_progress_retrieve(state->workq, &event, DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) ==
	       DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
	}

	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to retrieve job: %s", doca_get_error_string(result));
	else if (event.result.u64 != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Job finished unsuccessfully");
		result = event.result.u64;
	} else
		result = DOCA_SUCCESS;

	return result;
}

/*
 * Populate destination doca buffer for compress jobs
 *
 * @state [in]: application configuration struct
 * @dst_buffer [in]: destination buffer
 * @dst_buf_size [in]: destination buffer size
 * @dst_doca_buf [out]: created doca buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
populate_dst_buf(struct program_core_objects *state, uint8_t **dst_buffer, size_t dst_buf_size, struct doca_buf **dst_doca_buf)
{
	doca_error_t result;

	dst_buffer = calloc(1, dst_buf_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, dst_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}

	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to start destination memory map: %s", doca_get_error_string(result));
		free(dst_buffer);
		return result;
	}

	result = doca_buf_inventory_buf_by_addr(state->buf_inv, state->dst_mmap, dst_buffer, dst_buf_size,
						dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_get_error_string(result));
		return result;
	}
	return result;
}

/*
 * Construct compress job and submit it
 *
 * @state [in]: application configuration struct
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @job_type [in]: compress job type - compress for client and decompress for server
 * @dst_buf_size [in]: allocated destination buffer length
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @output_chksum [out]: the returned checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file_hw(struct program_core_objects *state, char *file_data, size_t file_size,
	      enum doca_compress_job_types job_type, size_t dst_buf_size, uint8_t **compressed_file,
	      size_t *compressed_file_len, uint64_t *output_chksum)
{
	struct doca_buf *dst_doca_buf;
	struct doca_buf *src_doca_buf;
	uint8_t *resp_head;
	doca_error_t result;

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set memory range of source memory map: %s", doca_get_error_string(result));
		munmap(file_data, file_size);
		return result;
	}
	result = doca_mmap_set_free_cb(state->src_mmap, &unmap_cb, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set free callback of source memory map: %s", doca_get_error_string(result));
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
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s", doca_get_error_string(result));
		return result;
	}

	doca_buf_get_data(src_doca_buf, (void **)&resp_head);
	doca_buf_set_data(src_doca_buf, resp_head, file_size);

	result = populate_dst_buf(state, compressed_file, dst_buf_size, &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	/* Construct compress job */
	const struct doca_compress_deflate_job compress_job = {
		.base = (struct doca_job) {
			.type = job_type,
			.flags = DOCA_JOB_FLAGS_NONE,
			.ctx = state->ctx,
			},
		.dst_buff = dst_doca_buf,
		.src_buff = src_doca_buf,
		.output_chksum = output_chksum,
	};

	result = process_job(state, &compress_job.base);
	if (result != DOCA_SUCCESS) {
		doca_buf_refcount_rm(dst_doca_buf, NULL);
		doca_buf_refcount_rm(src_doca_buf, NULL);
		return result;
	}

	doca_buf_refcount_rm(src_doca_buf, NULL);

	doca_buf_get_head(dst_doca_buf, (void **)compressed_file);
	doca_buf_get_data_len(dst_doca_buf, compressed_file_len);
	doca_buf_refcount_rm(dst_doca_buf, NULL);

	return DOCA_SUCCESS;
}

/*
 * Compress the input file in SW
 *
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @dst_buf_size [in]: allocated destination buffer length
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file_sw(char *file_data, size_t file_size, size_t dst_buf_size, Byte **compressed_file, uLong *compressed_file_len)
{
	z_stream c_stream; /* compression stream */
	int err;

	memset(&c_stream, 0, sizeof(c_stream));

	c_stream.zalloc = NULL;
	c_stream.zfree = NULL;

	err = deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	c_stream.next_in  = (z_const unsigned char *)file_data;
	c_stream.next_out = *compressed_file;

	c_stream.avail_in = file_size;
	c_stream.avail_out = dst_buf_size;
	err = deflate(&c_stream, Z_NO_FLUSH);
	if (err < 0) {
		DOCA_LOG_ERR("Failed to compress file");
		return DOCA_ERROR_BAD_STATE;
	}

	/* Finish the stream */
	err = deflate(&c_stream, Z_FINISH);
	if (err < 0 || err != Z_STREAM_END) {
		DOCA_LOG_ERR("Failed to compress file");
		return DOCA_ERROR_BAD_STATE;
	}

	err = deflateEnd(&c_stream);
	if (err < 0) {
		DOCA_LOG_ERR("Failed to compress file");
		return DOCA_ERROR_BAD_STATE;
	}
	*compressed_file_len = c_stream.total_out;
	return DOCA_SUCCESS;
}

/*
 * Calculate file checksum with zlib, where the lower 32 bits contain the CRC checksum result
 * and the upper 32 bits contain the Adler checksum result.
 *
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @output_chksum [out]: the calculated checksum
 */
static void
calculate_checksum_sw(char *file_data, size_t file_size, uint64_t *output_chksum)
{
	uint32_t crc;
	uint32_t adler;
	uint64_t result_checksum;

	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const unsigned char *)file_data, file_size);
	adler = adler32(0L, Z_NULL, 0);
	adler = adler32(adler, (const unsigned char *)file_data, file_size);

	result_checksum = adler;
	result_checksum <<= 32;
	result_checksum += crc;

	*output_chksum = result_checksum;
}

/*
 * Compress / decompress the input file data
 *
 * @state [in]: application configuration struct
 * @file_data [in]: file data to the source buffer
 * @file_size [in]: file size
 * @job_type [in]: compress job type - compress for client and decompress for server
 * @compress_method [in]: compress with software or hardware
 * @compressed_file [out]: destination buffer with the result
 * @compressed_file_len [out]: destination buffer size
 * @output_chksum [out]: the calculated checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
compress_file(struct program_core_objects *state, char *file_data, size_t file_size, enum doca_compress_job_types job_type,
	      enum file_compression_compress_method compress_method, uint8_t **compressed_file, size_t *compressed_file_len,
	      uint64_t *output_chksum)
{
	size_t dst_buf_size = 0;

	if (job_type == DOCA_COMPRESS_DEFLATE_JOB) {
		dst_buf_size = MAX(file_size + 16, file_size * 2);
		if (dst_buf_size > MAX_FILE_SIZE)
			dst_buf_size = MAX_FILE_SIZE;
	} else if (job_type == DOCA_DECOMPRESS_DEFLATE_JOB)
		dst_buf_size = MIN(MAX_FILE_SIZE, DECOMPRESS_RATIO * file_size);

	*compressed_file = calloc(1, dst_buf_size);
	if (*compressed_file == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		return DOCA_ERROR_NO_MEMORY;
	}

	if (compress_method == COMPRESS_DEFLATE_SW) {
		calculate_checksum_sw(file_data, file_size, output_chksum);
		return compress_file_sw(file_data, file_size, dst_buf_size, compressed_file, compressed_file_len);
	} else
		return compress_file_hw(state, file_data, file_size, job_type, dst_buf_size, compressed_file, compressed_file_len, output_chksum);
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
	uint32_t i;
	size_t msg_len;
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
file_compression_client(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
		      struct file_compression_config *app_cfg, struct program_core_objects *state)
{
	char *file_data;
	char msg[MAX_MSG_SIZE] = {0};
	size_t msg_len;
	struct stat statbuf;
	int fd;
	uint8_t *compressed_file;
	size_t compressed_file_len;
	uint64_t checksum;
	uint64_t checksum_msg;
	doca_error_t result;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};

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

	DOCA_DLOG_INFO("File size: %ld", statbuf.st_size);
	/* Send compress job */
	result = compress_file(state, file_data, statbuf.st_size, DOCA_COMPRESS_DEFLATE_JOB, app_cfg->compress_method, &compressed_file,
			       &compressed_file_len, &checksum);
	if (result != DOCA_SUCCESS) {
		close(fd);
		free(compressed_file);
		return result;
	}
	close(fd);
	DOCA_DLOG_INFO("Compressed file size: %ld", compressed_file_len);

	/* send source file checksum */
	checksum_msg = htonq(checksum);
	while ((result = doca_comm_channel_ep_sendto(ep, &checksum_msg, sizeof(uint64_t), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not sent: %s", doca_get_error_string(result));
		free(compressed_file);
		return result;
	}

	/* Send the file content to the server */
	result = send_file(ep, peer_addr, (char *)compressed_file, compressed_file_len);
	if (result != DOCA_SUCCESS) {
		free(compressed_file);
		return result;
	}
	free(compressed_file);

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
file_compression_server(struct doca_comm_channel_ep_t *ep, struct doca_comm_channel_addr_t **peer_addr,
		struct file_compression_config *app_cfg, struct program_core_objects *state)
{
	char received_msg[MAX_MSG_SIZE] = {0};
	uint32_t i, total_msgs;
	size_t msg_len;
	int fd;
	char finish_msg[] = "Server was done receiving messages";
	char *compressed_file = NULL;
	char *tmp_compressed_file;
	uint8_t *resp_head;
	uint64_t checksum;
	size_t data_len;
	uint64_t received_checksum;
	int counter;
	int num_of_iterations = (app_cfg->timeout * 1000 * 1000) / (SLEEP_IN_NANOS / 1000);
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};
	doca_error_t result;

	/* receive file checksum from the client */
	msg_len = MAX_MSG_SIZE;
	while ((result = doca_comm_channel_ep_recvfrom(ep, received_msg, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       peer_addr)) == DOCA_ERROR_AGAIN) {
		nanosleep(&ts, &ts);
		msg_len = MAX_MSG_SIZE;
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Message was not received: %s", doca_get_error_string(result));
		goto finish_msg;
	}
	if (msg_len != sizeof(uint64_t)) {
		DOCA_LOG_ERR("Received wrong message size, required %ld, got %ld", sizeof(uint64_t), msg_len);
		goto finish_msg;
	}
	received_checksum = ntohq(*(uint64_t *)received_msg);

	/* receive number of total msgs from the client */
	msg_len = MAX_MSG_SIZE;
	counter = 0;
	while ((result = doca_comm_channel_ep_recvfrom(ep, received_msg, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       peer_addr)) == DOCA_ERROR_AGAIN) {
		msg_len = MAX_MSG_SIZE;
		nanosleep(&ts, &ts);
		counter++;
		if (counter == num_of_iterations) {
			DOCA_LOG_ERR("Message was not received at the given timeout");
			goto finish_msg;
		}
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

	compressed_file = calloc(1, MAX_FILE_SIZE);
	if (compressed_file == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory");
		result = DOCA_ERROR_NO_MEMORY;
		goto finish_msg;
	}
	tmp_compressed_file = compressed_file;

	/* receive file */
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
				goto finish_msg;
			}
		}
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Message was not received: %s", doca_get_error_string(result));
			goto finish_msg;
		}

		DOCA_DLOG_DBG("Received message #%d", i+1);
		if (tmp_compressed_file - compressed_file + msg_len > MAX_FILE_SIZE) {
			DOCA_LOG_ERR("Received file exceeded maximum file size");
			goto finish_msg;
		}
		memcpy(tmp_compressed_file, received_msg, msg_len);
		tmp_compressed_file += msg_len;
	}

	result = compress_file(state, compressed_file, tmp_compressed_file - compressed_file, DOCA_DECOMPRESS_DEFLATE_JOB, COMPRESS_DEFLATE_HW, &resp_head, &data_len, &checksum);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decompress the received file");
		free(resp_head);
		goto finish_msg;
	}
	if (checksum == received_checksum)
		DOCA_LOG_INFO("SUCCESS: file was received and decompressed successfully");
	else {
		DOCA_LOG_ERR("ERROR: file checksum is different. received: %ld, calculated: %ld", received_checksum, checksum);
		free(resp_head);
		result = DOCA_ERROR_BAD_STATE;
		goto finish_msg;
	}

	fd = open(app_cfg->file_path, O_CREAT | O_WRONLY, S_IRUSR | S_IRGRP);
	if (fd < 0) {
		DOCA_LOG_ERR("Failed to open %s", app_cfg->file_path);
		free(resp_head);
		result = DOCA_ERROR_IO_FAILED;
		goto finish_msg;
	}

	if ((size_t)write(fd, resp_head, data_len) != data_len) {
		DOCA_LOG_ERR("Failed to write the decompressed into the input file");
		free(resp_head);
		close(fd);
		result = DOCA_ERROR_IO_FAILED;
		goto finish_msg;
	}
	free(resp_head);
	close(fd);

finish_msg:
	if (compressed_file != NULL)
		free(compressed_file);

	/* Send finish message to the client */
	while ((result = doca_comm_channel_ep_sendto(ep, finish_msg, sizeof(finish_msg), DOCA_CC_MSG_FLAG_NONE,
						     *peer_addr)) == DOCA_ERROR_AGAIN)
		nanosleep(&ts, &ts);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to send finish message: %s", doca_get_error_string(result));
	return result;
}

/**
 * Check if given device is capable of executing a DOCA_COMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_COMPRESS_DEFLATE_JOB and DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_compress_is_supported(struct doca_devinfo *devinfo)
{
	return doca_compress_job_get_supported(devinfo, DOCA_COMPRESS_DEFLATE_JOB);
}

/**
 * Check if given device is capable of executing a DOCA_DECOMPRESS_DEFLATE_JOB.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA_DECOMPRESS_DEFLATE_JOB and DOCA_ERROR otherwise.
 */
static doca_error_t
compress_jobs_decompress_is_supported(struct doca_devinfo *devinfo)
{
	return doca_compress_job_get_supported(devinfo, DOCA_DECOMPRESS_DEFLATE_JOB);
}

doca_error_t
file_compression_init(struct doca_comm_channel_ep_t **ep, struct doca_comm_channel_addr_t **peer_addr,
		struct file_compression_config *app_cfg, struct program_core_objects *state,
		struct doca_compress **compress_ctx)
{
	struct doca_dev *cc_doca_dev;
	struct doca_dev_rep *cc_doca_dev_rep = NULL;
	uint32_t workq_depth = 1; /* The app will run 1 compress job at a time */
	uint32_t max_bufs = 2;    /* The app will use 2 doca buffers */
	doca_error_t result;
	struct timespec ts = {
		.tv_nsec = SLEEP_IN_NANOS,
	};

	app_cfg->compress_method = COMPRESS_DEFLATE_HW;

	/* set default timeout */
	if (app_cfg->timeout == 0)
		app_cfg->timeout = DEFAULT_TIMEOUT;

	/* Create Comm Channel endpoint */
	result = doca_comm_channel_ep_create(ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create Comm Channel endpoint: %s", doca_get_error_string(result));
		return result;
	}

	/* create compress library */
	result = doca_compress_create(compress_ctx);
	if (result != DOCA_SUCCESS) {
		doca_comm_channel_ep_destroy(*ep);
		DOCA_LOG_ERR("Failed to init compress library: %s", doca_get_error_string(result));
		return result;
	}

	state->ctx = doca_compress_as_ctx(*compress_ctx);

	/* open DOCA device for CC */
	result = open_doca_device_with_pci(app_cfg->cc_dev_pci_addr, NULL, &cc_doca_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA device: %s", doca_get_error_string(result));
		goto compress_destroy;
	}

	/* open representor device for CC server */
	if (app_cfg->mode == SERVER) {
		result = open_doca_device_rep_with_pci(cc_doca_dev, DOCA_DEV_REP_FILTER_NET,
						       app_cfg->cc_dev_rep_pci_addr, &cc_doca_dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open representor device: %s", doca_get_error_string(result));
			goto dev_close;
		}
	}

	/* open device for compress job */
	if (app_cfg->mode == SERVER) {
		result = open_doca_device_with_capabilities(&compress_jobs_decompress_is_supported, &state->dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open DOCA device for DOCA Compress: %s", doca_get_error_string(result));
			goto rep_dev_close;
		}
	} else {
		result = open_doca_device_with_capabilities(&compress_jobs_compress_is_supported, &state->dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_WARN("Failed to find device for compress job, running SW compress with zlib");
			app_cfg->compress_method = COMPRESS_DEFLATE_SW;
		}
	}

	/* Set ep attributes */
	result = set_endpoint_properties(app_cfg->mode, *ep, cc_doca_dev, cc_doca_dev_rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA core objects: %s", doca_get_error_string(result));
		goto rep_dev_close;
	}

	if (app_cfg->compress_method == COMPRESS_DEFLATE_HW) {
		result = init_core_objects(state, workq_depth, max_bufs);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to init DOCA core objects: %s", doca_get_error_string(result));
			goto destroy_core_objs;
		}
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
			doca_dev_rep_close(cc_doca_dev_rep);
			DOCA_LOG_ERR("Comm channel server couldn't start listening: %s", doca_get_error_string(result));
			goto destroy_core_objs;
		}

		DOCA_LOG_INFO("Started Listening, waiting for new connection");
	}

	return DOCA_SUCCESS;

destroy_core_objs:
	if (app_cfg->compress_method == COMPRESS_DEFLATE_HW)
		destroy_core_objects(state);
rep_dev_close:
	if (app_cfg->mode == SERVER)
		doca_dev_rep_close(cc_doca_dev_rep);
dev_close:
	doca_dev_close(cc_doca_dev);
compress_destroy:
	doca_compress_destroy(*compress_ctx);
	doca_comm_channel_ep_destroy(*ep);
	return result;
}

void
file_compression_cleanup(struct program_core_objects *state, struct file_compression_config *app_cfg,
			 struct doca_compress *compress_ctx, struct doca_comm_channel_ep_t *ep,
			 struct doca_comm_channel_addr_t **peer_addr)
{
	doca_error_t result;

	result = doca_comm_channel_ep_disconnect(ep, *peer_addr);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to disconnect channel: %s", doca_get_error_string(result));

	result = doca_comm_channel_ep_destroy(ep);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy channel: %s", doca_get_error_string(result));

	if (app_cfg->compress_method == COMPRESS_DEFLATE_HW) {
		result = destroy_core_objects(state);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to destroy core objects: %s", doca_get_error_string(result));
	}

	result = doca_compress_destroy(compress_ctx);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy compress: %s", doca_get_error_string(result));
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
	struct file_compression_config *app_cfg = (struct file_compression_config *)config;
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
	struct file_compression_config *app_cfg = (struct file_compression_config *)config;
	char *pci_addr = (char *)param;

	if (strnlen(pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE) == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}
	strlcpy(app_cfg->cc_dev_pci_addr, pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE);
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
	struct file_compression_config *app_cfg = (struct file_compression_config *)config;
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
	struct file_compression_config *app_cfg = (struct file_compression_config *)config;
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
	struct file_compression_config *app_cfg = (struct file_compression_config *)cfg;

	if (app_cfg->mode == CLIENT && (access(app_cfg->file_path, F_OK) == -1)) {
		DOCA_LOG_ERR("File was not found %s", app_cfg->file_path);
		return DOCA_ERROR_NOT_FOUND;
	} else if (app_cfg->mode == SERVER && strlen(app_cfg->cc_dev_rep_pci_addr) == 0) {
		DOCA_LOG_ERR("Missing representor PCI address for server");
		return DOCA_ERROR_NOT_FOUND;
	}
	return DOCA_SUCCESS;
}

doca_error_t
register_file_compression_params(void)
{
	doca_error_t result;

	struct doca_argp_param *dev_pci_addr_param, *rep_pci_addr_param, *file_param, *timeout_param;

	/* Create and register pci param */
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

	/* Create and register rep PCI address param */
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
