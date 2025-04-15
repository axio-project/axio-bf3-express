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

#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include <doca_ctx.h>
#include <doca_pe.h>
#include <doca_log.h>
#include <doca_urom.h>

#include <worker_graph.h>

#include "common.h"
#include "urom_common.h"

DOCA_LOG_REGISTER(UROM_MULTI_WORKERS_BOOTS::SAMPLE);

static int nb_exit_workers;    /* Number of exited workers */
static int nb_running_workers; /* Number of running workers */
static pthread_mutex_t mutex;  /* Mutex to sync between the workers threads */
static bool worker_force_quit; /* Flag for forcing Workers to exit and terminate the sample */

/* Worker context per thread */
struct worker_ctx {
	uint32_t gid;			   /* UROM worker group id */
	uint64_t worker_id;		   /* UROM worker id to create */
	uint64_t plugins;		   /* UROM worker plugins */
	struct doca_urom_service *service; /* UROM service context */
	doca_error_t *exit_status;	   /* Worker exit status */
};

/**
 * loopback task result structure
 */
struct loopback_result {
	uint64_t data;	     /* Loopback data */
	doca_error_t result; /* Worker task result */
};

/*
 * Signal handler
 *
 * @signum [in]: Signal number to handle
 */
static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		worker_force_quit = true;
	}
}

/*
 * Graph loopback task callback function
 *
 * @result [in]: task result
 * @cookie [in]: worker cookie
 * @data [in]: loopback data
 */
static void worker_graph_loopback_finished_cb(doca_error_t result, union doca_data cookie, uint64_t data)
{
	struct loopback_result *ret = cookie.ptr;

	if (ret == NULL)
		return;
	ret->data = data;
	ret->result = result;
}

/*
 * Thread main function for creating UROM worker context
 *
 * @context [in]: Thread context
 * @return: NULL (dummy return because of pthread requirement)
 */
static void *worker_main(void *context)
{
	uint8_t ret;
	uint64_t id;
	int pthread_ret;
	struct doca_pe *pe;
	doca_error_t result, tmp_result;
	enum doca_ctx_states state;
	const uint64_t nb_tasks = 2;
	struct doca_urom_worker *worker;
	struct loopback_result lb_res = {0};
	struct worker_ctx *ctx = (struct worker_ctx *)context;
	union doca_data cookie;
	char *env[] = {"UCX_LOG_LEVEL=debug"};

	/* Create worker PE */
	result = doca_pe_create(&pe);
	if (result != DOCA_SUCCESS) {
		worker_force_quit = true;
		DOCA_LOG_ERR("Failed to create PE");
		goto worker_exit;
	}

	/* Create and start worker context */
	result = start_urom_worker(pe,
				   ctx->service,
				   ctx->worker_id,
				   &ctx->gid,
				   nb_tasks,
				   NULL,
				   env,
				   1,
				   ctx->plugins,
				   &worker);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Start UROM worker failed, returned error: %s", doca_error_get_descr(result));
		worker_force_quit = true;
		goto pe_destroy;
	}

	ret = 0;
	/* Progress till worker state changes to running or error happened */
	do {
		ret += doca_pe_progress(pe);
		result = doca_ctx_get_state(doca_urom_worker_as_ctx(worker), &state);
	} while (state == DOCA_CTX_STATE_STARTING && result == DOCA_SUCCESS && !worker_force_quit);

	/* Verify that worker state is running */
	if (ret == 0 || state != DOCA_CTX_STATE_RUNNING) {
		DOCA_LOG_ERR("Bad worker state");
		result = DOCA_ERROR_BAD_STATE;
		goto err_exit;
	}

	/* Get worker id */
	result = doca_urom_worker_get_id(worker, &id);
	if (result != DOCA_SUCCESS)
		goto err_exit;

	DOCA_LOG_INFO("Worker id is %lu", id);

	/* Run graph loopback task */
	cookie.ptr = &lb_res;
	result = urom_graph_task_loopback(worker, cookie, id, worker_graph_loopback_finished_cb);
	if (result != DOCA_SUCCESS)
		goto err_exit;

	/* Wait for task completion */
	do {
		ret = doca_pe_progress(pe);
	} while (ret == 0 && !worker_force_quit);

	if (lb_res.result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Loopback Failed, result [%s]", doca_error_get_descr(lb_res.result));
		worker_force_quit = true;
		result = lb_res.result;
		goto worker_destroy;
	}

	/* Check if data was updated accordingly in the task callback function */
	if (lb_res.data != id) {
		DOCA_LOG_ERR("Loopback data is wrong, should be [%lu] and received [%lu]", id, lb_res.data);
		worker_force_quit = true;
		result = DOCA_ERROR_INVALID_VALUE;
		goto worker_destroy;
	}

	DOCA_LOG_INFO("Worker id %lu received loopback data %lu", id, lb_res.data);

	pthread_ret = pthread_mutex_lock(&mutex);
	if (pthread_ret != 0) {
		DOCA_LOG_ERR("Failed to lock resource, error=%d", errno);
		goto err_exit;
	}
	nb_running_workers++;
	pthread_ret = pthread_mutex_unlock(&mutex);
	if (pthread_ret != 0) {
		DOCA_LOG_ERR("Failed to unlock resource, error=%d", errno);
		goto err_exit;
	}

	/* Wait till triggering sample teardown */
	while (!worker_force_quit)
		sleep(1);

	goto worker_destroy;

