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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <glib.h>
#ifdef GPU_SUPPORT
#include <cuda_runtime_api.h>
#include <cuda.h>
#include <driver_types.h>
#endif

#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <string.h>
#include <assert.h>

#include <doca_log.h>

#include "allreduce_ucx.h"

/*
 * Gets an invocation of a callback and updates callback_errno as needed
 */
#define INVOKE_CALLBACK(call)                   \
{						\
	doca_error_t result = call;		\
	if (callback_errno == DOCA_SUCCESS)	\
		callback_errno = result;	\
}

DOCA_LOG_REGISTER(ALLREDUCE::UCX);

/* @NOTE: AM = Active message */

struct allreduce_ucx_am_callback_info {
	struct allreduce_ucx_context *context;	/* Pointer to UCX context */
	allreduce_ucx_am_callback callback;	/* Callback which should be invoked upon receiving AM */
};

struct allreduce_ucx_context {
	ucp_context_h context;			/* Holds a UCP communication instance's global information */
	ucp_worker_h worker;			/* Holds local communication resource and the progress engine
						 * associated with it
						 */
	ucp_listener_h listener;		/* Handle for listening on a specific address and accepting
						 * incoming connections
						 */
	GHashTable *ep_to_connections_hash;	/* Hash Table to map active EP to its active connection */
	unsigned int max_am_id;			/* Maximum Active Message (AM) identifier utilized by the user */
	struct allreduce_ucx_am_callback_info *am_callback_infos;	/* AM callback which was specified by a user */
};

struct allreduce_ucx_connection {
	struct allreduce_ucx_context *context;          /* Pointer to the context which owns this connection */
	ucp_ep_h ep;                                    /* Endpoint that is connected to a remote worker */
	struct sockaddr_storage *destination_address;   /* Address of the peer */
};

enum allreduce_ucx_op {
	ALLREDUCE_UCX_UNKNOWN_OP,	/* Unknown UCX operation */
	ALLREDUCE_UCX_AM_SEND,		/* Active Message (AM) send operation */
	ALLREDUCE_UCX_AM_RECV_DATA	/* Active Message (AM) receive data operation */
};

struct allreduce_ucx_request {
	allreduce_ucx_callback callback;		/* Completion callback which was specified by a user */
	void *arg;					/* Argument which should be passed to the completion callback
							 */
	struct allreduce_ucx_connection *connection;	/* Owner of UCX request */
	int log_err;					/* Indicates whether error message should be printed in case
							 * error detected
							 */
	ucs_status_t status;				/* Current status of the operation */
	enum allreduce_ucx_op op;			/* Operation type */
};

struct allreduce_ucx_am_desc {
	struct allreduce_ucx_connection *connection;	/* Pointer to the connection on which this AM operation
							 * was received
							 */
	const void *header;				/* Header got from AM callback */
	size_t header_length;				/* Length of the header got from AM callback */
	void *data_desc;				/* Pointer to the descriptor got from AM callback. In case of
							 * Rendezvous, it is not the actual data, but only a data
							 * descriptor
							 */
	size_t length;					/* Length of the received data */
	uint64_t flags;					/* AM operation flags */
};

static const char * const allreduce_ucx_op_str[] = {
	[ALLREDUCE_UCX_AM_SEND] = "ucp_am_send_nbx",		/* Name of Active Message (AM) send operation */
	[ALLREDUCE_UCX_AM_RECV_DATA] = "ucp_am_recv_data_nbx"	/* Name of Active Message (AM) receive data operation */
};

static GHashTable *active_connections_hash;
static doca_error_t callback_errno = DOCA_SUCCESS;

/***** Requests Processing *****/

/*
 * Initialize a new "allreduce_ucx" request handle
 *
 * @request [in]: Pointer to a handle
 */
static void
request_init(void *request)
{
	struct allreduce_ucx_request *r = (struct allreduce_ucx_request *)request;

	/* Initialize all fields of UCX request by default values */
	r->connection = NULL;
	r->callback = NULL;
	r->arg = NULL;
	r->op = ALLREDUCE_UCX_UNKNOWN_OP;
	r->status = UCS_INPROGRESS;
	r->log_err = 1;
}

/*
 * Releases a "allreduce_ucx" request handle
 *
 * @request [in]: Pointer to a handle
 */
static void
request_release(void *request)
{
	/* Reset UCP request to the initial state */
	request_init(request);
	/* Free UCP request */
	ucp_request_free(request);
}

/*
 * Called after a UCX operation or request was completed, invokes the callback with the given arg
 * and sets request to NULL
 *
 * @callback [in]: Callback to be called, use NULL to ignore
 * @arg [in]: Additional argument to the callback
 * @request_p [in]: Request that was completed, use NULL to ignore
 * @status [in]: The status in which the UCX operation has ended
 */
