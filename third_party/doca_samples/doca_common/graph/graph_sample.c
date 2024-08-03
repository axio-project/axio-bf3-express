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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <doca_mmap.h>
#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_dma.h>
#include <doca_sha.h>
#include <doca_graph.h>
#include <doca_types.h>
#include <doca_log.h>

#include <samples/common.h>

DOCA_LOG_REGISTER(GRAPH::SAMPLE);

/**
 * This sample creates the following graph:
 *
 *         +-----+             +-----+
 *         | SHA |             | DMA |
 *         +--+--+             +-----+
 *            |                   |
 *            +---------+---------+
 *                      |
 *                +-----------+
 *                | User Node |
 *                +-----------+
 *
 * The user node checks that DMA source and destination are the same and prints sha VALUE
 *
 * The sample creates 10 graph instances, queues them to the work queue and waits until all instances are completed.
 * The sample uses work queue in polling mode for the sake of simplicity.
 */

/**
 * This macro is used to minimize code size.
 * The macro runs an expression and returns error if the expression status is not DOCA_SUCCESS
 */
#define EXIT_ON_FAILURE(_expression_) { \
	doca_error_t _status_ = _expression_; \
	\
	if (_status_ != DOCA_SUCCESS) { \
		DOCA_LOG_ERR("%s failed with status %s", __func__, doca_get_error_string(_status_)); \
		return _status_; \
	} \
}

#define NUM_GRAPH_INSTNACES		10

#define SOURCE_BUFFER_SIZE		1024
#define DMA_DEST_BUFFER_SIZE		SOURCE_BUFFER_SIZE
#define SHA_DEST_BUFFER_SIZE		DOCA_SHA256_BYTE_COUNT

#define REQUIRED_ENTRY_SIZE		(SOURCE_BUFFER_SIZE + DMA_DEST_BUFFER_SIZE + SHA_DEST_BUFFER_SIZE)

#define BUFFER_SIZE			(REQUIRED_ENTRY_SIZE * NUM_GRAPH_INSTNACES)

/* Buffers are: source, DMA destination, SHA value */
#define GRAPH_INSTNACE_NUM_BUFFERS	(3)
#define BUF_INVENTORY_SIZE		(GRAPH_INSTNACE_NUM_BUFFERS * NUM_GRAPH_INSTNACES)

/* Context jobs are: DMA, SHA */
#define NUM_CTX_JOBS_PER_GRAPH_INSTNACE 2

/**
 * Work queue depth should be >= (num graph context nodes + 1 for the graph) * num graph instances.
 * The depth should be larger if the work queue serves stand alone jobs or other graphs.
 */
#define WORK_QUEUE_DEPTH ((NUM_CTX_JOBS_PER_GRAPH_INSTNACE + 1) * NUM_GRAPH_INSTNACES)

/**
 * The sample uses an array of doca_ctx. This enum defines the nodes in the array.
 */
enum sample_contexts {
	SAMPLE_CONTEXT_SHA,
	SAMPLE_CONTEXT_DMA,
	NUM_SAMPLE_CONTEXTS /* MUST BE LAST */
};

/**
 * It is recommended to put the graph instance data in a struct.
 * Notice that graph jobs life span must be >= life span of the graph instance.
 */
struct graph_instance_data {
	uint32_t index; /* Index is used for printing */
	struct doca_graph_instance *graph_instance;
	struct doca_buf *source;
	uint8_t *source_addr;

	struct doca_sha_job sha_job;
	struct doca_event sha_job_event;
	struct doca_buf *sha_dest;
	uint8_t *sha_dest_addr;

	struct doca_dma_job_memcpy dma_job;
	struct doca_event dma_job_event;
	struct doca_buf *dma_dest;
	uint8_t *dma_dest_addr;

	struct doca_event user_node_event;
};

/**
 * This struct defines the application context.
 */
struct graph_sample_state {
	/**
	 * Resources
	 */
	struct doca_dev *device;
	struct doca_mmap *mmap;
	struct doca_buf_inventory *inventory;
	struct doca_workq *work_queue;
	struct doca_ctx *contexts[NUM_SAMPLE_CONTEXTS];
	struct doca_sha *sha;
	struct doca_dma *dma;

	/**
	 * Buffer
	 * This buffer is used for the source and destination.
	 * Real life scenario may use more memory areas.
	 */
	uint8_t *buffer;
	uint8_t *available_buffer; /* Points to the available location in the buffer, used during initialization */

