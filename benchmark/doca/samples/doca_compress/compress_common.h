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

#ifndef COMPRESS_COMMON_H_
#define COMPRESS_COMMON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <doca_dev.h>
#include <doca_compress.h>
#include <doca_mmap.h>
#include <doca_error.h>

#define USER_MAX_FILE_NAME 255		       /* Max file name length */
#define MAX_FILE_NAME (USER_MAX_FILE_NAME + 1) /* Max file name string length */
#define SLEEP_IN_NANOS (10 * 1000)	       /* Sample the task every 10 microseconds */
#define NUM_COMPRESS_TASKS (1)		       /* Number of compress tasks */
#define ADLER_CHECKSUM_SHIFT (32)	       /* The shift for the Adler checksum within the output checksum */
#define ZLIB_HEADER_SIZE (2)		       /* The header size in zlib */
#define ZLIB_TRAILER_SIZE (4)		       /* The trailer size in zlib (the 32-bit checksum) */
/* Additional memory for zlib compatibility, used in allocation and reading */
#define ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY (ZLIB_HEADER_SIZE + ZLIB_TRAILER_SIZE)

/* Compress modes */
enum compress_mode {
	COMPRESS_MODE_COMPRESS_DEFLATE,	     /* Compress mode */
	COMPRESS_MODE_DECOMPRESS_DEFLATE,    /* Decompress mode with deflate algorithm */
	COMPRESS_MODE_DECOMPRESS_LZ4_STREAM, /* Decompress stream mode with lz4 algorithm */
};

/* Configuration struct */
struct compress_cfg {
	char file_path[MAX_FILE_NAME];		      /* File to compress/decompress */
	char output_path[MAX_FILE_NAME];	      /* Output file */
	char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE]; /* Device PCI address */
	enum compress_mode mode;		      /* Compress task type */
	bool is_with_frame;			      /* Write \ read a file with a frame.
						       * In deflate - compatible with default zlib settings.
						       * In LZ4 - compatible with LZ4 frame format */
	bool has_block_checksum;		      /* For use only in LZ4 stream sample, flag to indicate if blocks
						       * have a checksum */
	bool are_blocks_independent;		      /* For use only in LZ4 stream sample, flag to indicate if blocks
						       * are independent */
	bool output_checksum;			      /* To output checksum or not */
};

/* DOCA compress resources */
struct compress_resources {
	struct program_core_objects *state; /* DOCA program core objects */
	struct doca_compress *compress;	    /* DOCA compress context */
	size_t num_remaining_tasks;	    /* Number of remaining compress tasks */
	enum compress_mode mode;	    /* Compress mode - compress/decompress */
	bool run_pe_progress;		    /* Controls whether progress loop should run */
};

/* A zlib header, for compatibility with third-party applications */
struct compress_zlib_header {
	uint8_t cmf; /**< The CMF byte represents the Compression Method and Compression Info */
	uint8_t flg; /**< The FLG byte represents various flags, including FCHECK and FLEVEL */
} __attribute__((packed));

/*
 * Register the command line parameters for all compress samples
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_compress_params(void);

/*
 * Register the command line parameters for deflate samples
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_deflate_params(void);

/*
 * Register the command line parameters for lz4 stream sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_lz4_stream_params(void);

/*
 * Initiate the fields of the zlib header with default values
 *
 * @zlib_header [in]: A Zlib header to initiate with DOCA Compress default settings
 */
void init_compress_zlib_header(struct compress_zlib_header *zlib_header);