static inline void
user_request_complete(allreduce_ucx_callback callback, void *arg, struct allreduce_ucx_request **request_p,
		      ucs_status_t status)
{
	if (callback != NULL) {
		/* Callback was specified by a user, invoke it */
		INVOKE_CALLBACK(callback(arg, status));
	}

	if (request_p != NULL) {
		/* Storage for request was specified by a user, set it to NULL, because operation was already completed */
		*request_p = NULL;
	}
}

/*
 * Checks a UCX request handle for status and initialize it if it's in a pending state.
 * If the operation was performed immediately it will invoke the callback and complete the request handle operation.
 * If the operation is pending it will initialize the request handle to perform the callback once the request
 * is operation is done in "common_request_callback".
 *
 * @connection [in]: The connection related to the request handle
 * @op [in]: Enum describing the operation that generates the request
 * @ptr_status [in]: Pointer to request handle or UCX status of completion
 * @callback [in]: Callback to be called once the operation completes
 * @arg [in]: Argument to pass the callback
 * @log_err [in]: 0 or 1 - To log errors or not
 * @request_p [out]: The request handle that was initialized, if NULL is returned then the operation was completed
 *		     use NULL to ignore this request handle
 * @return: DOCA_SUCCESS on success/in-progress "ptr_status" values, and DOCA_ERROR otherwise ("ptr_status" of an error)
 */
static doca_error_t
request_process(struct allreduce_ucx_connection *connection, enum allreduce_ucx_op op, ucs_status_ptr_t ptr_status,
		allreduce_ucx_callback callback, void *arg, int log_err, struct allreduce_ucx_request **request_p)
{
	const char *what = allreduce_ucx_op_str[op];
	struct allreduce_ucx_request *r = NULL;
	ucs_status_t status;

	if (ptr_status == NULL) {
		/* Operation was completed successfully */
		user_request_complete(callback, arg, request_p, UCS_OK);
		if (request_p != NULL)
			*request_p = NULL;
		return DOCA_SUCCESS;
	} else if (UCS_PTR_IS_ERR(ptr_status)) {
		/* Operation was completed with the error */
		status = UCS_PTR_STATUS(ptr_status);
		if (log_err) {
			/* Requested to print an error */
			DOCA_LOG_ERR("The operation %s failed with status: %s", what, ucs_status_string(status));
		}
		/* Complete operation and provide the error status */
		user_request_complete(callback, arg, request_p, status);
		return DOCA_ERROR_UNEXPECTED;
	}

	/* Got pointer to request */
	r = (struct allreduce_ucx_request *)ptr_status;
	if (r->status != UCS_INPROGRESS) {
		/* Already completed by "common_request_callback" */

		assert(r->op == op);

		/* Complete operation and provide the status */
		status = r->status;
		user_request_complete(callback, arg, request_p, status);
		/* Release the request */
		request_release(r);

		if (status != UCS_OK) {
			DOCA_LOG_ERR("The operation %s failed with status: %s", what, ucs_status_string(status));
			return DOCA_ERROR_UNEXPECTED;
		}
	} else {
		/* Will be completed by "common_request_callback", initialize the request */

		assert(r->op == ALLREDUCE_UCX_UNKNOWN_OP);

		r->callback = callback;
		r->connection = connection;
		r->arg = arg;
		r->op = op;
		r->log_err = log_err;

		if (request_p != NULL) {
			/* If it was requested by a user, provide the request to wait on */
			*request_p = r;
		}
	}

	return DOCA_SUCCESS;
}

/*
 * Callback that is called once an UCX send/receive operation was completed.
 * If the request was already processed by "request_process" It will invoke the request handle callback and
 * release the request afterward. Otherwise, it will initialize the request with the "user_data" to be
 * processed by "request_process".
 *
 * @request [in]: The request handle of the completed operation
 * @status [in]: The status the operation has ended with
 * @user_data [in]: The connection the operation was performed on
 * @op [in]: The type of operation that was completed
 */
static inline void
common_request_callback(void *request, ucs_status_t status, void *user_data, enum allreduce_ucx_op op)
{
	struct allreduce_ucx_request *r = (struct allreduce_ucx_request *)request;

	/* Save completion status */
	r->status = status;

	if (r->connection != NULL) {
		/* Already processed by "request_process" */
		if (r->callback != NULL) {
			/* Callback was specified by a user, invoke it */
			INVOKE_CALLBACK(r->callback(r->arg, status));
			/* Release the request */
			request_release(request);
		} else {
			/* User is responsible to check if the request completed or not and release the request then */
		}
	} else {
		assert(r->op == ALLREDUCE_UCX_UNKNOWN_OP);

		/* Not processed by "request_process" */
		r->connection = user_data;
		r->op = op;
	}
}