	/**
	 * Graph
	 * This section holds the graph and nodes.
	 * The nodes are used during instance creation and maintenance.
	 */
	struct doca_graph *graph;
	struct doca_graph_node *sha_node;
	struct doca_graph_node *dma_node;
	struct doca_graph_node *user_node;

	/* Array of graph instances. All will be submitted to the work queue at once */
	struct graph_instance_data instances[NUM_GRAPH_INSTNACES];
};

/**
 * Allocates a buffer that will be used for the source and destination buffers.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
allocate_buffer(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Allocating buffer");

	state->buffer = (uint8_t *)malloc(BUFFER_SIZE);
	if (state->buffer == NULL)
		return DOCA_ERROR_NO_MEMORY;

	state->available_buffer = state->buffer;

	return DOCA_SUCCESS;
}

/*
 * Check if DOCA device is DMA and SHA capable
 *
 * @devinfo [in]: Device to check
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
check_dev_dma_sha_capable(struct doca_devinfo *devinfo)
{
	doca_error_t status = doca_sha_job_get_supported(devinfo, DOCA_SHA_JOB_SHA256);

	if (status != DOCA_SUCCESS)
		return status;

	status = doca_dma_job_get_supported(devinfo, DOCA_DMA_JOB_MEMCPY);
	if (status != DOCA_SUCCESS)
		return status;

	return DOCA_SUCCESS;
}

/**
 * Opens a device that supports SHA and DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
open_device(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Opening device");

	EXIT_ON_FAILURE(open_doca_device_with_capabilities(check_dev_dma_sha_capable, &state->device));

	return DOCA_SUCCESS;
}

/**
 * Creates a work queue
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_work_queue(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating work queue");

	EXIT_ON_FAILURE(doca_workq_create(WORK_QUEUE_DEPTH, &state->work_queue));
	/* The sample focuses on DOCA graph, so it will use polling mode for simplicity */
	EXIT_ON_FAILURE(doca_workq_set_event_driven_enable(state->work_queue, false));

	return DOCA_SUCCESS;
}

/**
 * Create MMAP, initialize and start it.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_mmap(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating MMAP");

	EXIT_ON_FAILURE(doca_mmap_create(NULL, &state->mmap));
	EXIT_ON_FAILURE(doca_mmap_set_memrange(state->mmap, state->buffer, BUFFER_SIZE));
	EXIT_ON_FAILURE(doca_mmap_dev_add(state->mmap, state->device));
	EXIT_ON_FAILURE(doca_mmap_set_permissions(state->mmap, DOCA_ACCESS_LOCAL_READ_WRITE));
	EXIT_ON_FAILURE(doca_mmap_start(state->mmap));

	return DOCA_SUCCESS;
}

/**
 * Create buffer inventory, initialize and start it.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_buf_inventory(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating buf inventory");

	EXIT_ON_FAILURE(
		doca_buf_inventory_create(NULL, BUF_INVENTORY_SIZE, DOCA_BUF_EXTENSION_NONE, &state->inventory));
	EXIT_ON_FAILURE(doca_buf_inventory_start(state->inventory));

	return DOCA_SUCCESS;
}

/**
 * Create SHA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_sha(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating SHA");

	EXIT_ON_FAILURE(doca_sha_create(&state->sha));
	state->contexts[SAMPLE_CONTEXT_SHA] = doca_sha_as_ctx(state->sha);
	return DOCA_SUCCESS;
}

/**
 * Create DMA
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_dma(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating DMA");

	EXIT_ON_FAILURE(doca_dma_create(&state->dma));
	state->contexts[SAMPLE_CONTEXT_DMA] = doca_dma_as_ctx(state->dma);
	return DOCA_SUCCESS;
}

/**
 * Start contexts
 * The method adds the device to the contexts, starts them and add them to the work queue.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
start_contexts(struct graph_sample_state *state)
{
	uint32_t i = 0;

	DOCA_LOG_INFO("Starting contexts");

	for (i = 0; i < NUM_SAMPLE_CONTEXTS; i++) {
		EXIT_ON_FAILURE(doca_ctx_dev_add(state->contexts[i], state->device));
		EXIT_ON_FAILURE(doca_ctx_start(state->contexts[i]));
		EXIT_ON_FAILURE(doca_ctx_workq_add(state->contexts[i], state->work_queue));
	}

	return DOCA_SUCCESS;
}

/**
 * Stop contexts
 * The method removes the contexts from the work queue, stops them and removes the device from them.
 *
 * @state [in]: sample state
 */
