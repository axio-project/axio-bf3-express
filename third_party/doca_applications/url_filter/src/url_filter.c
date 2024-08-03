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
#include <errno.h>
#include <sys/wait.h>

#include <cmdline_socket.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline.h>
#include <rte_compat.h>
#include <rte_malloc.h>
#include <rte_hash.h>
#include <rte_jhash.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <utils.h>

#include "url_filter_core.h"

DOCA_LOG_REGISTER(URL_FILTER);

/* Create database command result */
struct cmd_create_result {
	cmdline_fixed_string_t create_db;	/* Command first segment */
};

/*
 * Parse create database command
 */
static void
cmd_create_parsed(__rte_unused void *parsed_result, __rte_unused struct cmdline *cl,
	__rte_unused void *data)
{
	create_database(DEFAULT_TXT_INPUT);
}

/* Define the token of create database */
cmdline_parse_token_string_t cmd_create_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_create_result, create_db, "create database");

/* Define create database command structure for parsing */
cmdline_parse_inst_t cmd_create = {
	.f = cmd_create_parsed,					/* function to call */
	.data = NULL,						/* 2nd arg of func */
	.help_str = "Delete and create a new database",		/* Command print usage */
	.tokens = {						/* token list, NULL terminated */
		(void *)&cmd_create_tok,
		NULL,
	},
};

/* Update database command result */
struct cmd_update_result {
	cmdline_fixed_string_t commit_db;	/* Command first segment */
	cmdline_fixed_string_t file_path;	/* Command last segment */
};

/*
 * Parse update database command
 *
 * @parsed_result [in]: Command line interface input with user input
 */
static void
cmd_update_parsed(void *parsed_result, __rte_unused struct cmdline *cl,
	__rte_unused void *data)
{
	struct cmd_update_result *path_data = (struct cmd_update_result *)parsed_result;

	compile_and_load_signatures(path_data->file_path, DEFAULT_CDO_OUTPUT);
}

/* Define the token of commit database */
cmdline_parse_token_string_t cmd_commit_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_update_result, commit_db, "commit database");

/* Define the token of create file path */
cmdline_parse_token_string_t cmd_path_tok  =
	TOKEN_STRING_INITIALIZER(struct cmd_update_result, file_path, NULL);

/* Define update database command structure for parsing */
cmdline_parse_inst_t cmd_update = {
	.f = cmd_update_parsed,									/* function to call */
	.data = NULL,										/* 2nd arg of func */
	.help_str = "Update the DPI database in filepath - default is /tmp/signature.cdo",	/* Command print usage */
	.tokens = {										/* token list, NULL terminated */
		(void *)&cmd_commit_tok,
		(void *)&cmd_path_tok,
		NULL,
	},
};

/* URL filter command result */
struct cmd_filter_result {
	cmdline_fixed_string_t filter;		/* Command first segment */
	cmdline_fixed_string_t proto;		/* Command second segment */
	cmdline_fixed_string_t msg;		/* Command third segment */
	cmdline_fixed_string_t pcre;		/* Command last segment */
};

/*
 * Parse update database command
 *
 * @parsed_result [in]: Command line interface input with user input
 */
static void
cmd_filter_parsed(void *parsed_result, __rte_unused struct cmdline *cl, __rte_unused void *data)
{
	struct cmd_filter_result *filter_data = (struct cmd_filter_result *)parsed_result;

	create_url_signature(DEFAULT_TXT_INPUT, filter_data->msg, filter_data->pcre);
}

/* Define the token of filter */
cmdline_parse_token_string_t cmd_filter_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_filter_result, filter, "filter");

/* Define the token of http */
cmdline_parse_token_string_t cmd_http_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_filter_result, proto, "http");

/* Define the token of message */
cmdline_parse_token_string_t cmd_msg_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_filter_result, msg, NULL);

/* Define the token of pcre */
cmdline_parse_token_string_t cmd_pcre_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_filter_result, pcre, NULL);

/* Define URL filter command structure for parsing */
cmdline_parse_inst_t cmd_filter = {
	.f = cmd_filter_parsed,									/* function to call */
	.data = NULL,										/* 2nd arg of func */
	.help_str = "Filter URL - 3rd argument stand for the printed name and 4th for PCRE",	/* Command print usage */
	.tokens = {										/* token list, NULL terminated */
		(void *)&cmd_filter_tok,
		(void *)&cmd_http_tok,
		(void *)&cmd_msg_tok,
		(void *)&cmd_pcre_tok,
		NULL,
	},
};

/* Quit command result */
struct cmd_quit_result {
	cmdline_fixed_string_t quit;		/* Command first segment */
};

/*
 * Quit command line interface
 *
 * @cl [in]: Command line
 */
static void
cmd_quit_parsed(__rte_unused void *parsed_result, struct cmdline *cl, __rte_unused void *data)
{
	cmdline_quit(cl);
}

/* Define the token of quit */
cmdline_parse_token_string_t cmd_quit_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_quit_result, quit, "quit");

/* Define quit command structure for parsing */
cmdline_parse_inst_t cmd_quit = {
	.f = cmd_quit_parsed,				/* function to call */
	.data = NULL,					/* 2nd arg of func */
	.help_str = "Exit application",			/* Command print usage */
	.tokens = {					/* token list, NULL terminated */
		(void *)&cmd_quit_tok,
		NULL,
	},
};

/* Command line interface context */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_quit,
	(cmdline_parse_inst_t *)&cmd_filter,
	(cmdline_parse_inst_t *)&cmd_update,
	(cmdline_parse_inst_t *)&cmd_create,
	NULL,
};


/*
 * Command line parsing initialization
 *
 * @cl_shell_output [in]: command line
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
initiate_cmdline(char *cl_shell_output)
{
	struct cmdline *cl;

	cl = cmdline_stdin_new(main_ctx, cl_shell_output);
	if (cl == NULL)
		return DOCA_ERROR_DRIVER;
	cmdline_interact(cl);
	cmdline_stdin_exit(cl);
	return DOCA_SUCCESS;
}

/*
 * URL Filter application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char *argv[])
{
	doca_error_t result;
	int exit_status = EXIT_SUCCESS;
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
	struct url_filter_config url_filter_config = {.dpdk_config = &dpdk_config};

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	/* Parse cmdline/json arguments */
	result = doca_argp_init("doca_url_filter", &url_filter_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	result = register_url_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register application params: %s", doca_get_error_string(result));
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

	/* All needed preparations - Check for required files, init the DPI, etc */
	result = url_filter_init(&dpdk_config, &url_filter_config, &dpi_worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init url filter application: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	/* Start the DPI processing */
	result = dpi_worker_lcores_run(dpdk_config.port_config.nb_queues, dpi_worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start DPI workers: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto url_filter_cleanup;
	}

	/* Initiate the interactive command line session */
	result = initiate_cmdline("URL FILTER>> ");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initiate cmdline: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto url_filter_cleanup;
	}

url_filter_cleanup:
	/* End of application flow */
	url_filter_destroy();

dpdk_cleanup:
	/* DPDK cleanup */
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_destroy:
	dpdk_fini();

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