err_exit:
	worker_force_quit = true;
worker_destroy:
	tmp_result = doca_ctx_stop(doca_urom_worker_as_ctx(worker));
	if (tmp_result != DOCA_SUCCESS && tmp_result != DOCA_ERROR_IN_PROGRESS) {
		DOCA_LOG_ERR("Failed to request stop UROM worker");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	ret = 0;
	do {
		ret += doca_pe_progress(pe);
		tmp_result = doca_ctx_get_state(doca_urom_worker_as_ctx(worker), &state);
	} while (state != DOCA_CTX_STATE_IDLE && tmp_result == DOCA_SUCCESS);

	if (ret == 0 || state != DOCA_CTX_STATE_IDLE) {
		DOCA_LOG_ERR("Failed to stop worker context");
		goto pe_destroy;
	}

	tmp_result = doca_urom_worker_destroy(worker);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy UROM worker returned error: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

pe_destroy:
	tmp_result = doca_pe_destroy(pe);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy PE");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

worker_exit:
	*ctx->exit_status = result;
	pthread_ret = pthread_mutex_lock(&mutex);
	if (pthread_ret != 0) {
		DOCA_LOG_ERR("Failed to lock resource, error=%d", errno);
		goto exit;
	}
	nb_exit_workers++;
	pthread_ret = pthread_mutex_unlock(&mutex);
	if (pthread_ret != 0)
		DOCA_LOG_ERR("Failed to unlock resource, error=%d", errno);
exit:
	free(ctx);
	return NULL;
}

/*
 * Worker odd ids query task completion
 *
 * @task [in]: worker ids query task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void odd_gid_task_cb(struct doca_urom_service_get_workers_by_gid_task *task,
			    union doca_data task_user_data,
			    union doca_data ctx_user_data)
{
	(void)ctx_user_data;

	size_t i;
	size_t worker_counts;
	const uint64_t *ids;
	doca_error_t result;
	uint64_t *is_failure = task_user_data.ptr;

	if (task == NULL) {
		*is_failure = 1;
		return;
	}

	result = doca_task_get_status(doca_urom_service_get_workers_by_gid_task_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Service workers query by gid 1 failed");
		*is_failure = 1;
	}

	worker_counts = doca_urom_service_get_workers_by_gid_task_get_workers_count(task);
	ids = doca_urom_service_get_workers_by_gid_task_get_worker_ids(task);

	for (i = 0; i < worker_counts; i++) {
		DOCA_LOG_DBG("Worker #%lu id is %lu", i, ids[i]);
		if (ids[0] % 2 != 1) {
			DOCA_LOG_ERR("Wrong worker id exists in workers ids list, should be odd number");
			*is_failure = 1;
			return;
		}
	}
	DOCA_LOG_INFO("Worker odd ids query finished successfully");
	*is_failure = 0;
	doca_urom_service_get_workers_by_gid_task_release(task);
}

/*
 * Worker even ids query task completion
 *
 * @task [in]: worker ids query task
 * @task_user_data [in]: doca_data from the task
 * @ctx_user_data [in]: doca_data from the context
 */