void
stop_contexts(struct graph_sample_state *state)
{
	uint32_t i = 0;

	for (i = 0; i < NUM_SAMPLE_CONTEXTS; i++) {
		if (state->contexts[i] != NULL) {
			doca_ctx_workq_rm(state->contexts[i], state->work_queue);
			doca_ctx_stop(state->contexts[i]);
			doca_ctx_dev_rm(state->contexts[i], state->device);
		}
	}
}

/**
 * User node callback
 * This callback is called when the graph user node is executed.
 * The method compares the source and DMA destination and prints the SHA value.
 *
 * @cookie [in]: callback cookie
 * @ev [in]: event to fill
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
user_node_callback(void *cookie, struct doca_event *ev)
{
	uint32_t i = 0;

	(void)(ev);

	struct graph_instance_data *instance = (struct graph_instance_data *)cookie;
	size_t dma_length = 0;
	char sha_output[DOCA_SHA256_BYTE_COUNT * 2 + 1] = {0};

	DOCA_LOG_INFO("Instance %d user callback", instance->index);

	EXIT_ON_FAILURE(doca_buf_get_data_len(instance->dma_dest, &dma_length));

	if (dma_length != DMA_DEST_BUFFER_SIZE) {
		DOCA_LOG_ERR("DMA destination buffer length %zu should be %d", dma_length, DMA_DEST_BUFFER_SIZE);
		return DOCA_ERROR_BAD_STATE;
	}

	if (memcmp(instance->dma_dest_addr, instance->source_addr, dma_length) != 0) {
		DOCA_LOG_ERR("DMA source and destination mismatch");
		return DOCA_ERROR_BAD_STATE;
	}

	for (i = 0; i < DOCA_SHA256_BYTE_COUNT; i++)
		snprintf(sha_output + (2 * i), 3, "%02x", instance->sha_dest_addr[i]);

	DOCA_LOG_INFO("SHA Value: %s", sha_output);

	return DOCA_SUCCESS;
}

/**
 * This method creates the graph.
 * Creating a node adds it to the graph roots.
 * Adding dependency removes a dependent node from the graph roots.
 * The method creates all nodes and then adds the dependency out of convenience. Adding dependency during node creation
 * is supported.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_graph(struct graph_sample_state *state)
{
	DOCA_LOG_INFO("Creating graph");
	EXIT_ON_FAILURE(doca_graph_create(&state->graph));

	/* Creating nodes */
	EXIT_ON_FAILURE(doca_graph_ctx_node_create(state->graph, DOCA_SHA_JOB_SHA256,
						   state->contexts[SAMPLE_CONTEXT_SHA], &state->sha_node));

	EXIT_ON_FAILURE(doca_graph_ctx_node_create(state->graph, DOCA_DMA_JOB_MEMCPY,
						   state->contexts[SAMPLE_CONTEXT_DMA], &state->dma_node));

	EXIT_ON_FAILURE(doca_graph_user_node_create(state->graph, user_node_callback, &state->user_node));

	/* Setting dependencies (building the graph) */
	EXIT_ON_FAILURE(doca_graph_add_dependency(state->graph, state->dma_node, state->user_node));
	EXIT_ON_FAILURE(doca_graph_add_dependency(state->graph, state->sha_node, state->user_node));

	/* Graph must be started before it is added to the work queue. The graph is validated during this call */
	EXIT_ON_FAILURE(doca_graph_start(state->graph));

	EXIT_ON_FAILURE(doca_graph_workq_add(state->graph, state->work_queue));

	return DOCA_SUCCESS;
}

/**
 * Destroy the graph
 *
 * @state [in]: sample state
 */
void
destroy_graph(struct graph_sample_state *state)
{
	if (state->graph == NULL)
		return;

	doca_graph_workq_rm(state->graph, state->work_queue);
	doca_graph_stop(state->graph);
	doca_graph_destroy(state->graph);
}