int
allreduce_ucx_request_wait(int ret, struct allreduce_ucx_request *request)
{
	if (ret < 0 && request != NULL) {
		/* Operation was completed with error */
		DOCA_LOG_ERR("The operation %p failed: %s", allreduce_ucx_op_str[request->op],
			     ucs_status_string(request->status));
	} else if (request != NULL) {
		while (request->status == UCS_INPROGRESS) {
			/* Progress UCX context until completion status is in-progress */
			allreduce_ucx_progress(request->connection->context);
		}

		if (request->status != UCS_OK) {
			/* Operation failed */
			if (request->log_err) {
				/* Print error if requested by a caller */
				DOCA_LOG_ERR("The operation %s failed: %s", allreduce_ucx_op_str[request->op],
						ucs_status_string(request->status));
			}
			ret = -1;
		}

		/* Release the request */
		allreduce_ucx_request_release(request);
	}

	return ret;
}

void
allreduce_ucx_request_release(struct allreduce_ucx_request *request)
{
	request_release(request);
}

/***** Active Message send operation *****/

/*
 * Active Message (AM) send callback that is called after every send is completed, it will process the
 * request handle and the return status of the send operation
 *
 * @request [in]: The request handle of the send operation
 * @status [in]: The UCX status of the send operation
 * @user_data [in]: The connection the send was performed from
 */
static void
am_send_request_callback(void *request, ucs_status_t status, void *user_data)
{
	common_request_callback(request, status, user_data, ALLREDUCE_UCX_AM_SEND);
}

/*
 * Sends a message with a specific Active Message ID to the connection
 *
 * @connection [in]: Connection for sending the message
 * @am_id [in]: Active Message ID to associate the message with
 * @log_err [in]: 0 or 1 - To log errors or not
 * @header [in]: Message header
 * @header_length [in]: Size of header in bytes
 * @buffer [in]: Buffer to send, can be GPU memory
 * @length [in]: Size of the buffer in bytes
 * @callback [in]: Callback to invoke once the send operation has completed
 * @arg [in]: Additional argument to pass the callback
 * @request_p [out]: Pointer to store UCX request handle, useful to track progress of the operation. Use NULL to ignore
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
am_send(struct allreduce_ucx_connection *connection, unsigned int am_id, int log_err, const void *header,
	size_t header_length, const void *buffer, size_t length, allreduce_ucx_callback callback, void *arg,
	struct allreduce_ucx_request **request_p)
{
	ucp_request_param_t param = {
		/* Completion callback, user data and flags are specified */
		.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FIELD_FLAGS,
		/* Send completion callback */
		.cb.send = am_send_request_callback,
		/* User data is the pointer of the connection on which the operation will posted */
		.user_data = connection,
		/* Force passing UCP EP of the connection to the AM receive handler on a receiver side */
		.flags = UCP_AM_SEND_FLAG_REPLY
	};
	ucs_status_ptr_t status_ptr;

	/* Submit AM send operation */
	status_ptr = ucp_am_send_nbx(connection->ep, am_id, header, header_length, buffer, length, &param);
	/* Process 'status_ptr' */
	return request_process(connection, ALLREDUCE_UCX_AM_SEND, status_ptr, callback, arg, log_err, request_p);
}

doca_error_t
allreduce_ucx_am_send(struct allreduce_ucx_connection *connection, unsigned int am_id, const void *header,
			size_t header_length, const void *buffer, size_t length, allreduce_ucx_callback callback,
			void *arg, struct allreduce_ucx_request **request_p)
{
	if (g_hash_table_lookup(active_connections_hash, connection) == NULL) {
		DOCA_LOG_WARN("Send to a disconnected endpoint was requested");
		if (request_p != NULL)
			*request_p = NULL;
		INVOKE_CALLBACK(callback(arg, UCS_ERR_UNREACHABLE));
		return DOCA_ERROR_NOT_CONNECTED; /* already been disconnected */
	}
	return am_send(connection, am_id, 1, header, header_length, buffer, length, callback, arg, request_p);
}

/***** Active Message receive operation *****/

/*
 * Active Message (AM) receive callback that is called after every receive is completed, it will process the
 * request handle and the return status of the receive operation
 *
 * @request [in]: The request handle of the receive operation
 * @status [in]: The UCX status of the receive operation
 * @length [in]: Unused
 * @user_data [in]: The connection the receive was performed on
 */
static void
am_recv_data_request_callback(void *request, ucs_status_t status, size_t length, void *user_data)
{
	(void)length;

	common_request_callback(request, status, user_data, ALLREDUCE_UCX_AM_RECV_DATA);
}