static void even_gid_task_cb(struct doca_urom_service_get_workers_by_gid_task *task,
			     union doca_data task_user_data,
			     union doca_data ctx_user_data)
{
	(void)ctx_user_data;

	size_t i;
	size_t worker_counts;
	const uint64_t *ids;
	doca_error_t result;
	uint64_t *is_failure = task_user_data.ptr;

	if (task == NULL) {
		*is_failure = 1;
		return;
	}

	result = doca_task_get_status(doca_urom_service_get_workers_by_gid_task_as_task(task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Service workers query by gid 0 failed");
		*is_failure = 1;
	}

	worker_counts = doca_urom_service_get_workers_by_gid_task_get_workers_count(task);
	ids = doca_urom_service_get_workers_by_gid_task_get_worker_ids(task);

	for (i = 0; i < worker_counts; i++) {
		DOCA_LOG_DBG("Worker #%lu id is %lu", i, ids[i]);
		if (ids[0] % 2 != 0) {
			DOCA_LOG_ERR("Wrong worker id exists in workers ids list, should be even number");
			*is_failure = 1;
			return;
		}
	}
	DOCA_LOG_INFO("Worker even ids query finished successfully");
	*is_failure = 0;
	doca_urom_service_get_workers_by_gid_task_release(task);
}

/*
 * Run multi_workers_bootstrap sample
 *
 * @device_name [in]: DOCA UROM device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t multi_workers_bootstrap(char *device_name)
{
	struct doca_pe *pe;
	union doca_data data;
	struct doca_dev *dev;
	doca_cpu_set_t cpuset;
	struct worker_ctx *ctx;
	uint64_t is_failure = 0;
	const int nb_workers = 4;
	pthread_t ids[nb_workers];
	int ret, idx, actual_workers = 0;
	doca_error_t status_arr[nb_workers];
	size_t i, plugins_count = 0;
	char *plugin_name = "worker_graph";
	doca_error_t result, tmp_result;
	struct doca_urom_service *service;
	const struct doca_urom_service_plugin_info *plugins, *graph_info = NULL;
	struct doca_urom_service_get_workers_by_gid_task *odd_gid_task, *even_gid_task;

	data.ptr = &is_failure;
	memset(status_arr, DOCA_SUCCESS, sizeof(status_arr));

	if (pthread_mutex_init(&mutex, NULL) != 0) {
		DOCA_LOG_ERR("Failed to initiate UROM worker lock, error=%d", errno);
		return DOCA_ERROR_BAD_STATE;
	}

	result = open_doca_device_with_ibdev_name((uint8_t *)device_name, strlen(device_name), NULL, &dev);
	if (result != DOCA_SUCCESS)
		goto mutex_free;

	result = doca_pe_create(&pe);
	if (result != DOCA_SUCCESS)
		goto close_dev;

	result = start_urom_service(pe, dev, nb_workers, &service);
	if (result != DOCA_SUCCESS)
		goto pe_cleanup;

	result = doca_urom_service_get_plugins_list(service, &plugins, &plugins_count);
	if (result != DOCA_SUCCESS || plugins_count == 0)
		goto service_stop;

	for (i = 0; i < plugins_count; i++) {
		if (strcmp(plugin_name, plugins[i].plugin_name) == 0) {
			graph_info = &plugins[i];
			break;
		}
	}

	if (graph_info == NULL) {
		DOCA_LOG_ERR("Failed to match graph plugin");
		result = DOCA_ERROR_INVALID_VALUE;
		goto service_stop;
	}

	result = urom_graph_init(graph_info->id, graph_info->version);
	if (result != DOCA_SUCCESS)
		goto service_stop;

	doca_urom_service_get_cpuset(service, &cpuset);
	for (idx = 0; idx < 8; idx++) {
		if (!doca_cpu_is_set(idx, &cpuset))
			goto service_stop;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Create workers threads */
	for (idx = 0; idx < nb_workers && !worker_force_quit; idx++) {
		ctx = malloc(sizeof(*ctx));
		if (ctx == NULL) {
			DOCA_LOG_ERR("Failed to create worker context");
			worker_force_quit = true;
			result = DOCA_ERROR_NO_MEMORY;
			goto progress;
		}

		ctx->service = service;
		ctx->worker_id = idx;
		/* Split the workers to two groups according to modulo of two */
		ctx->gid = idx % 2;
		ctx->plugins = graph_info->id;
		ctx->exit_status = &status_arr[idx];
		if (pthread_create(&ids[idx], NULL, worker_main, ctx) != 0) {
			worker_force_quit = true;
			result = DOCA_ERROR_IO_FAILED;
			goto progress;
		}
		actual_workers++;
	}

progress:
	/* Handling workers requests for bootstrap */
	do {
		doca_pe_progress(pe);
	} while (nb_running_workers != actual_workers && !worker_force_quit);
	if (result != DOCA_SUCCESS || worker_force_quit)
		goto teardown;

