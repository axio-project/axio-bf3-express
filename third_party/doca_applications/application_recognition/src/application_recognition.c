/*
 * Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include <rte_sft.h>
#include <rte_malloc.h>
#include <rte_ring.h>
#include <cmdline_socket.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <dpi_worker.h>
#include <sig_db.h>
#include <utils.h>

#include "application_recognition_core.h"

DOCA_LOG_REGISTER(AR);

/* Quit command result */
struct cmd_quit_result {
	cmdline_fixed_string_t quit;	/* Command first segment */
};

/*
 * Function for parsing the quit command
 *
 * @cl [in]: command line
 */
static void
cmd_quit_parsed(__rte_unused void *parsed_result, struct cmdline *cl, __rte_unused void *data)
{
	cmdline_quit(cl);
	force_quit = true;
}

/* Define the token of quit */
cmdline_parse_token_string_t cmd_quit_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_quit_result, quit, "quit");

/* Define quit command structure for parsing */
cmdline_parse_inst_t cmd_quit = {
	.f = cmd_quit_parsed,			/* Function to call */
	.data = NULL,				/* 2nd arg of func */
	.help_str = "Exit application",		/* Command print usage */
	.tokens = {				/* Token list, NULL terminated */
		(void *)&cmd_quit_tok,
		NULL,
	},
};

/* Define result structure of block command */
struct cmd_block_result {
	cmdline_fixed_string_t block;	/* Command first segment */
	uint32_t sig_id;		/* Command last segment */
};

/*
 * Function for parsing the block command
 *
 * @parsed_result [in]: parsing result
 * @cl [in]: command line
 */
static void
cmd_block_parsed(void *parsed_result, struct cmdline *cl, __rte_unused void *data)
{
	struct cmd_block_result *block_data = (struct cmd_block_result *)parsed_result;

	cmdline_printf(cl, "Blocking sig_id=%d!\n", block_data->sig_id);
	sig_db_sig_info_set_block_status(block_data->sig_id, true);
}

/* Define the token of block */
cmdline_parse_token_string_t cmd_block_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_block_result, block, "block");

/* Define the token of flow ID */
cmdline_parse_token_num_t cmd_fid_tok =
	TOKEN_NUM_INITIALIZER(struct cmd_block_result, sig_id, RTE_UINT32);

/* Define block command structure for parsing */
cmdline_parse_inst_t cmd_block = {
	.f = cmd_block_parsed,			/* function to call */
	.data = NULL,				/* 2nd arg of func */
	.help_str = "Block signature ID",	/* Command print usage */
	.tokens = {				/* token list, NULL terminated */
		(void *)&cmd_block_tok,
		(void *)&cmd_fid_tok,
		NULL,
	},
};

/*
 * Function for parsing the unblock command
 *
 * @parsed_result [in]: Command line interface input with user input
 * @cl [in]: command line
 */
static void
cmd_unblock_parsed(void *parsed_result, struct cmdline *cl, __rte_unused void *data)
{
	struct cmd_block_result *block_data = (struct cmd_block_result *)parsed_result;

	cmdline_printf(cl, "Unblocking sig_id=%d!\n", block_data->sig_id);
	sig_db_sig_info_set_block_status(block_data->sig_id, false);
}

/* Define the token of unblock */
cmdline_parse_token_string_t cmd_unblock_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_block_result, block, "unblock");

/* Define unblock command structure for parsing */
cmdline_parse_inst_t cmd_unblock = {
	.f = cmd_unblock_parsed,		/* Function to call */
	.data = NULL,				/* 2nd arg of func */
	.help_str = "Unblock signature ID",	/* Command print usage */
	.tokens = {				/* token list, NULL terminated */
		(void *)&cmd_unblock_tok,
		(void *)&cmd_fid_tok,
		NULL,
	},
};

/* Command line interface context */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_quit,
	(cmdline_parse_inst_t *)&cmd_block,
	(cmdline_parse_inst_t *)&cmd_unblock,
	NULL,
};

/*
 * Command line parsing initialization
 *
 * @cl_shell_prompt [in]: command line
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
initiate_cmdline(char *cl_shell_prompt)
{
	struct cmdline *cl;

	cl = cmdline_stdin_new(main_ctx, cl_shell_prompt);
	if (cl == NULL)
		return DOCA_ERROR_DRIVER;
	cmdline_interact(cl);
	cmdline_stdin_exit(cl);
	return DOCA_SUCCESS;
}

/*
 * Signals handler function to handle SIGINT and SIGTERM signals
 *
 * @signum [in]: signal number
 */
static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		force_quit = true;
	}
}

/*
 * AR application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char *argv[])
{
	int ret;
	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
	pthread_t cmdline_thread;
	struct dpi_worker_attr dpi_worker = {0};
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = 2,
		.port_config.nb_queues = 2,
		.port_config.nb_hairpin_q = 4,
		.port_config.enable_mbuf_metadata = 1,
		.sft_config = {
			.enable = 1, /* Enable SFT */
			.enable_ct = 1,
			.enable_state_hairpin = 1,
			.enable_state_drop = 1,
		},
		.reserve_main_thread = true,
	};
	struct ar_config ar_config = {.dpdk_config = &dpdk_config};

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_application_recognition", &ar_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = register_ar_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	/* update queues and ports */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update application ports and queues: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_destroy;
	}

	/* AR application init */
	ret = ar_init(&ar_config, &dpi_worker);
	if (ret < 0) {
		DOCA_LOG_ERR("Failed to init application recognition");
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	/* Start the DPI processing */
	result = dpi_worker_lcores_run(dpdk_config.port_config.nb_queues, dpi_worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DPI workers: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto ar_cleanup;
	}

	if (ar_config.interactive_mode) {
		ret = rte_ctrl_thread_create(&cmdline_thread, "cmdline_thread", NULL,
			(void *)initiate_cmdline, "APPLICATION RECOGNITION>> ");
		if (ret != 0) {
			DOCA_LOG_ERR("Thread creation failed");
			exit_status = EXIT_FAILURE;
			goto ar_cleanup;
		}
	} else {
		DOCA_LOG_INFO("Non-interactive mode - Ctrl+C to quit");
		signal(SIGINT, signal_handler);
		signal(SIGTERM, signal_handler);
	}
	/* The main thread loop to collect statistics */
	while (!force_quit) {
		if (ar_config.create_csv) {
			sleep(1);
			result = sig_database_write_to_csv(ar_config.csv_filename);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("CSV file access failed");
				exit_status = EXIT_FAILURE;
				goto interactive_cleanup;
			}
		}
		if (ar_config.netflow_source_id && send_netflow_record() != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unexpected Netflow failure");
			exit_status = EXIT_FAILURE;
			goto interactive_cleanup;
		}
	}

interactive_cleanup:
	/* Clearing threads */
	if (ar_config.interactive_mode)
		pthread_kill(cmdline_thread, 0);

ar_cleanup:
	/* AR application cleanup */
	ar_destroy(&ar_config);

dpdk_cleanup:
	/* DPDK cleanup */
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_destroy:
	dpdk_fini();

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