doca_error_t
allreduce_ucx_am_recv(struct allreduce_ucx_am_desc *am_desc, void *buffer, size_t length,
		      allreduce_ucx_callback callback, void *arg, struct allreduce_ucx_request **request_p)
{
	struct allreduce_ucx_connection *connection = am_desc->connection;
	struct allreduce_ucx_context *context = connection->context;
	ucp_request_param_t param = {
		/* Completion callback and user data are specified */
		.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
		/* Completion callback */
		.cb.recv_am = am_recv_data_request_callback,
		/* User data is context which owns the receive operation */
		.user_data = context
	};
	ucs_status_ptr_t status_ptr;

	if (am_desc->flags & UCP_AM_RECV_ATTR_FLAG_RNDV) {
		/* if the received AM descriptor is just a notification about Rendezvous, start receiving the whole data */
		status_ptr = ucp_am_recv_data_nbx(context->worker, am_desc->data_desc, buffer, length, &param);
	} else {
		/* The whole data was read, just copy it to the user's buffer */
		status_ptr = NULL;
#ifdef GPU_SUPPORT
		/* Cannot use cudaMemcpyAsync since "am_desc->data_desc" is freed when this function return.
		 * We also can't use "ucp_am_data_release" without making sure the copy is finished (requires
		 * synchronization with the GPU). We also can't use "ucp_am_data_release" as CUDA callback
		 * since it is not thread-safe in the current worker configuration.
		 */
		cudaMemcpy(buffer, am_desc->data_desc, MIN(length, am_desc->length), cudaMemcpyDefault);
#else
		memcpy(buffer, am_desc->data_desc, MIN(length, am_desc->length));
#endif
	}

	/* Process 'status_ptr' */
	return request_process(connection, ALLREDUCE_UCX_AM_RECV_DATA, status_ptr, callback, arg, 1, request_p);
}

void
allreduce_ucx_am_desc_query(struct allreduce_ucx_am_desc *am_desc, struct allreduce_ucx_connection **connection,
			    const void **header, size_t *header_length, size_t *length)
{
	*connection = am_desc->connection;
	*header = am_desc->header;
	*header_length = am_desc->header_length;
	*length = am_desc->length;
}

/*
 * Proxy to handle AM receive operation and call user's callback, it will initialize the Active Message descriptor
 * with info on the incoming message and pass it to the callback
 *
 * @arg [in]: Argument to pass to the user's callback
 * @header [in]: The header of the incoming message
 * @header_length [in]: The size of the header in bytes
 * @data_desc [in]: Active Message descriptor to initialize
 * @length [in]: The size of the incoming buffer in bytes
 * @param [in]: UCX receive parameters
 * @return: UCS_OK
 */
static ucs_status_t
am_recv_callback(void *arg, const void *header, size_t header_length, void *data_desc, size_t length,
		 const ucp_am_recv_param_t *param)
{
	struct allreduce_ucx_am_callback_info *callback_info = arg;
	struct allreduce_ucx_context *context = callback_info->context;
	ucp_ep_h ep = param->reply_ep;
	struct allreduce_ucx_connection *connection;
	struct allreduce_ucx_am_desc am_desc;

	/* Try to find connection in the hash of the connections where key is the UCP EP */
	connection = g_hash_table_lookup(context->ep_to_connections_hash, ep);
	assert(connection != NULL);

	/* Fill AM descriptor which will be passed to the user and used to fetch the data then by
	 * 'allreduce_ucx_am_recv'
	 */
	am_desc.connection = connection;
	am_desc.flags = param->recv_attr;
	am_desc.header = header;
	am_desc.header_length = header_length;
	am_desc.data_desc = data_desc;
	am_desc.length = length;

	/* Invoke user's callback specified for the AM ID */
	INVOKE_CALLBACK(callback_info->callback(&am_desc));
	return UCS_OK;
}

/*
 * Sets a callback to be invoked with an Active Message descriptor once an incoming message arrives with the given ID
 *
 * @worker [in]: UCP worker context to configure with the callback
 * @am_id [in]: Active Message ID to associate with the callback
 * @cb [in]: Callback to use for incoming messages with the given ID
 * @arg [in]: Additional argument to pass to the callback
 */
static void
am_set_recv_handler_common(ucp_worker_h worker, unsigned int am_id, ucp_am_recv_callback_t cb, void *arg)
{
	ucp_am_handler_param_t param = {
		/* AM identifier, callback and argument are set */
		.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_CB |
				UCP_AM_HANDLER_PARAM_FIELD_ARG,
		/* Active Message (AM) identifier */
		.id = am_id,
		/* User's callback which should be called upon receiving */
		.cb = cb,
		/* User's argument which should be passed to user's callback upon receiving */
		.arg = arg
	};

	/* Specify AM receive handler to the UCP worker */
	ucp_worker_set_am_recv_handler(worker, &param);
}