/**
 * This method creates a graph instance
 * Graph instance creation usually includes initializing the data for the nodes (e.g. initializing jobs).
 *
 * @state [in]: sample state
 * @index [in]: the graph instance index
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_graph_instance(struct graph_sample_state *state, uint32_t index)
{
	struct graph_instance_data *instance = &state->instances[index];

	instance->index = index;

	EXIT_ON_FAILURE(doca_graph_instance_create(state->graph, &instance->graph_instance));

	/* Use doca_buf_inventory_buf_by_data to initialize the source buffer */
	EXIT_ON_FAILURE(doca_buf_inventory_buf_by_data(state->inventory, state->mmap, state->available_buffer,
						       SOURCE_BUFFER_SIZE, &instance->source));
	memset(state->available_buffer, (index + 1), SOURCE_BUFFER_SIZE);
	instance->source_addr = state->available_buffer;
	state->available_buffer += SOURCE_BUFFER_SIZE;

	/* Initialize DMA job */
	EXIT_ON_FAILURE(doca_buf_inventory_buf_by_addr(state->inventory, state->mmap, state->available_buffer,
						       DMA_DEST_BUFFER_SIZE, &instance->dma_dest));
	instance->dma_dest_addr = state->available_buffer;
	state->available_buffer += DMA_DEST_BUFFER_SIZE;

	instance->dma_job.base.ctx = state->contexts[SAMPLE_CONTEXT_DMA];
	instance->dma_job.base.type = DOCA_DMA_JOB_MEMCPY;
	instance->dma_job.base.flags = DOCA_JOB_FLAGS_NONE;
	/* This sample does not use the job user data, but it is supported */
	instance->dma_job.base.user_data.ptr = NULL;
	instance->dma_job.src_buff = instance->source;
	instance->dma_job.dst_buff = instance->dma_dest;
	EXIT_ON_FAILURE(doca_graph_instance_set_ctx_node_data(instance->graph_instance, state->dma_node,
							      &instance->dma_job.base,
							      &instance->dma_job_event));

	/* Initialize SHA job*/
	EXIT_ON_FAILURE(doca_buf_inventory_buf_by_addr(state->inventory, state->mmap, state->available_buffer,
						       SHA_DEST_BUFFER_SIZE, &instance->sha_dest));
	instance->sha_dest_addr = state->available_buffer;
	state->available_buffer += SHA_DEST_BUFFER_SIZE;

	instance->sha_job.base.ctx = state->contexts[SAMPLE_CONTEXT_SHA];
	instance->sha_job.base.type = DOCA_SHA_JOB_SHA256;
	instance->sha_job.base.flags = DOCA_JOB_FLAGS_NONE;
	/** This sample does not use the job user data, but it is supported */
	instance->sha_job.base.user_data.ptr = NULL;
	instance->sha_job.req_buf = instance->source;
	instance->sha_job.resp_buf = instance->sha_dest;
	EXIT_ON_FAILURE(doca_graph_instance_set_ctx_node_data(instance->graph_instance, state->sha_node,
							      &instance->sha_job.base, &instance->sha_job_event));

	/* Initialize user callback */
	/* The sample does not use the node event, but it is supported */
	/* The sample uses the instance as a cookie. From there it can get all the information it needs */
	EXIT_ON_FAILURE(doca_graph_instance_set_user_node_data(instance->graph_instance, state->user_node, instance,
							       &instance->user_node_event));

	return DOCA_SUCCESS;
}

/**
 * Destroy graph instance
 *
 * @state [in]: sample state
 * @index [in]: the graph instance index
 */
void
destroy_graph_instance(struct graph_sample_state *state, uint32_t index)
{
	struct graph_instance_data *instance = &state->instances[index];

	if (instance->graph_instance != NULL)
		doca_graph_instance_destroy(instance->graph_instance);

	if (instance->source != NULL)
		doca_buf_refcount_rm(instance->source, NULL);
	if (instance->dma_dest != NULL)
		doca_buf_refcount_rm(instance->dma_dest, NULL);
	if (instance->sha_dest != NULL)
		doca_buf_refcount_rm(instance->sha_dest, NULL);
}

/**
 * Create graph instances
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
create_graph_instances(struct graph_sample_state *state)
{
	uint32_t i = 0;

	DOCA_LOG_INFO("Creating graph instances");

	for (i = 0; i < NUM_GRAPH_INSTNACES; i++)
		EXIT_ON_FAILURE(create_graph_instance(state, i));

	return DOCA_SUCCESS;
}

/**
 * Destroy graph instances
 *
 * @state [in]: sample state
 */