	/* Query worker ids by group id 1 */
	DOCA_LOG_INFO("Start service workers query task with gid 1 for odd ids");
	result = doca_urom_service_get_workers_by_gid_task_allocate_init(service, 1, odd_gid_task_cb, &odd_gid_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate service query task");
		goto teardown;
	}
	doca_task_set_user_data(doca_urom_service_get_workers_by_gid_task_as_task(odd_gid_task), data);
	result = doca_task_submit(doca_urom_service_get_workers_by_gid_task_as_task(odd_gid_task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit service query task");
		goto teardown;
	}

	do {
		ret = doca_pe_progress(pe);
	} while (ret == 0 && !is_failure && !worker_force_quit);

	if (is_failure) {
		DOCA_LOG_ERR("Worker odd ids query finished with errors");
		goto teardown;
	}

	/* Query worker ids by group id 0 */
	DOCA_LOG_INFO("Start service workers query task with gid 0 for even ids");
	result = doca_urom_service_get_workers_by_gid_task_allocate_init(service, 0, even_gid_task_cb, &even_gid_task);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate service query task");
		goto teardown;
	}
	doca_task_set_user_data(doca_urom_service_get_workers_by_gid_task_as_task(even_gid_task), data);
	result = doca_task_submit(doca_urom_service_get_workers_by_gid_task_as_task(even_gid_task));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to submit service query task");
		goto teardown;
	}

	do {
		ret = doca_pe_progress(pe);
	} while (ret == 0 && !is_failure && !worker_force_quit);

	if (is_failure) {
		DOCA_LOG_ERR("Worker even ids query finished with errors");
		goto teardown;
	}

teardown:
	/* Handling workers requests for teardown */
	do {
		doca_pe_progress(pe);
	} while (nb_exit_workers != actual_workers);

	/* Wait all threads to exit */
	for (idx = 0; idx < actual_workers; idx++) {
		pthread_join(ids[idx], NULL);
		DOCA_ERROR_PROPAGATE(result, status_arr[idx]);
	}

service_stop:
	tmp_result = doca_ctx_stop(doca_urom_service_as_ctx(service));
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to stop UROM service");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

	tmp_result = doca_urom_service_destroy(service);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy UROM service");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
pe_cleanup:
	tmp_result = doca_pe_destroy(pe);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy PE");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

close_dev:
	tmp_result = doca_dev_close(dev);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to close device");
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}

mutex_free:
	if (pthread_mutex_destroy(&mutex) != 0)
		DOCA_LOG_ERR("Failed to destroy UROM worker lock, error=%d", errno);

	return result;
}