void
allreduce_ucx_am_set_recv_handler(struct allreduce_ucx_context *context, unsigned int am_id,
				  allreduce_ucx_am_callback callback)
{
	if (context->am_callback_infos == NULL) {
		/* Array of AM callback infos wasn't allocated yet, allocate it now */
		context->am_callback_infos = malloc((context->max_am_id + 1) * sizeof(*context->am_callback_infos));
		if (context->am_callback_infos == NULL) {
			DOCA_LOG_ERR("Failed to allocate memory to hold AM callbacks");
			return;
		}
	}

	/* Save user's callback for further invoking it then upon receiving data */
	context->am_callback_infos[am_id].context = context;
	context->am_callback_infos[am_id].callback = callback;
	am_set_recv_handler_common(context->worker, am_id, am_recv_callback, &context->am_callback_infos[am_id]);
}

/***** Connection establishment *****/

/*
 * Active Message (AM) callback to receive connection check message
 *
 * @arg [in]: Ignored
 * @header [in]: Ignored
 * @header_length [in]: Ignored
 * @data [in]: Ignored
 * @length [in]: Ignored
 * @param [in]: Ignored
 * @return: UCS_OK
 */
static ucs_status_t
am_connection_check_recv_callback(void *arg, const void *header, size_t header_length, void *data, size_t length,
				  const ucp_am_recv_param_t *param)
{
	(void)arg;
	(void)header;
	(void)header_length;
	(void)data;
	(void)length;
	(void)param;

	return UCS_OK;
}

/*
 * Common functionality to disconnect a connection
 *
 * @connection [in]: Connection to disconnect
 * @flags [in]: Flags to use in the close parameters of "ucp_ep_close_nbx"
 */
static void
disconnect_common(struct allreduce_ucx_connection *connection, uint32_t flags)
{
	struct allreduce_ucx_context *context = connection->context;
	ucp_request_param_t close_params = {
		/* Indicate that flags parameter is specified */
		.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS,
		/* UCP EP closure flags */
		.flags = flags
	};
	ucs_status_t status;
	ucs_status_ptr_t close_req;

	if (connection->ep == NULL) {
		/* Disconnection has already been scheduled */
		return;
	}

	g_hash_table_remove(active_connections_hash, connection);
	g_hash_table_steal(context->ep_to_connections_hash, connection->ep);

	/* Close request is equivalent to an async-handler to know the close operation status */
	close_req = ucp_ep_close_nbx(connection->ep, &close_params);
	if (UCS_PTR_IS_PTR(close_req)) {
		/* Wait completion of UCP EP close operation */
		do {
			/* Progress UCP worker */
			ucp_worker_progress(context->worker);
			status = ucp_request_check_status(close_req);
		} while (status == UCS_INPROGRESS);
		/* Free UCP request */
		ucp_request_free(close_req);
	}

	/* Set UCP EP to NULL to catch possible use after UCP EP closure */
	connection->ep = NULL;
}

/*
 * Releases an existing connection
 *
 * @connection [in]: Connection to free
 */
static inline void
connection_deallocate(struct allreduce_ucx_connection *connection)
{
	free(connection->destination_address);
	free(connection);
}

/*
 * Cleanup callback for glibc hash tables, to properly destroy the ep_to_connections hashtable
 *
 * @data [in]: Pointer to a connection
 */
static void
destroy_connection_callback(gpointer data)
{
	struct allreduce_ucx_connection *connection = data;

	disconnect_common(connection, UCP_EP_CLOSE_FLAG_FORCE);
	connection_deallocate(connection);
}

/*
 * Allocate a new connection to the context
 *
 * @context [in]: Context to use for creating the connection
 * @ep_params [in]: Configurations of the underline UCX endpoint
 * @return: The created connection
 */
static struct allreduce_ucx_connection *
connection_allocate(struct allreduce_ucx_context *context, ucp_ep_params_t *ep_params)
{
	struct allreduce_ucx_connection *connection;

	connection = malloc(sizeof(*connection));
	if (connection == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for connection");
		return NULL;
	}

	connection->ep = NULL;
	connection->context = context;

	if (ep_params->flags & UCP_EP_PARAMS_FLAGS_CLIENT_SERVER) {
		/* Allocate memory to hold destination address which could be used for reconnecting */
		connection->destination_address = malloc(sizeof(*connection->destination_address));
		if (connection->destination_address == NULL) {
			DOCA_LOG_ERR("Failed to allocate memory to hold destination address");
			free(connection);
			return NULL;
		}

		/* Fill destination address by socket address used for conenction establishment */
		*connection->destination_address = *(const struct sockaddr_storage *)ep_params->sockaddr.addr;
	} else {
		connection->destination_address = NULL;
	}

	return connection;
}