/*
 * Verify the header values are valid and compatible with DOCA compress
 *
 * @zlib_header [in]: A Zlib header to initiate with DOCA Compress default settings
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t verify_compress_zlib_header(struct compress_zlib_header *zlib_header);

/*
 * Parse the LZ4 frame and verify the header values are valid and compatible with DOCA compress
 *
 * @src_buf [in]: The src buffer, beginning with the frame header. Data section will be updated accordingly.
 * @cfg [in/out]: The compress configuration, in order to update relevant flags
 * @has_content_checksum [out]: true if the content includes a checksum (ignored if the pointer is NULL).
 * @content_checksum [out]: A pointer to hold the content checksum, if present (ignored if the pointer is NULL).
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t parse_lz4_frame(struct doca_buf *src_buf,
			     struct compress_cfg *cfg,
			     bool *has_content_checksum,
			     uint32_t *content_checksum);

/*
 * Allocate DOCA compress resources
 *
 * @pci_addr [in]: Device PCI address
 * @max_bufs [in]: Maximum number of buffers for DOCA Inventory
 * @resources [out]: DOCA compress resources to allocate
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allocate_compress_resources(const char *pci_addr, uint32_t max_bufs, struct compress_resources *resources);

/*
 * Destroy DOCA compress resources
 *
 * @resources [in]: DOCA compress resources to destroy
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t destroy_compress_resources(struct compress_resources *resources);

/*
 * Submit compress deflate task and wait for completion
 * Also calculate the checksum where the lower 32 bits contain the CRC checksum result
 * and the upper 32 bits contain the Adler checksum result.
 *
 * @resources [in]: DOCA compress resources
 * @src_buf [in]: Source buffer
 * @dst_buf [in]: Destination buffer
 * @output_checksum [out]: The calculated checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t submit_compress_deflate_task(struct compress_resources *resources,
					  struct doca_buf *src_buf,
					  struct doca_buf *dst_buf,
					  uint64_t *output_checksum);

/*
 * Submit decompress deflate task and wait for completion
 * Also calculate the checksum where the lower 32 bits contain the CRC checksum result
 * and the upper 32 bits contain the Adler checksum result.
 *
 * @resources [in]: DOCA compress resources
 * @src_buf [in]: Source buffer
 * @dst_buf [in]: Destination buffer
 * @output_checksum [out]: The calculated checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t submit_decompress_deflate_task(struct compress_resources *resources,
					    struct doca_buf *src_buf,
					    struct doca_buf *dst_buf,
					    uint64_t *output_checksum);

/*
 * Submit decompress lz4 task and wait for completion
 * Also calculate the checksum where the lower 32 bits contain the CRC checksum result
 * and the upper 32 bits contain the Adler checksum result.
 *
 * @resources [in]: DOCA compress resources
 * @has_block_checksum [in]: has_block_checksum flag
 * @are_blocks_independent [in]: are_blocks_independent flag
 * @src_buf [in]: Source buffer
 * @dst_buf [in]: Destination buffer
 * @output_crc_checksum [out]: The calculated crc checksum
 * @output_xxh_checksum [out]: The calculated xxHash checksum
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t submit_decompress_lz4_stream_task(struct compress_resources *resources,
					       uint8_t has_block_checksum,
					       uint8_t are_blocks_independent,
					       struct doca_buf *src_buf,
					       struct doca_buf *dst_buf,
					       uint32_t *output_crc_checksum,
					       uint32_t *output_xxh_checksum);

/*
 * Check if given device is capable of executing a DOCA compress deflate task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA compress deflate task and DOCA_ERROR otherwise
 */
doca_error_t compress_task_compress_deflate_is_supported(struct doca_devinfo *devinfo);

/*
 * Check if given device is capable of executing a DOCA decompress deflate task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA decompress deflate task and DOCA_ERROR otherwise
 */
doca_error_t compress_task_decompress_deflate_is_supported(struct doca_devinfo *devinfo);

/*
 * Check if given device is capable of executing a DOCA decompress deflate task.
 *
 * @devinfo [in]: The DOCA device information
 * @return: DOCA_SUCCESS if the device supports DOCA decompress deflate task and DOCA_ERROR otherwise
 */
doca_error_t compress_task_decompress_lz4_stream_is_supported(struct doca_devinfo *devinfo);
/*
 * Compress task completed callback
 *
 * @compress_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void compress_completed_callback(struct doca_compress_task_compress_deflate *compress_task,
				 union doca_data task_user_data,
				 union doca_data ctx_user_data);

/*
 * Compress task error callback
 *
 * @compress_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void compress_error_callback(struct doca_compress_task_compress_deflate *compress_task,
			     union doca_data task_user_data,
			     union doca_data ctx_user_data);

/*
 * Decompress deflate task completed callback
 *
 * @decompress_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void decompress_deflate_completed_callback(struct doca_compress_task_decompress_deflate *decompress_task,
					   union doca_data task_user_data,
					   union doca_data ctx_user_data);

/*
 * Decompress deflate task error callback
 *
 * @decompress_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void decompress_deflate_error_callback(struct doca_compress_task_decompress_deflate *decompress_task,
				       union doca_data task_user_data,
				       union doca_data ctx_user_data);

/*
 * Decompress lz4 stream task completed callback
 *
 * @decompress_task [in]: Completed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void decompress_lz4_stream_completed_callback(struct doca_compress_task_decompress_lz4_stream *decompress_task,
					      union doca_data task_user_data,
					      union doca_data ctx_user_data);

/*
 * Decompress lz4 stream task error callback
 *
 * @decompress_task [in]: failed task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
void decompress_lz4_stream_error_callback(struct doca_compress_task_decompress_lz4_stream *decompress_task,
					  union doca_data task_user_data,
					  union doca_data ctx_user_data);

#endif /* COMPRESS_COMMON_H_ */
