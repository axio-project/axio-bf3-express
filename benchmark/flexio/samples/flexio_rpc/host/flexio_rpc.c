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

/* Source file for host part of rpc sample.
 * Contain functions for parsing input parameters, allocate and free resources,
 * initialization of a process and run RPC of DPA application.
 */

/* Used for sleep function */
#include <unistd.h>

/* Used for strtoimax function */
#include <inttypes.h>

/* Flex IO SDK host side version API header. */
#include <libflexio/flexio_ver.h>

/* Set current version of FLEXIO_VER_USED. */
#define FLEXIO_VER_USED FLEXIO_VER(25, 1, 0)

/* Flex IO SDK host side API header. */
#include <libflexio/flexio.h>

/* Flex IO packet processor device (DPA) side function stub.
 * The pointer is named the same as the entry point function
 * in the DPA application.
 */
extern flexio_func_t rpc_calculate;

/* The structure for input parameters of the sample */
struct sample_args {
	char *device;
	uint64_t arg1;
	uint64_t arg2;
};

/* The structure containing context structures of the sample */
struct sample_context {
	/* IBV context opened for the device name provided by the user. */
	struct ibv_context *ibv_ctx;
	/* Flex IO process is used to load a program to the DPA. */
	struct flexio_process *flexio_process;
	/* Flex IO application to load to the process. */
	struct flexio_app *flexio_app;
	/* Flex IO message stream is used to get messages from the DPA. */
	struct flexio_msg_stream *stream;
};

/* The macro for converting a logarithm to a value */
#define L2V(l) (1UL << (l))

/* The function for parsing input parameters of the sample application */
static int parse_rpc_sample_args(int argc, char **argv, struct sample_args *sample_args)
{
	/* The pointer to the second of the arguments of strtoimax function */
	char *endptr;

	/* The number of arguments must be 4 */
	if (argc != 4) {
		printf("Syntax: %s <mlx5 device> <first number> <second number>\n", argv[0]);
		return -1;
	}

	/* The first argument is the mlx5 device name */
	sample_args->device = argv[1];

	/* Convert the first number from the string to a numeric value.
	 * If the string contains non-numeric characters, exit with
	 * an error.
	 */
	sample_args->arg1 = strtoumax(argv[2], &endptr, 10);
	if (*endptr) {
		printf("Invalid first number value - %s", argv[2]);
		return -1;
	}

	/* Convert the second number from the string to a numeric value.
	 * If the string contains non-numeric characters, exit with
	 * an error.
	 */
	sample_args->arg2 = strtoumax(argv[3], &endptr, 10);
	if (*endptr) {
		printf("Invalid second number value - %s", argv[3]);
		return -1;
	}

	return 0;
}

/* dev msg stream buffer built from chunks of 2^FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE each */
#define MSG_HOST_BUFF_BSIZE (4 * L2V(FLEXIO_MSG_DEV_LOG_DATA_CHUNK_BSIZE))

/* Application name in string format for Flex IO app get. */
#define DEV_APP_NAME_STR(_app_name) #_app_name
#define DEV_APP_NAME_XSTR(_app_name) DEV_APP_NAME_STR(_app_name)