/* Forward declaration */
static void
error_callback(void *arg, ucp_ep_h ep, ucs_status_t status);

/*
 * Common functionality to connect a remote node
 *
 * @context [in]: Context to use for creating the connection
 * @ep_params [in]: Configurations for the underline UCX endpoint
 * @connection_p [out]: The created connection
 * @return: 0 on success and negative value otherwise
 */
static int
connect_common(struct allreduce_ucx_context *context, ucp_ep_params_t *ep_params,
	       struct allreduce_ucx_connection **connection_p)
{
	struct allreduce_ucx_connection *connection;
	ucs_status_t status;

	if (*connection_p == NULL) {
		/* It is normal connection establishment - allocate the new connection object */
		connection = connection_allocate(context, ep_params);
		if (connection == NULL)
			return -1;
	} else {
		/* User is reconnecting - use connection from a passed pointer and do reconnection */
		connection = *connection_p;
		assert(connection->context == context);
	}

	/* Error handler and error handling mode are specified */
	ep_params->field_mask |= UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
	/* Error handling PEER mode is needed to detect disconnection of clients on daemon and close the endpoint */
	ep_params->err_mode = UCP_ERR_HANDLING_MODE_PEER;
	/* Error callback */
	ep_params->err_handler.cb = error_callback;
	/* Argument which will be passed to error callback */
	ep_params->err_handler.arg = connection;

	assert(connection->ep == NULL);

	/* Create UCP EP */
	status = ucp_ep_create(context->worker, ep_params, &connection->ep);
	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to create UCP endpoint: %s", ucs_status_string(status));
		if (*connection_p == NULL) {
			/* Destroy only if allocated here */
			connection_deallocate(connection);
		}
		return -1;
	}

	/* Insert the new connection to the context's hash of connections */
	g_hash_table_insert(context->ep_to_connections_hash, connection->ep, connection);
	g_hash_table_insert(active_connections_hash, connection, connection);

	*connection_p = connection;
	return 0;
}

/*
 * Connect to a specific IP and port
 *
 * @context [in]: Context to use for creating the connection
 * @dst_saddr [in]: IP address and port of the destination
 * @connection_p [out]: The created connection
 * @return: 0 on success and negative value otherwise
 */
static int
sockaddr_connect(struct allreduce_ucx_context *context, const struct sockaddr_storage *dst_saddr,
		 struct allreduce_ucx_connection **connection_p)
{
	ucp_ep_params_t ep_params = {
		/* Flags and socket address are specified */
		.field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR,
		/* Client-server connection establishment mode */
		.flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER,
		/* Peer's socket address */
		.sockaddr.addr = (const struct sockaddr *)dst_saddr,
		/* Size of socket address */
		.sockaddr.addrlen = sizeof(*dst_saddr)
	};

	/* Connect to a peer */
	return connect_common(context, &ep_params, connection_p);
}

/*
 * Callback which is invoked upon error detection in a UCX operation
 *
 * @arg [in]: The connection in which the error occurred
 * @ep [in]: UCX endpoint in which the error occurred
 * @status [in]: The error code of the operation
 */
static void
error_callback(void *arg, ucp_ep_h ep, ucs_status_t status)
{
	(void)ep;
	(void)status;

	struct allreduce_ucx_connection *connection = (struct allreduce_ucx_connection *)arg;
	int result;

	/* Disconnect from a peer forcibly */
	disconnect_common(connection, UCP_EP_CLOSE_FLAG_FORCE);
	if (connection->destination_address == NULL) {
		/* If the connection was created from callback - can free the memory */
		free(connection);
		return;
	}

	/* Reconnect to the peer */
	result = sockaddr_connect(connection->context, connection->destination_address, &connection);
	if (result < 0) {
		/* Can't reconnect - print the error message */
		DOCA_LOG_ERR("Connection to peer/daemon broke and attempts to reconnect fail");
		connection_deallocate(connection);
	}
}

/*
 * Set the sockaddr object with the given IP and port
 *
 * @ip_str [in]: IP address to use
 * @port [in]: port number to use
 * @saddr [in]: Allocated object to be initialized
 * @return: 0 on success and negative value otherwise
 */
static int
set_sockaddr(const char *ip_str, uint16_t port, struct sockaddr_storage *saddr)
{
	struct sockaddr_in *sa_in = (struct sockaddr_in *)saddr;
	struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)saddr;

	/* Try to convert string representation of the IPv4 address to the socket address */
	if (inet_pton(AF_INET, ip_str, &sa_in->sin_addr) == 1) {
		/* Success - set family and port */
		sa_in->sin_family = AF_INET;
		sa_in->sin_port = htons(port);
		return 0;
	}

	/* Try to convert string representation of the IPv6 address to the socket address */
	if (inet_pton(AF_INET6, ip_str, &sa_in6->sin6_addr) == 1) {
		/* Success - set family and port */
		sa_in6->sin6_family = AF_INET6;
		sa_in6->sin6_port = htons(port);
		return 0;
	}

	DOCA_LOG_ERR("Invalid address: '%s'", ip_str);
	return -1;
}

