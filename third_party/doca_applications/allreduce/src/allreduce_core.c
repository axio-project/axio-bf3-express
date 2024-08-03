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
#include <string.h>
#include <sys/time.h>
#include <glib.h>

#include <utils.h>

#include "allreduce_core.h"
#include "allreduce_reducer.h"
#include "allreduce_mem_pool.h"

DOCA_LOG_REGISTER(ALLREDUCE::Core);

#define HANDSHAKE_MAX_MSG_LEN	1024
#define HANDSHAKE_MSG_FMT	"-s %zu -d %s -b %zu -i %zu"

/* Incoming handshake arguments which could be passed to send/recv callbacks */
struct allreduce_incoming_handshake_arg {
	struct allreduce_ucx_connection *connection;	/* Connection to the peer */
	enum allreduce_role peer_role;			/* Indicates whether the peer is daemon or client */
	size_t length;					/* Length of the handshake message */
	char msg[HANDSHAKE_MAX_MSG_LEN];		/* Buffer to hold handshake message */
};

/* Names of allreduce process modes */
const char * const allreduce_role_str[] = {
	[ALLREDUCE_CLIENT] = "client",	/* Name of client allreduce role */
	[ALLREDUCE_DAEMON] = "daemon"	/* Name of daemon allreduce role */
};
/* Names of allreduce algorithms */
const char * const allreduce_mode_str[] = {
	[ALLREDUCE_NON_OFFLOADED_MODE] = "non-offloaded",	/* Name of non-offloaded allreduce algorithm */
	[ALLREDUCE_OFFLOAD_MODE] = "offloaded"			/* Name of offloaded allreduce algorithm */
};
const char * const allreduce_datatype_str[] = {
	[ALLREDUCE_BYTE] = "byte",	/* Name of "byte" datatype */
	[ALLREDUCE_INT] = "int",	/* Name of "int" datatype */
	[ALLREDUCE_FLOAT] = "float",	/* Name of "float" datatype */
	[ALLREDUCE_DOUBLE] = "double"	/* Name of "double" datatype */
};
const size_t allreduce_datatype_size[] = {
	[ALLREDUCE_BYTE] = sizeof(uint8_t),	/* Size of "byte" datatype */
	[ALLREDUCE_INT] = sizeof(int),		/* Size of "int" datatype */
	[ALLREDUCE_FLOAT] = sizeof(float),	/* Size of "float" datatype */
	[ALLREDUCE_DOUBLE] = sizeof(double)	/* Size of "double" datatype */
};
const char * const allreduce_operation_str[] = {
	[ALLREDUCE_SUM] = "sum",	/* Name of summation of two vector elements */
	[ALLREDUCE_PROD] = "prod"	/* Name of product of two vector elements */
};
struct allreduce_config allreduce_config = {};	/* UCX allreduce configuration */
struct allreduce_ucx_context *context;		/* UCX context */
struct allreduce_ucx_connection **connections;	/* Array of UCX connections */
GHashTable *allreduce_super_requests_hash;	/* Hash which contains "ID -> allreduce super request" elements */
size_t client_active_allreduce_requests;	/* Number of allreduce operations which are submitted on a client */

