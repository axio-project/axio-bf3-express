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

#include <signal.h>
#include <errno.h>

#include "allreduce_daemon.h"
#include "allreduce_reducer.h"
#include "allreduce_mem_pool.h"

static int running = 1;	/* Indicates if the process still running or not, used by daemons */

DOCA_LOG_REGISTER(ALLREDUCE::Daemon);

/*
 * Daemon callback that is called after completing a receive of a vector with the data from a client
 *
 * @arg [in]: Pointer to the client-request that was completed
 * @status [in]: UCP receive operation completion status
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
daemon_am_recv_data_complete_callback(void *arg, ucs_status_t status)
{
	struct allreduce_request *allreduce_request = arg;
	struct allreduce_super_request *allreduce_super_request;

	assert(status == UCS_OK);

	/* Received size of the vector must be <= the configured size by a user */
	assert(allreduce_request->vector_size <= allreduce_config.vector_size);

	/* Try to find or allocate allreduce super request to match the allreduce request which is currently received */
	allreduce_super_request =
		allreduce_super_request_get(&allreduce_request->header, allreduce_request->vector_size, NULL);
	if (allreduce_super_request == NULL) {
		allreduce_request_destroy(allreduce_request);
		DOCA_LOG_ERR("Abort - No memory to continue");
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Attach the received allreduce request to the allreduce super request for further processing */
	allreduce_request->allreduce_super_request = allreduce_super_request;
	++allreduce_super_request->num_allreduce_requests;
	STAILQ_INSERT_TAIL(&allreduce_super_request->allreduce_requests_list, allreduce_request, entry);

	/* CPU version -
	 * Do operation using the received vector and save the result to the vector from the allreduce super request
	 * GPU version -
	 * Save the vector into an array and reduce all later
	 */
	if (allreduce_super_request->result_vector != NULL) {
#ifdef GPU_SUPPORT
		/*
		 * If we got here there are at least 2 clients and at least 2 requests were counted,
		 * meaning: "num_allreduce_requests >= 2 && clients_recv_vectors != NULL" is true.
		 *
		 * -2 Since "num_allreduce_requests" counts received requests and also we don't keep the first vector
		 */
		allreduce_super_request->clients_recv_vectors[allreduce_super_request->num_allreduce_requests - 2] =
			allreduce_request->vector;
#else
		allreduce_reduce(allreduce_super_request, allreduce_request->vector, false);
		allreduce_free_vec_vecs_pool(allreduce_request->vector);
#endif
	} else
		allreduce_super_request->result_vector = allreduce_request->vector;
	allreduce_request->vector = NULL;

	/* The whole result will be sent to the other daemons when all vectors are received from clients */
	if (allreduce_super_request->num_allreduce_requests == allreduce_config.num_clients) {
		/*
		 * Daemons received the allreduce vectors from all clients - perform allreduce among other daemons
		 * (if any)
		 */
#ifdef GPU_SUPPORT
		if (ucs_likely(allreduce_config.num_clients > 1)) {
			allreduce_reduce_all(allreduce_super_request, false);

			/* Return vectors to pool. "num_requests-1" Since one vector was taken to be the result vector */
			allreduce_free_vecs_vecs_pool(allreduce_super_request->clients_recv_vectors,
						allreduce_super_request->num_allreduce_requests - 1);
			allreduce_free_vec_clients_bufs_pool(allreduce_super_request->clients_recv_vectors);
			allreduce_super_request->clients_recv_vectors = NULL;

			/* No need to sync before send, UCX uses CUDA default stream which perform implicit sync */
		}
#endif
		allreduce_scatter(allreduce_super_request);
	} else if (allreduce_super_request->num_allreduce_requests > allreduce_config.num_clients) {
		DOCA_LOG_WARN("More vectors than clients were received for a single Allreduce operation. Ignoring vector of new client (considered duplicates) but including in the final result response");
	} else {
		/* Not all clients sent their vectors to the daemon */
	}
	return DOCA_SUCCESS;
}

/*
 * Active Message receive callback which is invoked when the daemon gets an incoming message from a client
 *
 * @am_desc [in]: Pointer to a descriptor of the incoming vector
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
daemon_am_recv_ctrl_callback(struct allreduce_ucx_am_desc *am_desc)
{
	struct allreduce_ucx_connection *connection;
	const struct allreduce_header *allreduce_header;
	struct allreduce_request *allreduce_request;
	size_t header_length, length;

	allreduce_ucx_am_desc_query(am_desc, &connection, (const void **)&allreduce_header, &header_length, &length);

	assert(sizeof(*allreduce_header) == header_length);

	/* Allreduce request will be freed in "daemon_allreduce_complete_client_operation_callback" function upon
	 * completion of sending results to all clients
	 */
	allreduce_request = allreduce_request_allocate(connection, allreduce_header, length);
	if (allreduce_request == NULL)
		return DOCA_ERROR_NO_MEMORY;

	/* Continue receiving data to the allocated vector */
	DOCA_DLOG_DBG("Received a vector from a client");
	return allreduce_ucx_am_recv(am_desc, allreduce_request->vector, length, daemon_am_recv_data_complete_callback,
			      allreduce_request, NULL);
}

/*
 * Tells daemon to begin cleanup and exit
 *
 * @signo [in]: Ignored. signal number.
 */
static void
signal_terminate_handler(int signo)
{
	(void)signo;

	running = 0;
}
/*
 * Sets "signal_terminate_handler" as a signal handler of incoming SIGINT
 *
 */
static void
signal_terminate_set(void)
{
	struct sigaction new_sigaction = {
		.sa_handler = signal_terminate_handler,
		.sa_flags = 0
	};

	sigemptyset(&new_sigaction.sa_mask);

	if (sigaction(SIGINT, &new_sigaction, NULL) != 0) {
		DOCA_LOG_ERR("Failed to set SIGINT signal handler: %s", strerror(errno));
		abort();
	}
}

void
daemon_run(void)
{
	if (allreduce_config.num_clients == 0) {
		/* Nothing to do */
		DOCA_LOG_ERR("Stop running - daemon doesn't have clients");
		return;
	}

	signal_terminate_set();

	/*
	 * Setup receive handler for Active message control messages from client which carries a vector to do
	 * allreduce for
	 */
	allreduce_ucx_am_set_recv_handler(context, ALLREDUCE_CTRL_AM_ID, daemon_am_recv_ctrl_callback);

	DOCA_LOG_INFO("Daemon is active and waiting for client connections... Press Ctrl+C to terminate");

	while (running) {
		/* Progress UCX to handle client's allreduce requests until a signal is received */
		if (allreduce_ucx_progress(context) != DOCA_SUCCESS)
			break;
	}
}