/*
 * Converts "saddr" to a string of IP and port in the format "IP:port"
 *
 * @saddr [in]: The socket-address struct to be converted
 * @buf_len [in]: The buffer length
 * @buf [out]: Buffer to hold the string
 */
static void
sockaddr_str(const struct sockaddr *saddr, size_t buf_len, char *buf)
{
	uint16_t port;

	if (saddr->sa_family != AF_INET) {
		snprintf(buf, buf_len, "%s", "<unknown address family>");
		return;
	}

	switch (saddr->sa_family) {
	case AF_INET:
		/* IPv4 address */
		inet_ntop(AF_INET, &((const struct sockaddr_in *)saddr)->sin_addr, buf, buf_len);
		port = ntohs(((const struct sockaddr_in *)saddr)->sin_port);
		break;
	case AF_INET6:
		/* IPv6 address */
		inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)saddr)->sin6_addr, buf, buf_len);
		port = ntohs(((const struct sockaddr_in6 *)saddr)->sin6_port);
		break;
	default:
		snprintf(buf, buf_len, "%s", "<invalid address>");
		return;
	}

	snprintf(buf + strlen(buf), buf_len - strlen(buf), ":%u", port);
}

int
allreduce_ucx_connect(struct allreduce_ucx_context *context, const char *dest_ip_str, uint16_t dest_port,
		      struct allreduce_ucx_connection **connection_p)
{
	struct sockaddr_storage dst_saddr;
	int result;
	uint8_t dummy[0];
	struct allreduce_ucx_request *request;

	assert(dest_ip_str != NULL);
	/* Set IP address and port specified by a user to the socket address */
	result = set_sockaddr(dest_ip_str, dest_port, &dst_saddr);
	if (result < 0)
		return result;

	*connection_p = NULL;

	/* Connect to the peer using socket address generated above */
	result = sockaddr_connect(context, &dst_saddr, connection_p);
	if (result < 0)
		return result;

	/* Try sending connection check AM to make sure the new UCP EP is successfully connected to the peer */
	do {
		/* If sending AM fails, reconnection will be done form the error callback */
		request = NULL;
		result = am_send(*connection_p, context->max_am_id + 1, 0, &dummy, 0, NULL, 0, NULL, NULL, &request);
		if (result == 0)
			result = allreduce_ucx_request_wait(result, request);
	} while (result < 0);

	return result;
}

/*
 * Callback which is invoked upon receiving incoming connection attempt, it will accept the connection request
 * and properly store it
 *
 * @conn_req [in]: The connection request
 * @arg [in]: The "allreduce_ucx" context in which the connection request has arrived
 */
static void
connect_callback(ucp_conn_request_h conn_req, void *arg)
{
	struct allreduce_ucx_context *context = (struct allreduce_ucx_context *)arg;
	ucp_ep_params_t ep_params = {
		/* Connection request is specified */
		.field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST,
		/* Connection request */
		.conn_request = conn_req
	};
	ucp_conn_request_attr_t conn_req_attr = {
		/* Request getting the client's address which is an initiator of the connection */
		.field_mask = UCP_CONN_REQUEST_ATTR_FIELD_CLIENT_ADDR
	};
	struct allreduce_ucx_connection *connection = NULL;
	char buf[128];
	ucs_status_t status;

	/* Query connection request information */
	status = ucp_conn_request_query(conn_req, &conn_req_attr);
	if (status == UCS_OK) {
		sockaddr_str((const struct sockaddr *)&conn_req_attr.client_address, sizeof(buf), buf);
		DOCA_DLOG_DBG("Got new connection request %p from %s", conn_req, buf);
	} else {
		DOCA_LOG_ERR("Got new connection request %p, connection request query failed: %s", conn_req,
						ucs_status_string(status));
	}

	/* Connect to the peer by accepting the incoming connection */
	connect_common(context, &ep_params, &connection);
}

void
allreduce_ucx_disconnect(struct allreduce_ucx_connection *connection)
{
	if (g_hash_table_lookup(active_connections_hash, connection) == NULL)
		return;  /* already been disconnected */

	/* Normal disconnection from a peer with flushing all operations */
	disconnect_common(connection, 0);
	connection_deallocate(connection);
}

/***** Main UCX operations *****/

