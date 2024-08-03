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

#ifndef ALLREDUCE_CORE_H_
#define ALLREDUCE_CORE_H_

/* Define _GNU_SOURCE which is required to use GNU hash from glib.h */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <sys/queue.h>
#ifdef GPU_SUPPORT
#include <cuda_runtime_api.h>
#include <cuda.h>
#endif

#include <doca_argp.h>
#include <doca_log.h>

#include "allreduce_utils.h"
#include "allreduce_ucx.h"

#ifndef STAILQ_FOREACH_SAFE
/*
 * A for-each loop that allows a node to be removed or freed within the loop.
 * Traverses the tail queue referenced by _list in the forward direction, assigning each element in turn to _node.
 *
 * @NOTE: This is an implemntation equivalent to "STAILQ_FOREACH_SAFE" of BSD
 */
#define STAILQ_FOREACH_SAFE(_node, _list, _name, _temp_node) \
	for (_node = STAILQ_FIRST(_list), \
		_temp_node = ((_node) != NULL) ? STAILQ_NEXT(_node, _name) : NULL; \
		(_node) != NULL; \
		_node = _temp_node, \
		_temp_node = ((_node) != NULL) ? STAILQ_NEXT(_node, _name) : NULL)

#endif

enum allreduce_role {
	ALLREDUCE_CLIENT,	/* Allreduce client (available for non-offloaded and offloaded modes) */
	ALLREDUCE_DAEMON	/* Allreduce daemon (available for offloaded mode only) */
};

enum allreduce_mode {
	ALLREDUCE_NON_OFFLOADED_MODE,	/* Non-offloaded allreduce algorithm, requires connection between clients only
					 */
	ALLREDUCE_OFFLOAD_MODE		/* Offloaded allreduce algorithm, requires connection between clients and
					 * daemons
					 */
};

enum allreduce_am_id {
	ALLREDUCE_CTRL_AM_ID,		/* Sent by clients to daemon to notify about allreduce operations, and sent by
					 * daemons to clients to notify about completions of allreduce operations
					 */
	ALLREDUCE_OP_AM_ID,		/* Exchanged by clients or daemons to perform allreduce operations */
	ALLREDUCE_HANDSHAKE_AM_ID,	/* Exchanged by new connections, to validate "allreduce_config" is the same */
	ALLREDUCE_MAX_AM_ID		/* Maximum AM identifier used by the application */
};

enum allreduce_datatype {
	ALLREDUCE_BYTE,		/* Indicates the vector elements should be interperated as byte */
	ALLREDUCE_INT,		/* Indicates the vector elements should be interperated as int */
	ALLREDUCE_FLOAT,	/* Indicates the vector elements should be interperated as float */
	ALLREDUCE_DOUBLE	/* Indicates the vector elements should be interperated as double */
};

enum allreduce_operation {
	ALLREDUCE_SUM,		/* Indicates the operation between vector should be element-element summation */
	ALLREDUCE_PROD		/* Indicates the operation between vector should be element-element product */
};

struct allreduce_config {
	enum allreduce_role role;		/* Indicates whether the process is daemon or client */
	uint16_t dest_port;			/* Peer's port which should be used if the port isn't specified in the
						 * string of the addresses
						 */
	uint16_t listen_port;			/* Port which should be used to list for incoming connections */
	size_t num_clients;			/* Indicates how many client's connections should be expected by
						 * daemons (it should be utilized for daemons only)
						 */
	size_t vector_size;			/* Allreduce vector size */
	enum allreduce_datatype datatype;	/* Datatype of allreduce element */
	enum allreduce_operation operation;	/* Allreduce operation */
	size_t batch_size;			/* Number of allreduce operations to submit simultaneously and wait
						 * compeltion for
						 */
	size_t num_batches;			/* Indicates how many batches should be performed by clients */
	enum allreduce_mode allreduce_mode;	/* Allreduce algorithm which should be used */
	struct {
		union {
			STAILQ_HEAD(, allreduce_address) list;	/* Valid after calling dest_addresses_init() */
			char *str;					/* Valid before calling dest_addresses_init()
									 */
		};
		size_t num;						/* Number of peer's addresses */
	} dest_addresses;						/* Destination addresses */
};

struct allreduce_address {
	STAILQ_ENTRY(allreduce_address) entry;	/* List entry */
	char ip_address_str[64];		/* Peer's IP address string */
	uint16_t port;				/* Peer's port */
};

struct allreduce_header {
	size_t id;	/* Allreduce operation identifier */
};

/*
 * Request of allreduce operation which supervises 'allreduce_request' operations which do some parts of complex
 * allreduce operation, e.g. receiving initial data from clients on daemon to do allreduce for
 */
