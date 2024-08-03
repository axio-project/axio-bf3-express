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

#include <stdlib.h>

#include <doca_error.h>
#include <doca_log.h>

DOCA_LOG_REGISTER(GRAPH::MAIN);

/* Sample's Logic */
doca_error_t run_graph_sample(void);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char *argv[])
{
	(void)(argc);
	(void)(argv);

	doca_error_t result;
	int exit_status = EXIT_FAILURE;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	DOCA_LOG_INFO("Starting the sample");

	/* Run the sample's core function */
	result = run_graph_sample();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("run_graph_sample() encountered an error: %s", doca_get_error_string(result));
		goto sample_exit;
	}

	exit_status = EXIT_SUCCESS;

sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