void
destroy_graph_instances(struct graph_sample_state *state)
{
	uint32_t i = 0;

	for (i = 0; i < NUM_GRAPH_INSTNACES; i++)
		destroy_graph_instance(state, i);
}

/**
 * This method cleans up the sample resources in reverse order of their creation.
 * This method does not check for destroy return values for simplify.
 * Real code should check the return value and act accordingly (e.g. if doca_workq_destroy failed it means that some
 * contexts are still added or even that there are still in flight jobs in the work queue).
 *
 * @state [in]: sample state
 */
void
cleanup(struct graph_sample_state *state)
{
	destroy_graph_instances(state);

	destroy_graph(state);

	stop_contexts(state);

	if (state->work_queue != NULL)
		doca_workq_destroy(state->work_queue);

	if (state->sha != NULL)
		doca_sha_destroy(state->sha);

	if (state->dma != NULL)
		doca_dma_destroy(state->dma);

	if (state->inventory != NULL) {
		doca_buf_inventory_stop(state->inventory);
		doca_buf_inventory_destroy(state->inventory);
	}

	if (state->mmap != NULL) {
		doca_mmap_stop(state->mmap);
		doca_mmap_destroy(state->mmap);
	}

	if (state->device != NULL)
		doca_dev_close(state->device);

	if (state->buffer != NULL)
		free(state->buffer);
}

/**
 * Submit graph instances
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
submit_instances(struct graph_sample_state *state)
{
	uint32_t i = 0;

	DOCA_LOG_INFO("Submitting all graph instances");

	for (i = 0; i < NUM_GRAPH_INSTNACES; i++) {
		union doca_data data;

		data.ptr = &state->instances[i];
		EXIT_ON_FAILURE(doca_workq_graph_submit(state->work_queue, state->instances[i].graph_instance, data));
	}

	return DOCA_SUCCESS;
}

/**
 * Poll the work queue until all instances are completed
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
poll_for_completion(struct graph_sample_state *state)
{
	uint32_t num_completed_instances = 0;
	doca_error_t status = DOCA_SUCCESS;

	DOCA_LOG_INFO("Waiting until all instances are complete");

	while (num_completed_instances < NUM_GRAPH_INSTNACES) {
		struct doca_event ev = {0};
		struct graph_instance_data *instance = NULL;

		while ((status = doca_workq_progress_retrieve(state->work_queue, &ev,
							      DOCA_WORKQ_RETRIEVE_FLAGS_NONE)) == DOCA_ERROR_AGAIN) {
			/* Sleep here. Notice that again is expected for intermediate graph jobs */
		}
		if (status != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Progress retrieve failed with status %s", doca_get_error_string(status));
			return status;
		}

		num_completed_instances++;

		instance = (struct graph_instance_data *)ev.user_data.ptr;
		DOCA_LOG_INFO("Instance %d completed", instance->index);
	}

	DOCA_LOG_INFO("All instances completed successfully");

	return DOCA_SUCCESS;
}

/**
 * Run the sample
 * The method (and the method it calls) does not cleanup anything in case of failures.
 * It assumes that cleanup is called after it at any case.
 *
 * @state [in]: sample state
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
run(struct graph_sample_state *state)
{
	EXIT_ON_FAILURE(allocate_buffer(state));
	EXIT_ON_FAILURE(open_device(state));
	EXIT_ON_FAILURE(create_mmap(state));
	EXIT_ON_FAILURE(create_buf_inventory(state));
	EXIT_ON_FAILURE(create_work_queue(state));
	EXIT_ON_FAILURE(create_sha(state));
	EXIT_ON_FAILURE(create_dma(state));
	EXIT_ON_FAILURE(start_contexts(state));
	EXIT_ON_FAILURE(create_graph(state));
	EXIT_ON_FAILURE(create_graph_instances(state));
	EXIT_ON_FAILURE(submit_instances(state));
	EXIT_ON_FAILURE(poll_for_completion(state));

	return DOCA_SUCCESS;
}

/**
 * Run the graph sample
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t
run_graph_sample(void)
{
	struct graph_sample_state state = {0};
	doca_error_t status = run(&state);

	cleanup(&state);

	return status;
}