struct allreduce_super_request {
	STAILQ_HEAD(allreduce_requests, allreduce_request) allreduce_requests_list;	/* List of allreduce requests
											 * received by daemons to
											 * perform allreduce operation
											 */
	size_t num_allreduce_requests;		/* Number of allreduce requests received by daemons and not completed
						 * yet
						 */
	size_t num_allreduce_operations;	/* Number of send and receive operations that are not completed yet
						 * between peers (daemons or non-offloaded clients) to consider an
						 * allreduce operation done
						 */
	int result_vector_owner;		/* Indicates memory ownership over the result vectors */
	void *result_vector;			/* Allreduce result vector */
	void *peer_result_vector;		/* Allreduce result vector for peers vectors only */
	size_t recv_vector_iter;		/* Indicated how many receive vectors are filled by data received from
						 * daemons or clients
						 */
	void **recv_vectors;			/* Receive vectors to hold vectors from peers */
	struct allreduce_header header;		/* Header of allreduce operation
						 */
	size_t result_vector_size;		/* Size of the allreduce result vector */
#ifdef GPU_SUPPORT
	cudaStream_t *stream;			/* GPU stream for the GPU operations of this request */
	void **clients_recv_vectors;		/* Receive vectors to hold vectors from clients */
#endif
};

/*
 * Request of allreduce operation which defines some part of complex allreduce operation, e.g. receiving initial data
 * from clients on daemon to do allreduce for
 */
struct allreduce_request {
	STAILQ_ENTRY(allreduce_request) entry;				/* List entry */
	struct allreduce_super_request *allreduce_super_request;	/* Owner of allreduce operation */
	struct allreduce_header header;					/* Header of allreduce operation */
	struct allreduce_ucx_connection *connection;			/* Connection on which an allreduce operation
									 * was sent on clients or received on daemons
									 */
	void *vector;							/* Vector which contains data to send as a part
									 * of an allreduce operation
									 */
	size_t vector_size;						/* Size of a vector which should be send as a
									 * part of an allreduce operation
									 */
};

extern const char * const allreduce_mode_str[];
extern const char * const allreduce_datatype_str[];
extern const size_t allreduce_datatype_size[];
extern const char * const allreduce_operation_str[];
extern struct allreduce_config allreduce_config;
extern struct allreduce_ucx_context *context;
extern struct allreduce_ucx_connection **connections;
extern size_t client_active_allreduce_requests;

/*
 * Register the command line parameters for the application
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_allreduce_params(void);

/*
 * Properly frees a client request
 *
 * @allreduce_request [in]: Allocated request to properly free
 *
 * @NOTE: External use should be done only if an unexpected exit is required while processing unregistered new request
 */
void allreduce_request_destroy(struct allreduce_request *allreduce_request);

/*
 * Returns an existing super_request according to the given allreduce_header or allocate a new super_request if no
 * existing request is found
 *
 * @header [in]: Header that is associate with a super_request
 * @result_length [in]: The expected/given result vector size
 * @result_vector [in]: Result vector to use. If NULL, the first received vector from a client will be the result vector
 * @return: Pointer to the allocated struct
 */
struct allreduce_super_request *allreduce_super_request_get(const struct allreduce_header *header,
							    size_t result_length, void *result_vector);

/*
 * Marks a super-request and releases the memory of the related super-request.
 *
 * @req_id [in]: An ID of an existing super-request
 */
void allreduce_super_request_finish(size_t req_id);

/*
 * Send the result vector to the daemon or to all the peers
 *
 * @allreduce_super_request [in]: The super request to scatter to peers
 *
 * @NOTE: Used by daemon to send the local result vectors composed from all the local clients
 */
void allreduce_scatter(struct allreduce_super_request *allreduce_super_request);

/*
 * Allocates a new client request
 *
 * @connection [in]: The UCP connection that the request arrived from
 * @header [in]: The header of the arrived request
 * @length [in]: The length (in bytes) of the vector that arrived in the request
 * @return: Pointer to the allocated struct
 */
struct allreduce_request *allreduce_request_allocate(struct allreduce_ucx_connection *connection,
						     const struct allreduce_header *header, size_t length);

/*
 * Initialize UCX, connects to destination addresses, and allocates memory pools
 *
 * @num_connections [out]: The number of established connections to peers/daemon
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t allreduce_init(int *num_connections);

/*
 * Cleanup UCX related resources
 *
 * @num_connection [in]: The number of established connections to peers/daemon
 */
void allreduce_destroy(int num_connection);

#endif /* ALLREDUCE_CORE_H_ */