/*
 * ARGP Callback - Handle the program role parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_role_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;
	const char *str = (const char *) param;

	if (strcmp(str, allreduce_role_str[ALLREDUCE_CLIENT]) == 0)
		app_config->role = ALLREDUCE_CLIENT;
	else if (strcmp(str, allreduce_role_str[ALLREDUCE_DAEMON]) == 0)
		app_config->role = ALLREDUCE_DAEMON;
	else {
		DOCA_LOG_ERR("Unknown role '%s' was specified", str);
		return DOCA_ERROR_NOT_SUPPORTED;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the mode parameter for Client roles
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_allreduce_mode_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;
	const char *str = (const char *) param;

	if (strcmp(str, allreduce_mode_str[ALLREDUCE_OFFLOAD_MODE]) == 0)
		app_config->allreduce_mode = ALLREDUCE_OFFLOAD_MODE;
	else if (strcmp(str, allreduce_mode_str[ALLREDUCE_NON_OFFLOADED_MODE]) == 0)
		app_config->allreduce_mode = ALLREDUCE_NON_OFFLOADED_MODE;
	else {
		DOCA_LOG_ERR("Unknown mode '%s' was specified", str);
		return DOCA_ERROR_NOT_SUPPORTED;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the destination addresses parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_dest_ip_str_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->dest_addresses.str = strdup((char *) param);
	if (app_config->dest_addresses.str == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory to hold a list of destination addresses '%s'", (char *) param);
		return DOCA_ERROR_NO_MEMORY;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the default destination port parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_dest_port_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->dest_port = *(uint16_t *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the listening port of daemon or non-offloaded client parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_listen_port_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->listen_port = *(uint16_t *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the number of offloaded clients that will connect to the daemon parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_num_clients_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->num_clients = *(int *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the size of vector for the allreduce parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_size_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->vector_size = *(int *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the datatype of vector elements parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_datatype_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;
	const char *str = (const char *) param;

	if (strcmp(str, allreduce_datatype_str[ALLREDUCE_BYTE]) == 0)
		app_config->datatype = ALLREDUCE_BYTE;
	else if (strcmp(str, allreduce_datatype_str[ALLREDUCE_INT]) == 0)
		app_config->datatype = ALLREDUCE_INT;
	else if (strcmp(str, allreduce_datatype_str[ALLREDUCE_FLOAT]) == 0)
		app_config->datatype = ALLREDUCE_FLOAT;
	else if (strcmp(str, allreduce_datatype_str[ALLREDUCE_DOUBLE]) == 0)
		app_config->datatype = ALLREDUCE_DOUBLE;
	else {
		DOCA_LOG_ERR("Unknown datatype '%s' was specified", str);
		return DOCA_ERROR_NOT_SUPPORTED;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the operation to do between allreduce vectors parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_operation_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;
	const char *str = (const char *) param;

	if (strcmp(str, allreduce_operation_str[ALLREDUCE_SUM]) == 0)
		app_config->operation = ALLREDUCE_SUM;
	else if (strcmp(str, allreduce_operation_str[ALLREDUCE_PROD]) == 0)
		app_config->operation = ALLREDUCE_PROD;
	else {
		DOCA_LOG_ERR("Unknown operation '%s' was specified", str);
		return DOCA_ERROR_NOT_SUPPORTED;
	}
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the number of allreduce operations submitted simultaneously parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_batch_size_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->batch_size = *(int *) param;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle the number of batches of allreduce operations parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_num_batches_param(void *param, void *config)
{
	struct allreduce_config *app_config = (struct allreduce_config *) config;

	app_config->num_batches = *(int *) param;
	return DOCA_SUCCESS;
}

doca_error_t
register_allreduce_params(void)
{
	doca_error_t result;
	struct doca_argp_param *role_param, *allreduce_mode_param, *dest_port_param, *dest_listen_port_param;
	struct doca_argp_param *num_clients_param, *size_param, *operation_param, *batch_size_param, *num_batches_param;
	struct doca_argp_param *dest_ip_str_param, *datatype_param;

	/* Create and register role param */
	result = doca_argp_param_create(&role_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(role_param, "r");
	doca_argp_param_set_long_name(role_param, "role");
	doca_argp_param_set_description(role_param, "Run DOCA UCX allreduce process as: \"client\" or \"daemon\"");
	doca_argp_param_set_callback(role_param, set_role_param);
	doca_argp_param_set_type(role_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(role_param);
	result = doca_argp_register_param(role_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register allreduce mode param */
	result = doca_argp_param_create(&allreduce_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(allreduce_mode_param, "m");
	doca_argp_param_set_long_name(allreduce_mode_param, "mode");
	doca_argp_param_set_arguments(allreduce_mode_param, "<allreduce_mode>");
	doca_argp_param_set_description(allreduce_mode_param, "Set allreduce mode: \"offloaded\", \"non-offloaded\" (valid for client only)");
	doca_argp_param_set_callback(allreduce_mode_param, set_allreduce_mode_param);
	doca_argp_param_set_type(allreduce_mode_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(allreduce_mode_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register destination port param */
	result = doca_argp_param_create(&dest_port_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(dest_port_param, "p");
	doca_argp_param_set_long_name(dest_port_param, "port");
	doca_argp_param_set_arguments(dest_port_param, "<port>");
	doca_argp_param_set_description(dest_port_param, "Set default destination port of daemons/clients, used for IPs without a port (see '-a' flag)");
	doca_argp_param_set_callback(dest_port_param, set_dest_port_param);
	doca_argp_param_set_type(dest_port_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(dest_port_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register listening port param */
	result = doca_argp_param_create(&dest_listen_port_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(dest_listen_port_param, "t");
	doca_argp_param_set_long_name(dest_listen_port_param, "listen-port");
	doca_argp_param_set_arguments(dest_listen_port_param, "<listen_port>");
	doca_argp_param_set_description(dest_listen_port_param, "Set listening port of daemon or client");
	doca_argp_param_set_callback(dest_listen_port_param, set_listen_port_param);
	doca_argp_param_set_type(dest_listen_port_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(dest_listen_port_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/*  Create and register clients number param, this parameter has affect for daemon proccess */
	result = doca_argp_param_create(&num_clients_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(num_clients_param, "c");
	doca_argp_param_set_long_name(num_clients_param, "num-clients");
	doca_argp_param_set_arguments(num_clients_param, "<num_clients>");
	doca_argp_param_set_description(num_clients_param, "Set the number of clients which participate in allreduce operations (valid for daemon only)");
	doca_argp_param_set_callback(num_clients_param, set_num_clients_param);
	doca_argp_param_set_type(num_clients_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(num_clients_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register vector size param */
	result = doca_argp_param_create(&size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(size_param, "s");
	doca_argp_param_set_long_name(size_param, "size");
	doca_argp_param_set_arguments(size_param, "<size>");
	doca_argp_param_set_description(size_param, "Set size of vector to do allreduce for");
	doca_argp_param_set_callback(size_param, set_size_param);
	doca_argp_param_set_type(size_param, DOCA_ARGP_TYPE_INT);
	result = doca_argp_register_param(size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register data type param */
	result = doca_argp_param_create(&datatype_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(datatype_param, "d");
	doca_argp_param_set_long_name(datatype_param, "datatype");
	doca_argp_param_set_arguments(datatype_param, "<datatype>");
	doca_argp_param_set_description(datatype_param, "Set datatype (\"byte\", \"int\", \"float\", \"double\") of vector elements to do allreduce for");
	doca_argp_param_set_callback(datatype_param, set_datatype_param);
	doca_argp_param_set_type(datatype_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(datatype_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register operation type param */
	result = doca_argp_param_create(&operation_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(operation_param, "o");
	doca_argp_param_set_long_name(operation_param, "operation");
	doca_argp_param_set_arguments(operation_param, "<operation>");
	doca_argp_param_set_description(operation_param, "Set operation (\"sum\", \"prod\") to do between allreduce vectors");
	doca_argp_param_set_callback(operation_param, set_operation_param);
	doca_argp_param_set_type(operation_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(operation_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register batch size param, this parameter has affect for client proccess */
	result = doca_argp_param_create(&batch_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(batch_size_param, "b");
	doca_argp_param_set_long_name(batch_size_param, "batch-size");
	doca_argp_param_set_arguments(batch_size_param, "<batch_size>");
	doca_argp_param_set_description(batch_size_param, "Set the number of allreduce operations submitted simultaneously (used for handshakes by daemons)");
	doca_argp_param_set_callback(batch_size_param, set_batch_size_param);
	doca_argp_param_set_type(batch_size_param, DOCA_ARGP_TYPE_INT);
	doca_argp_param_set_mandatory(batch_size_param);
	result = doca_argp_register_param(batch_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register batches number, this parameter has affect for client proccess */
	result = doca_argp_param_create(&num_batches_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(num_batches_param, "i");
	doca_argp_param_set_long_name(num_batches_param, "num-batches");
	doca_argp_param_set_arguments(num_batches_param, "<num_batches>");
	doca_argp_param_set_description(num_batches_param, "Set the number of batches of allreduce operations (used for handshakes by daemons)");
	doca_argp_param_set_callback(num_batches_param, set_num_batches_param);
	doca_argp_param_set_type(num_batches_param, DOCA_ARGP_TYPE_INT);
	doca_argp_param_set_mandatory(num_batches_param);
	result = doca_argp_register_param(num_batches_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register regex pci address param */
	result = doca_argp_param_create(&dest_ip_str_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(dest_ip_str_param, "a");
	doca_argp_param_set_long_name(dest_ip_str_param, "address");
	doca_argp_param_set_arguments(dest_ip_str_param, "<ip_address>");
	doca_argp_param_set_description(dest_ip_str_param, "Set comma-separated list of destination IPv4/IPv6 addresses and ports optionally (<ip_addr>:[<port>]) of daemons or clients");
	doca_argp_param_set_callback(dest_ip_str_param, set_dest_ip_str_param);
	doca_argp_param_set_type(dest_ip_str_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(dest_ip_str_param);
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

	return DOCA_SUCCESS;
}

/*
 * Close the connections to the peers/daemon abd free their resources
 *
 * @num_connections [in]: Number of connections
 */
static void
connections_cleanup(int num_connections)
{
	int i;

	/* Go over all connections and destroy them by disconnecting */
	for (i = 0; i < num_connections; ++i)
		allreduce_ucx_disconnect(connections[i]);

	free(connections);
}

/*
 * Connects offloaded clients to their daemon and connects daemons/non-offloaded-clients to other daemons/clients
 *
 * @return: On success, returns a positive number that indicates the number of connection and negative value otherwise
 */
static int
connections_init(void)
{
	struct allreduce_address *address;
	struct allreduce_ucx_connection *connection;
	int ret, num_connections = 0;

	connections = malloc(allreduce_config.dest_addresses.num * sizeof(*connections));
	if (connections == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory to hold array of connections");
		return -1;
	}

	/* Go over peer's addresses and establish connection to the peer using specified address */
	STAILQ_FOREACH(address, &allreduce_config.dest_addresses.list, entry) {
		connection = NULL;
		DOCA_DLOG_INFO("Connecting to %s", address->ip_address_str);
		ret = allreduce_ucx_connect(context, address->ip_address_str, address->port, &connection);
		if (ret < 0) {
			DOCA_LOG_ERR("Failed to establish connection");
			connections_cleanup(num_connections);
			return -1;
		}
		DOCA_DLOG_INFO("Connection established");

		/* Save connection to the array of connections */
		connections[num_connections++] = connection;
	}

	return num_connections;
}

/*
 * Hashes a key for glibc hash tables
 *
 * @v [in]: Key to hash
 * @return: The hash value as guint
 */
static guint
g_size_t_hash(gconstpointer v)
{
	return (guint) *(const size_t *)v;
}

/*
 * Checks if two keys are equal
 *
 * @v1 [in]: First Key
 * @v2 [in]: Second Key
 * @return: true/false - are the keys the same
 */
static gboolean
g_size_t_equal(gconstpointer v1, gconstpointer v2)
{
	return *((const size_t *)v1) == *((const size_t *)v2);
}

/*
 * Properly frees a super_request
 *
 * @allreduce_super_request [in]: Allocated super_request to properly free
 */
static void
allreduce_super_request_destroy(struct allreduce_super_request *allreduce_super_request)
{
	/* If a field is NULL it was already returned or was never used */
#ifdef GPU_SUPPORT
	allreduce_free_vec_streams_pool(allreduce_super_request->stream);
	if (allreduce_super_request->clients_recv_vectors != NULL) {
		/*
		 * If the super request is the owner of the result vector, one client vector was taken
		 * to be the result vector.
		 * This also means that "num_allreduce_requests >= result_vector_owner" is always true.
		 */
		allreduce_free_vecs_vecs_pool(allreduce_super_request->clients_recv_vectors,
					      allreduce_super_request->num_allreduce_requests -
						      allreduce_super_request->result_vector_owner);
		allreduce_free_vec_clients_bufs_pool(allreduce_super_request->clients_recv_vectors);
	}
#endif
	if (allreduce_super_request->result_vector_owner && allreduce_super_request->result_vector != NULL)
		allreduce_free_vec_vecs_pool(allreduce_super_request->result_vector);
	if (allreduce_super_request->peer_result_vector != NULL)
		allreduce_free_vec_vecs_pool(allreduce_super_request->peer_result_vector);
	if (allreduce_super_request->recv_vectors != NULL)
		allreduce_free_vec_bufs_pool(allreduce_super_request->recv_vectors);

	/* Free requests */
	struct allreduce_request *current, *next;

	if (allreduce_super_request->num_allreduce_requests > 0) {
		STAILQ_FOREACH_SAFE(current, &allreduce_super_request->allreduce_requests_list, entry, next)
		{
			/* A completion is sent only by daemons to clients */
			STAILQ_REMOVE(&allreduce_super_request->allreduce_requests_list, current, allreduce_request,
				      entry);
			allreduce_request_destroy(current);
		}
	}
	allreduce_free_vec_super_reqs_pool(allreduce_super_request);
}

/*
 * Cleanup callback for glibc hash tables, to properly destroy the super-requests-hashtable
 *
 * @data [in]: Pointer to an allreduce super request
 */
static void
allreduce_super_request_destroy_callback(gpointer data)
{
	allreduce_super_request_destroy((struct allreduce_super_request *)data);
}

void
allreduce_super_request_finish(size_t req_id)
{
	g_hash_table_remove(allreduce_super_requests_hash, &req_id);
}

/*
 * Allocates a new super_request
 *
 * @header [in]: Header content to associate with the super_request
 * @length [in]: The expected/given result vector size
 * @result_vector [in]: Result vector to use. If NULL, the first received vector from a client will be the result vector
 * @return: Pointer to the allocated struct
 */
static struct allreduce_super_request *
allreduce_super_request_allocate(const struct allreduce_header *header, size_t length, void *result_vector)
{
	struct allreduce_super_request *allreduce_super_request;

	if (allreduce_aloc_vec_super_reqs_pool((void **)&allreduce_super_request) != DOCA_SUCCESS)
		return NULL;

	/*
	 * First received vector from a peer will be the peers_result_vector, if there are more peers we need
	 * a buffer to hold other peers vectors. If there is only <=1 peers, the pool isn't initialized.
	 */
	if (allreduce_config.dest_addresses.num <= 1) {
		allreduce_super_request->recv_vectors = NULL;
	} else if (allreduce_aloc_vec_bufs_pool((void **)&allreduce_super_request->recv_vectors) != DOCA_SUCCESS) {
		allreduce_free_vec_super_reqs_pool(allreduce_super_request);
		return NULL;
	}

#ifdef GPU_SUPPORT
	/* Create GPU stream */
	if (allreduce_config.role == ALLREDUCE_CLIENT && allreduce_config.allreduce_mode == ALLREDUCE_OFFLOAD_MODE) {
		allreduce_super_request->stream = 0;  /* Default value, offloaded clients don't use the stream */
	} else if (allreduce_aloc_vec_streams_pool(&allreduce_super_request->stream) != DOCA_SUCCESS) {
		if (allreduce_super_request->recv_vectors != NULL)
			allreduce_free_vec_bufs_pool(allreduce_super_request->recv_vectors);
		allreduce_free_vec_super_reqs_pool(allreduce_super_request);
		return NULL;
	}

	/* Create buffer for clients vectors if there is a need for one */
	if (allreduce_config.num_clients <= 1) {
		allreduce_super_request->clients_recv_vectors = NULL;
	} else if (allreduce_aloc_vec_clients_bufs_pool((void **)&allreduce_super_request->clients_recv_vectors) !=
		   DOCA_SUCCESS) {
		allreduce_free_vec_streams_pool(allreduce_super_request->stream);
		if (allreduce_super_request->recv_vectors != NULL)
			allreduce_free_vec_bufs_pool(allreduce_super_request->recv_vectors);
		allreduce_free_vec_super_reqs_pool(allreduce_super_request);
		return NULL;
	}
#endif

	/* Set default values to the fields of the allreduce super requests */
	STAILQ_INIT(&allreduce_super_request->allreduce_requests_list);
	allreduce_super_request->header = *header;
	allreduce_super_request->num_allreduce_requests = 0;
	/*
	 * Count required send & receive vectors between us and peers (daemons or non-offloaded clients).
	 * Also, count +1 operation for completing operations in case of no peers exist.
	 */
	allreduce_super_request->num_allreduce_operations = 2 * allreduce_config.dest_addresses.num + 1;
	allreduce_super_request->result_vector_size = length;
	allreduce_super_request->recv_vector_iter = 0;
	allreduce_super_request->result_vector = result_vector;
	allreduce_super_request->result_vector_owner = (result_vector == NULL);
	allreduce_super_request->peer_result_vector = NULL;

	return allreduce_super_request;
}

struct allreduce_super_request *
allreduce_super_request_get(const struct allreduce_header *header, size_t result_length, void *result_vector)
{
	struct allreduce_super_request *allreduce_super_request;

	/* Check having allreduce super request in the hash */
	allreduce_super_request = g_hash_table_lookup(allreduce_super_requests_hash, &header->id);
	if (allreduce_super_request == NULL) {
		if (allreduce_config.role == ALLREDUCE_CLIENT)
			DOCA_LOG_DBG("Starting new operation with id %zu, for vector size: %zu", header->id,
				     result_length);
		/* If there is no allreduce super request in the hash, allocate it */
		allreduce_super_request =
			allreduce_super_request_allocate(header, result_length, result_vector);
		if (allreduce_super_request == NULL)
			return NULL;

		/* Insert the allocated allreduce super request to the hash */
		g_hash_table_insert(allreduce_super_requests_hash, &allreduce_super_request->header.id,
							allreduce_super_request);
	}

	return allreduce_super_request;
}

void
allreduce_request_destroy(struct allreduce_request *allreduce_request)
{
	if (allreduce_request->vector != NULL)
		allreduce_free_vec_vecs_pool(allreduce_request->vector);
	allreduce_free_vec_reqs_pool(allreduce_request);
}

struct allreduce_request *
allreduce_request_allocate(struct allreduce_ucx_connection *connection, const struct allreduce_header *header,
			   size_t length)
{
	struct allreduce_request *allreduce_request;

	if (allreduce_aloc_vec_reqs_pool((void **)&allreduce_request) != DOCA_SUCCESS)
		return NULL;

	/* Set default values to the fields of the allreduce request */
	allreduce_request->header = *header;
	if (allreduce_aloc_vec_vecs_pool(&allreduce_request->vector) != DOCA_SUCCESS) {
		allreduce_free_vec_reqs_pool(allreduce_request);
		return NULL;
	}
	allreduce_request->vector_size = length / allreduce_datatype_size[allreduce_config.datatype];
	allreduce_request->connection = connection;
	allreduce_request->allreduce_super_request = NULL;

	return allreduce_request;
}

/*
 * Callback that is called once a client request been completed. It will free the request resources and if it is
 * the final request in the related the super-request, it will also free the super-request
 *
 * @arg [in]: Pointer to a client request
 * @status [in]: The UCX status in which the operation ended with
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
daemon_allreduce_complete_client_operation_callback(void *arg, ucs_status_t status)
{
	(void)status;

	struct allreduce_request *allreduce_request = arg;
	struct allreduce_super_request *allreduce_super_request = allreduce_request->allreduce_super_request;

	/* Sending completion to the client was completed, release the allreduce request */
	allreduce_request_destroy(allreduce_request);

	--allreduce_super_request->num_allreduce_requests;
	if (allreduce_super_request->num_allreduce_requests == 0) {
		/* All allreduce operations were completed, release allreduce super request */
		allreduce_super_request_finish(allreduce_super_request->header.id);
	}
	return DOCA_SUCCESS;
}

/*
 * Callback that is called after a stage in the Allreduce process has ended - scatter or handling a new client request.
 * It will progress the super-request and if it is ready to send, it will scatter the result back to the clients
 *
 * @arg [in]: The related super request
 * @status [in]: The UCX status in which the operation ended with
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: This function frees completed requests and super-requests
 */
static doca_error_t
allreduce_complete_operation_callback(void *arg, ucs_status_t status)
{
	struct allreduce_super_request *allreduce_super_request = arg;
	struct allreduce_request *allreduce_request, *tmp_allreduce_request;

	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to complete an Allreduce scatter/gather. Please check all peers are alive");
		return DOCA_ERROR_IO_FAILED;
	}

	--allreduce_super_request->num_allreduce_operations;
	/* Check if completed receive and send operations per each connection */
	if (allreduce_super_request->num_allreduce_operations > 0) {
		/* Not all allreduce operations among clients or daemons were completed yet */
		return DOCA_SUCCESS;
	}

	/* All allreduce operations among clients or daemons were completed */
	DOCA_DLOG_DBG("Finished 'gathering from peers' stage of request %zu", allreduce_super_request->header.id);

	/* Do operation among the two sub-results elements of the received vector */
	if (allreduce_super_request->peer_result_vector != NULL) {
		allreduce_reduce(allreduce_super_request, allreduce_super_request->peer_result_vector, false);
		allreduce_free_vec_vecs_pool(allreduce_super_request->peer_result_vector);
		allreduce_super_request->peer_result_vector = NULL;
	}

	/* Return all peers vectors to pool besides the result vector */
	allreduce_free_vecs_vecs_pool(allreduce_super_request->recv_vectors, allreduce_super_request->recv_vector_iter);

	if (allreduce_config.role == ALLREDUCE_CLIENT) {
		assert(STAILQ_EMPTY(&allreduce_super_request->allreduce_requests_list));
		/* Allreduce operation is completed for client, because there is no need to send the result to peers */
		--client_active_allreduce_requests;
		allreduce_super_request_finish(allreduce_super_request->header.id);
#ifdef GPU_SUPPORT
		/* Wait for GPU to finish reducing the result before finishing the super request handle */
		cudaError_t result = cudaStreamSynchronize(*allreduce_super_request->stream);

		if (result != cudaSuccess)
			DOCA_LOG_WARN("Failed to sync CUDA stream, results might be wrong. Cuda error %s: %s",
				      cudaGetErrorName(result), cudaGetErrorString(result));
#endif
		return DOCA_SUCCESS;
	}

	/* No need to sync with CUDA before send, UCX uses CUDA default stream */

	/* Go over all requests received from the clients and send the result to them */
	STAILQ_FOREACH_SAFE(allreduce_request, &allreduce_super_request->allreduce_requests_list, entry,
			    tmp_allreduce_request)
	{
		/* A completion is sent only by daemons to clients */
		STAILQ_REMOVE(&allreduce_super_request->allreduce_requests_list, allreduce_request, allreduce_request,
			      entry);
		allreduce_ucx_am_send(allreduce_request->connection, ALLREDUCE_CTRL_AM_ID,
				      &allreduce_super_request->header, sizeof(allreduce_super_request->header),
				      allreduce_super_request->result_vector,
				      allreduce_super_request->result_vector_size *
					      allreduce_datatype_size[allreduce_config.datatype],
				      daemon_allreduce_complete_client_operation_callback, allreduce_request, NULL);
	}
	return DOCA_SUCCESS;
}

void
allreduce_scatter(struct allreduce_super_request *allreduce_super_request)
{
	size_t i;

	/* Post send operations to exchange allreduce vectors among other daemons/clients */
	for (i = 0; i < allreduce_config.dest_addresses.num; ++i)
		allreduce_ucx_am_send(connections[i], ALLREDUCE_OP_AM_ID, &allreduce_super_request->header,
						sizeof(allreduce_super_request->header),
						allreduce_super_request->result_vector,
						allreduce_super_request->result_vector_size *
						allreduce_datatype_size[allreduce_config.datatype],
						allreduce_complete_operation_callback, allreduce_super_request, NULL);

	DOCA_DLOG_DBG("Finished 'scatter' stage for request %zu", allreduce_super_request->header.id);

	/*
	 * Try to complete the operation, it completes if no other daemons or non-offloaded clients exist or sends were
	 * completed immediately
	 */
	allreduce_complete_operation_callback(allreduce_super_request, UCS_OK);
}

/*
 * Callback that is called once receive from client was completed. It will progress the super-request and if all
 * clients vectors were received it will reduce them.
 *
 * @arg [in]: The related super request
 * @status [in]: The UCX status in which the operation ended with
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
_allreduce_gather_callback(void *arg, ucs_status_t status)
{
	struct allreduce_super_request *allreduce_super_request = arg;

	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to complete an Allreduce scatter/gather. Please check all peers are alive");
		return DOCA_ERROR_IO_FAILED;
	}

	/* If there's more then one peer, do operation among the received vectors and peers sub-result vector */
	if (allreduce_super_request->recv_vector_iter + 1 == allreduce_config.dest_addresses.num &&
	    allreduce_super_request->recv_vector_iter > 0)
		allreduce_reduce_all(allreduce_super_request, true);

	return allreduce_complete_operation_callback(allreduce_super_request, status);
}

/*
 * Active Message receive callback which is invoked when the daemon/client receives incoming message from another
 * daemon/client
 *
 * @am_desc [in]: Pointer to a descriptor of the incoming message
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allreduce_gather_callback(struct allreduce_ucx_am_desc *am_desc)
{
	struct allreduce_ucx_connection *connection;
	const struct allreduce_header *allreduce_header;
	struct allreduce_super_request *allreduce_super_request;
	void *vector;
	size_t header_length, length, vector_size;

	allreduce_ucx_am_desc_query(am_desc, &connection, (const void **)&allreduce_header, &header_length, &length);

	assert(sizeof(*allreduce_header) == header_length);
	assert(length % allreduce_datatype_size[allreduce_config.datatype] == 0);

	vector_size = length / allreduce_datatype_size[allreduce_config.datatype];

	/* Either find or allocate the allreduce super request to start doing allreduce operations */
	allreduce_super_request = allreduce_super_request_get(allreduce_header, vector_size, NULL);
	if (allreduce_super_request == NULL) {
		DOCA_LOG_ERR("Abort - failed to allocate a new allreduce_super_request");
		return DOCA_ERROR_NO_MEMORY;
	}
	if (allreduce_aloc_vec_vecs_pool(&vector) != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Abort - failed to allocate a buffer for incoming vector");
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Save vector to the array of receive vectors for further performing allreduce and releasing it then */
	assert(allreduce_super_request->recv_vector_iter < allreduce_config.dest_addresses.num);
	if (allreduce_super_request->peer_result_vector != NULL) {
		allreduce_super_request->recv_vectors[allreduce_super_request->recv_vector_iter] = vector;
		++allreduce_super_request->recv_vector_iter;
	} else
		allreduce_super_request->peer_result_vector = vector;

	/* Continue receiving data to the allocated vector */
	DOCA_DLOG_DBG("Received a vector from a peer");
	return allreduce_ucx_am_recv(am_desc, vector, length, _allreduce_gather_callback, allreduce_super_request, NULL);
}

/*
 * Callback that is called once a handshake send was completed, it will free the buffer and check the status is success
 *
 * @arg [in]: The handshake buffer that was sent
 * @status [in]: The UCX status in which the operation ended with
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allreduce_incoming_handshake_send_callback(void *arg, ucs_status_t status)
{
	free(arg);
	if (status != UCS_OK) {
		DOCA_LOG_ERR("Failed to send handshake message");
		return DOCA_ERROR_IO_FAILED;
	}
	return DOCA_SUCCESS;
}

/*
 * Callback that is called once a receive of an incoming handshake message was completed, it will compare the handshake
 * to the local handshake and act accordingly
 *
 * @arg [in]: The handshake that arrived that was sent
 * @status [in]: The UCX status in which the operation ended with
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allreduce_incoming_handshake_recv_callback(void *arg, ucs_status_t status)
{
	struct allreduce_incoming_handshake_arg *recv_handshake = (struct allreduce_incoming_handshake_arg *)arg;
	struct allreduce_ucx_connection *connection = recv_handshake->connection;
	const uint8_t reply_header = allreduce_config.role;
	char handshake_msg[HANDSHAKE_MAX_MSG_LEN], *handshake_msg_arg;
	int handshake_msg_len;

	if (status != UCS_OK) {
		free(recv_handshake);
		DOCA_LOG_ERR("Failed to receive handshake message");
		return DOCA_ERROR_UNEXPECTED;
	}

	/* Create local handshake message */
	handshake_msg_len =
		snprintf(handshake_msg, HANDSHAKE_MAX_MSG_LEN, HANDSHAKE_MSG_FMT, allreduce_config.vector_size,
			allreduce_datatype_str[allreduce_config.datatype], allreduce_config.batch_size,
			allreduce_config.num_batches);

	if (handshake_msg_len < 0 || HANDSHAKE_MAX_MSG_LEN <= handshake_msg_len) {
		free(recv_handshake);
		DOCA_LOG_ERR("Failed to generate handshake message (snprintf returned %d)", handshake_msg_len);
		return DOCA_ERROR_OPERATING_SYSTEM;
	}

	/* To include '\0' at the buffer which will be sent */
	handshake_msg_len++;

	/* Compare settings */
	if (strcmp(handshake_msg, recv_handshake->msg) == 0) {
		free(recv_handshake);
		return DOCA_SUCCESS;
	}

	/* If the sender is a client and we are a daemon, notify the sender of the mismatch */
	if (recv_handshake->peer_role == ALLREDUCE_CLIENT && allreduce_config.role == ALLREDUCE_DAEMON) {
		handshake_msg_arg = malloc(handshake_msg_len * sizeof(*handshake_msg_arg));
		if (handshake_msg_arg == NULL) {
			free(recv_handshake);
			DOCA_LOG_ERR("Failed to allocate buffer to keep remote handshake message");
			return DOCA_ERROR_NO_MEMORY;
		}

		strlcpy(handshake_msg_arg, handshake_msg, handshake_msg_len);
		allreduce_ucx_am_send(connection, ALLREDUCE_HANDSHAKE_AM_ID, &reply_header, 1, handshake_msg_arg,
				      handshake_msg_len, allreduce_incoming_handshake_send_callback, handshake_msg_arg,
				      NULL);
	}

	/* Warn user */
	DOCA_LOG_ERR("Configuration mismatch. Us: \"%s\", Other: \"%s\"", handshake_msg,
			recv_handshake->msg);
	if (allreduce_config.role == ALLREDUCE_CLIENT && allreduce_config.allreduce_mode == ALLREDUCE_OFFLOAD_MODE) {
		free(recv_handshake);
		DOCA_LOG_ERR("Configuration differs from daemon");
		return DOCA_ERROR_BAD_STATE;
	}

	DOCA_LOG_ERR("Please rerun one of the daemons/clients with matching parameters or allreduce might crash");
	DOCA_LOG_INFO(
		"Daemons and non-offloaded clients cannot guess the correct settings - No terminations is performed");

	free(recv_handshake);
	return DOCA_SUCCESS;
}

/*
 * Active Message receive callback which is invoked when the daemon/client receives incoming handshake message from
 * another daemon/client
 *
 * @am_desc [in]: Pointer to a descriptor of the incoming message
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allreduce_incoming_handshake_callback(struct allreduce_ucx_am_desc *am_desc)
{
	struct allreduce_incoming_handshake_arg *handshake_arg;
	struct allreduce_ucx_connection *connection;
	const uint8_t *recv_header;
	size_t header_length, length;

	allreduce_ucx_am_desc_query(am_desc, &connection, (const void **)&recv_header, &header_length, &length);

	handshake_arg = malloc(sizeof(*handshake_arg));
	if (handshake_arg == NULL) {
		DOCA_LOG_ERR("Failed to allocate buffer to keep remote handshake message");
		return DOCA_ERROR_NO_MEMORY;
	}

	handshake_arg->connection = connection;
	handshake_arg->peer_role = *recv_header;
	handshake_arg->length = length;

	/* Get remote handshake message */
	return allreduce_ucx_am_recv(am_desc, handshake_arg->msg, length, allreduce_incoming_handshake_recv_callback,
				       handshake_arg, NULL);
}

/*
 * Sends the local handshake message to the daemon or all the peers
 *
 * @num_connections [in]: Number of peers/daemon
 * @return: 0 on success and negative value otherwise
 */
static int
allreduce_outgoing_handshake(int num_connections)
{
	int i;
	doca_error_t result;
	char handshake_msg[HANDSHAKE_MAX_MSG_LEN];
	struct allreduce_ucx_request *request_p;
	uint8_t header = allreduce_config.role;
	int handshake_msg_len =
		snprintf(handshake_msg, HANDSHAKE_MAX_MSG_LEN, HANDSHAKE_MSG_FMT, allreduce_config.vector_size,
			allreduce_datatype_str[allreduce_config.datatype], allreduce_config.batch_size,
			allreduce_config.num_batches);

	if (handshake_msg_len < 0 || HANDSHAKE_MAX_MSG_LEN <= handshake_msg_len)
		return -1;

	/* To include '\0' at the buffer which will be sent */
	handshake_msg_len++;

	for (i = 0; i < num_connections; ++i) {
		result = allreduce_ucx_am_send(connections[i], ALLREDUCE_HANDSHAKE_AM_ID, &header, 1, handshake_msg,
					    handshake_msg_len, NULL, NULL, &request_p);
		if (result != DOCA_SUCCESS && allreduce_ucx_request_wait(result, request_p) < 0)
			return -1;
	}

	return 0;
}

/*
 * Cleanup communication resources
 *
 * @num_connections [in]: Number of peers/daemon
 */
static void
communication_destroy(int num_connections)
{
	/* Destroy connections to other clients or daemon in case of client or to other daemons in case of daemon */
	connections_cleanup(num_connections);
	g_hash_table_destroy(allreduce_super_requests_hash);
}

/*
 * Configure the active network resources such as establish connections to peers/daemon,
 * performing handshake with connections, creating cache for super requests and more.
 *
 * @nb_connections [out]: The number of established connections
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
communication_init(int *nb_connections)
{
	int result;
	int num_connections;

	/* Allocate hash of allreduce requests to hold submitted operations */
	allreduce_super_requests_hash = g_hash_table_new_full(g_size_t_hash, g_size_t_equal, NULL,
								allreduce_super_request_destroy_callback);
	if (allreduce_super_requests_hash == NULL)
		return DOCA_ERROR_NO_MEMORY;

	/* Set handshake message receive handler before starting to accept any connections */
	allreduce_ucx_am_set_recv_handler(context, ALLREDUCE_HANDSHAKE_AM_ID, allreduce_incoming_handshake_callback);

	if (allreduce_config.role == ALLREDUCE_DAEMON ||
	    allreduce_config.allreduce_mode == ALLREDUCE_NON_OFFLOADED_MODE) {
		/*
		 * Setup receive handler for Active message messages from daemons or non-offloaded clients
		 * which carry allreduce data to do allreduce for
		 */
		allreduce_ucx_am_set_recv_handler(context, ALLREDUCE_OP_AM_ID, allreduce_gather_callback);

		/* Setup the listener to accept incoming connections from clients/daemons */
		result = allreduce_ucx_listen(context, allreduce_config.listen_port);
		if (result < 0) {
			g_hash_table_destroy(allreduce_super_requests_hash);
			return DOCA_ERROR_INITIALIZATION;
		}
	}

	/* Initialize connections to other clients or daemon in case of client or to other daemons in case of daemon */
	num_connections = connections_init();
	if (num_connections < 0)
		return DOCA_ERROR_INITIALIZATION;
	*nb_connections = num_connections;

	/* Send handshake message to peers */
	result = allreduce_outgoing_handshake(num_connections);
	if (result < 0) {
		DOCA_LOG_ERR("Failed to perform handshake");
		communication_destroy(num_connections);
		return DOCA_ERROR_INITIALIZATION;
	}

	return DOCA_SUCCESS;
}

/*
 * Cleanup the destination addresses memory
 */
static void
dest_address_cleanup(void)
{
	struct allreduce_address *address, *tmp_address;

	/* Go through all addresses saved in the configuration and free the memory allocated to hold them */
	STAILQ_FOREACH_SAFE(address, &allreduce_config.dest_addresses.list, entry, tmp_address) {
		free(address);
	}
}

/*
 * Converts "allreduce_config.dest_addresses.str" to "allreduce_config.dest_addresses.list"
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
dest_addresses_init(void)
{
	char *dest_addresses_str = allreduce_config.dest_addresses.str;
	const char *port_separator;
	char *str;
	size_t ip_addr_length;
	struct allreduce_address *address;

	allreduce_config.dest_addresses.str = NULL;
	allreduce_config.dest_addresses.num = 0;
	STAILQ_INIT(&allreduce_config.dest_addresses.list);

	/* Go over comma-separated list of <IP-address>:[<port>] elements */
	str = strtok(dest_addresses_str, ",");
	while (str != NULL) {
		address = malloc(sizeof(*address));
		if (address == NULL) {
			DOCA_LOG_ERR("Failed to allocate memory to hold address");
			dest_address_cleanup();
			free(dest_addresses_str);
			return DOCA_ERROR_NO_MEMORY;
		}

		/* Parse an element of comma-separated list and insert to the list of peer's addresses */
		port_separator = strchr(str, ':');
		if (port_separator == NULL) {
			/* Port wasn't specified - take port number from -p argument */
			address->port = allreduce_config.dest_port;
			strncpy(address->ip_address_str, str, sizeof(address->ip_address_str) - 1);
			address->ip_address_str[sizeof(address->ip_address_str) - 1] = '\0';
		} else {
			/* Port was specified - take port number from the string of the address */
			address->port = atoi(port_separator + 1);
			ip_addr_length = port_separator - str;
			memcpy(address->ip_address_str, str, ip_addr_length);
			address->ip_address_str[ip_addr_length] = '\0';
		}

		++allreduce_config.dest_addresses.num;
		STAILQ_INSERT_TAIL(&allreduce_config.dest_addresses.list, address, entry);

		str = strtok(NULL, ",");
	}

	free(dest_addresses_str);
	return DOCA_SUCCESS;
}

#ifdef GPU_SUPPORT

/*
 * Creates a new CUDA stream and initialize it
 *
 * @return: The allocate CUDA stream
 */
static void *
stream_gen(void)
{
	cudaStream_t *st = (cudaStream_t *)malloc(sizeof(cudaStream_t));
	cudaError_t result;

	if (st == NULL) {
		DOCA_LOG_ERR("Memory allocation failed");
		return NULL;
	}

	result = cudaStreamCreate(st);
	if (result != cudaSuccess) {
		free(st);
		DOCA_LOG_ERR("Failed to create a cuda stream for new allreduce request, Cuda msg: %s",
			     cudaGetErrorString(result));
		return NULL;
	}

	return st;
}

/*
 * Destroys a CUDA stream
 *
 * @stream_p [in]: Existing CUDA stream
 */
static void
stream_desc(void *stream_p)
{
	cudaStream_t *st = (cudaStream_t *)stream_p;

	cudaStreamDestroy(*st);
	free(st);
}
#endif

/*
 * Frees all memory pools
 */
static void
allreduce_destroy_mempools(void)
{
	allreduce_destroy_super_reqs_pool();
	if (allreduce_config.role == ALLREDUCE_DAEMON || allreduce_config.allreduce_mode == ALLREDUCE_NON_OFFLOADED_MODE) {
		allreduce_destroy_vecs_pool();
		/* If the pool was even allocated */
		if (allreduce_config.dest_addresses.num > 1)
			allreduce_destroy_bufs_pool();
		if (allreduce_config.role == ALLREDUCE_DAEMON)
			allreduce_destroy_reqs_pool();
#ifdef GPU_SUPPORT
		allreduce_destroy_streams_pool();
		if (allreduce_config.num_clients > 1)
			allreduce_destroy_clients_bufs_pool();
#endif
	}
}

/*
 * Initialize all memory pools
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
allreduce_init_mempools(void)
{
	doca_error_t result;
#ifdef GPU_SUPPORT
	mem_type vecs_mtype = CUDA;  /* Using CUDA Managed memory for the vectors breaks the implicit sync in
				      * GPU Direct and creates a need to use cudaStreamSynchronize() before sends
				      */
	mem_type buffs_mtype = CUDA_MANAGED;
#else
	mem_type vecs_mtype = CPU;
	mem_type buffs_mtype = CPU;
#endif

	/* Generate memory pools
	 *	vecs_pool size - start with 2 preallocated vectors per request
	 *	bufs_pool ("recv_vectors" pool) size - start with 2 preallocated buffers for at most 2 batches at once
	 *	reqs_pool size - are freed last, so start with enough for whole batches
	 *	super_reqs_pool size - start with enough for a 2 batches
	 *	streams_pool size - start with enough for a 2 batches
	 *	clients_bufs_pool ("clients_recv_vectors" pool) size - start with 2 preallocated buffers for at
	 *								most 2 batches at once
	 * We need enough resources to take care on 2 batches at once since this is the maximum amount that can be held
	 * at once. One batch is not enough since diverges in the clients/peers speed can effect releasing of resources
	 */
	result = allreduce_create_super_reqs_pool(2 * allreduce_config.batch_size,
						  sizeof(struct allreduce_super_request), CPU);
	/* The other pools are only needed by non-offloaded clients and daemons */
	if (allreduce_config.role == ALLREDUCE_CLIENT && allreduce_config.allreduce_mode == ALLREDUCE_OFFLOAD_MODE)
		return result;

	result |= allreduce_create_vecs_pool(
		2 * (allreduce_config.num_clients + allreduce_config.dest_addresses.num) * allreduce_config.batch_size,
		allreduce_config.vector_size * allreduce_datatype_size[allreduce_config.datatype], vecs_mtype);
	/*
	 * First received vector from a peer will be the peers_result_vector, if there are more peers we need
	 * a buffer to hold other peers vectors. If there is only <=1 peers, the pool isn't initialized.
	 */
	if (allreduce_config.dest_addresses.num > 1) {
		result |= allreduce_create_bufs_pool(2 * allreduce_config.batch_size,
						     allreduce_config.dest_addresses.num *
							  sizeof(*((struct allreduce_super_request *)0)->recv_vectors),
						     buffs_mtype);
	}
	if (allreduce_config.role == ALLREDUCE_DAEMON) {
		result |= allreduce_create_reqs_pool(2 * allreduce_config.batch_size * allreduce_config.num_clients,
						     sizeof(struct allreduce_request), CPU);
	}
#ifdef GPU_SUPPORT
	result |= allreduce_create_streams_pool(2 * allreduce_config.batch_size, stream_gen, stream_desc);

	/* If the there is less than 2 clients, no need for this pool */
	if (allreduce_config.num_clients > 1) {
		result |= allreduce_create_clients_bufs_pool(
			2 * allreduce_config.batch_size,
			(allreduce_config.num_clients - 1) *
				sizeof(*((struct allreduce_super_request *)0)->clients_recv_vectors),
			CUDA_MANAGED);
	}
#endif

	/* Destroys the allocated pools and do nothing for the empty ones */
	if (result != DOCA_SUCCESS)
		allreduce_destroy_mempools();

	return result;
}

doca_error_t
allreduce_init(int *num_connections)
{
	doca_error_t result;

#ifdef GPU_SUPPORT
	/* CUDA GPU init */
	if (allreduce_config.role == ALLREDUCE_DAEMON || allreduce_config.allreduce_mode == ALLREDUCE_NON_OFFLOADED_MODE)
		set_cuda_globals();
#endif

	/* Initialize destination addresses specified by a user */
	result = dest_addresses_init();
	if (result != DOCA_SUCCESS)
		return result;

	result = allreduce_init_mempools();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate sufficient memory to run");
		dest_address_cleanup();
		return DOCA_ERROR_INITIALIZATION;
	}
	DOCA_LOG_INFO("Successfully allocated all memory pools");

	/* Create context */
	result = allreduce_ucx_init(ALLREDUCE_MAX_AM_ID, &context);
	if (result != DOCA_SUCCESS) {
		allreduce_destroy_mempools();
		dest_address_cleanup();
		return result;
	}

	/* Create communication-related stuff */
	return communication_init(num_connections);
}

void
allreduce_destroy(int num_connection)
{
	/* Destroy communication-related stuff */
	communication_destroy(num_connection);
	/* Destroy UCX context */
	allreduce_ucx_destroy(context);
	/* Destroy memory pools */
	allreduce_destroy_mempools();
	/* Destroy destination addresses */
	dest_address_cleanup();
}