doca_error_t
allreduce_ucx_init(unsigned int max_am_id, struct allreduce_ucx_context **context_p)
{
	ucp_params_t context_params = {
		/* Features, request initialize callback and request size are specified */
		.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_REQUEST_INIT | UCP_PARAM_FIELD_REQUEST_SIZE,
		/* Request support for Active messages (AM) in a UCP context */
		.features = UCP_FEATURE_AM,
		/* Function which will be invoked to fill UCP request upon allocation */
		.request_init = request_init,
		/* Size of UCP request */
		.request_size = sizeof(struct allreduce_ucx_request)
	};
	ucp_worker_params_t worker_params = {
		/* Thread mode is specified */
		.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
		/* UCP worker progress and all send/receive operations must be called from a single thread at the same
		 * time
		 */
		.thread_mode = UCS_THREAD_MODE_SINGLE
	};
	ucs_status_t status;
	struct allreduce_ucx_context *context;

	context = malloc(sizeof(*context));
	if (context == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory for UCX context");
		return DOCA_ERROR_NO_MEMORY;
	}

	context->am_callback_infos = NULL;
	context->listener = NULL;

	/* Save maximum AM ID which will be specified by the user */
	context->max_am_id = max_am_id;

	/* Allocate hash to hold all connections created by user or accepted from a peer */
	context->ep_to_connections_hash =
		g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, destroy_connection_callback);
	if (context->ep_to_connections_hash == NULL) {
		free(context);
		return DOCA_ERROR_NO_MEMORY;
	}
	active_connections_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
	if (active_connections_hash == NULL) {
		g_hash_table_destroy(context->ep_to_connections_hash);
		free(context);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* UCP has default config that is set by env vars, we don't need to change it, so using NULL */
	status = ucp_init(&context_params, NULL, &context->context);
	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to create UCP context: %s", ucs_status_string(status));
		g_hash_table_destroy(active_connections_hash);
		g_hash_table_destroy(context->ep_to_connections_hash);
		free(context);
		return DOCA_ERROR_INITIALIZATION;
	}

	/* Create UCP worker */
	status = ucp_worker_create(context->context, &worker_params, &context->worker);
	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to create UCP worker: %s", ucs_status_string(status));
		ucp_cleanup(context->context);
		g_hash_table_destroy(active_connections_hash);
		g_hash_table_destroy(context->ep_to_connections_hash);
		free(context);
		return DOCA_ERROR_INITIALIZATION;
	}

	/* Use 'max_am_id + 1' to set AM callback to receive connection check message */
	am_set_recv_handler_common(context->worker, context->max_am_id + 1, am_connection_check_recv_callback, NULL);

	*context_p = context;

	return DOCA_SUCCESS;
}

void
allreduce_ucx_destroy(struct allreduce_ucx_context *context)
{
	/* Destroy all created connections inside hash destroy operation */
	g_hash_table_destroy(context->ep_to_connections_hash);
	/* Destroy this table after the above because cleanup method for values in the above table uses both tables */
	g_hash_table_destroy(active_connections_hash);

	if (context->listener != NULL) {
		/* Destroy UCP listener if it was created by a user */
		ucp_listener_destroy(context->listener);
	}

	/* Destroy UCP worker */
	ucp_worker_destroy(context->worker);
	/* Destroy UCP context */
	ucp_cleanup(context->context);

	free(context->am_callback_infos);
	free(context);
}

int
allreduce_ucx_listen(struct allreduce_ucx_context *context, uint16_t port)
{
	/* Listen on any IPv4 address and the user-specified port */
	const struct sockaddr_in listen_addr = {
		/* Set IPv4 address family */
		.sin_family = AF_INET,
		.sin_addr = {
			/* Set any address */
			.s_addr = INADDR_ANY
		},
		/* Set port from the user */
		.sin_port = htons(port)
	};
	ucp_listener_params_t listener_params = {
		/* Socket address and conenction handler are specified */
		.field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR | UCP_LISTENER_PARAM_FIELD_CONN_HANDLER,
		/* Listen address */
		.sockaddr.addr = (const struct sockaddr *)&listen_addr,
		/* Size of listen address */
		.sockaddr.addrlen = sizeof(listen_addr),
		/* Incoming connection handler */
		.conn_handler.cb = connect_callback,
		/* UCX context which is owner of the connection */
		.conn_handler.arg = context
	};
	ucs_status_t status;

	/* Create UCP listener to accept incoming connections */
	status = ucp_listener_create(context->worker, &listener_params, &context->listener);
	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to create UCP listener: %s", ucs_status_string(status));
		return -1;
	}

	return 0;
}

doca_error_t
allreduce_ucx_progress(struct allreduce_ucx_context *context)
{
	/* Progress send and receive operations on UCP worker */
	ucp_worker_progress(context->worker);
	return callback_errno;
}