/* main function is used for initialize contexts, run RPC, and clean up contexts */
int main(int argc, char **argv)
{
	/* Flex IO app get selection attributes. */
	struct flexio_app_select_attr flexio_app_sel_attr = {0};
	/* Message stream attributes. */
	flexio_msg_stream_attr_t stream_fattr = {0};
	/* Input argument's structure. */
	struct sample_args sample_arguments = {0};
	/* Queried IBV device list. */
	struct ibv_device **dev_list = NULL;
	/* Application context. */
	struct sample_context ctx = {0};
	/* Return value. */
	uint64_t func_ret;
	int i, err = 0;

	/* Parse input arguments */
	if (parse_rpc_sample_args(argc, argv, &sample_arguments)) {
		err = -1;
		goto clean_up;
	}

	printf("Welcome to 'Flex IO RPC' sample\n");

	/* Query IBV devices list. */
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		printf("Failed to get IB devices list (err = %d)\n", errno);
		err = -1;
		goto clean_up;
	}

	/* Loop over found IBV devices. */
	for (i = 0; dev_list[i]; i++) {
		if (!strcmp(ibv_get_device_name(dev_list[i]), sample_arguments.device))
			break;
	}

	/* Check a device was found. */
	if (!dev_list[i]) {
		printf("No IB device named '%s' was not found\n", sample_arguments.device);
		err = -1;
		goto clean_up;
	}

	printf("Registered on device %s\n", sample_arguments.device);

	/* Open the IBV device context for the requested device. */
	ctx.ibv_ctx = ibv_open_device(dev_list[i]);
	if (!ctx.ibv_ctx) {
		printf("Couldn't get context for %s (err = %d)\n", sample_arguments.device, errno);
		err = -1;
		goto clean_up;
	}

	/* Set current version for API */
	if (flexio_version_set(FLEXIO_VER_USED)) {
		printf("Failed to set version in FlexIO API.\n");
		err = -1;
		goto clean_up;
	}

	/* Get Flex IO application struct for used device. */
	/* Set app name to match. */
	flexio_app_sel_attr.app_name = DEV_APP_NAME_XSTR(DEV_APP_NAME);
	/* Set HW platform to default - this will auto-select the appropriate program. */
	flexio_app_sel_attr.hw_model_id = FLEXIO_HW_MODEL_DEF;
	/* Set IBV device to use for HW model query. */
	flexio_app_sel_attr.ibv_ctx = ctx.ibv_ctx;

	/* Get a Flex IO application.
	 * DPACC created Flex IO application per HW model. Match the select attributes to the
	 * found applications and return the matching one. Name must match. HW model is matched
	 * According to the queried HW model for the device. If no exact match is found a program
	 * built to an older HW model will be selected.
	 */
	err = flexio_app_get(&flexio_app_sel_attr, &ctx.flexio_app);
	if (err) {
		printf("Failed to get Flex IO app\n");
		goto clean_up;
	}

	/* Create a Flex IO process.
	 * The flexio_app struct is passed to load the program.
	 * No process creation attributes are needed for this application (default outbox).
	 * Created SW struct will be returned through the given pointer.
	 */
	err = flexio_process_create(ctx.ibv_ctx, ctx.flexio_app, NULL, &ctx.flexio_process);
	if (err) {
		printf("Failed to create Flex IO process\n");
		goto clean_up;
	}

	/* Create a Flex IO message stream for process.
	 * The size of single message stream is MSG_HOST_BUFF_BSIZE.
	 * The working mode is synchronous.
	 * The level of debug is INFO.
	 * Transport mode - QP RC (possible alternatives - QP UC or QP UD)
	 * The output is stdout.
	 */
	stream_fattr.data_bsize = MSG_HOST_BUFF_BSIZE;
	stream_fattr.sync_mode = FLEXIO_MSG_DEV_SYNC_MODE_SYNC;
	stream_fattr.level = FLEXIO_MSG_DEV_INFO;
	stream_fattr.transport_mode = FLEXIO_MSG_TRANSPORT_QP_RC;

	err = flexio_msg_stream_create(ctx.flexio_process, &stream_fattr, stdout, NULL,
				       &ctx.stream);
	if (err) {
		printf("Failed to init device messaging environment\n");
		goto clean_up;
	}

	/* Call a DPA function rpc_calculate from the Flex IO process with arguments
	 * sample_arguments.arg1 and sample_arguments.arg2, and return the value to
	 * the func_ret variable.
	 */
	err = flexio_process_call(ctx.flexio_process, &rpc_calculate, &func_ret,
				  sample_arguments.arg1, sample_arguments.arg2);
	if (err) {
		printf("Remote process call failed\n");
		goto clean_up;
	}

	/* Switch to the thread that receives device messages and wait until all are finished */
	sleep(1);

	/* Print return value , which is the sum of sample_arguments.arg1 and
	 *  sample_arguments.arg2.
	 */
	printf("Result: %lu\n", func_ret);
	printf("Flex IO RPC sample is done\n");

clean_up:
	/* Destroy the message stream if it was created */
	if (ctx.flexio_process && flexio_msg_stream_destroy(ctx.stream)) {
		printf("Failed to destroy device messaging environment\n");
		err = -1;
	}

	/* Destroy the Flex IO process */
	if (flexio_process_destroy(ctx.flexio_process)) {
		printf("Failed to destroy process\n");
		err = -1;
	}

	/* Close the IBV device if it was opened */
	if (ctx.ibv_ctx && ibv_close_device(ctx.ibv_ctx)) {
		printf("Failed to destroy process\n");
		err = -1;
	}

	/* Free the queried IBV devices list. */
	if (dev_list)
		ibv_free_device_list(dev_list);

	return err;
}
