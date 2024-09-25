/*
 * Copyright (c) 2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include <stdint.h>
#include <stdlib.h>

#include "worker_rdmo.h"
#include "urom_rdmo.h"

DOCA_LOG_REGISTER(UROM::WORKER::RDMO);

static uint64_t plugin_version = 0x01; /* RDMO plugin DPU version */

/* UCP memory pool for RDMO requests */
static ucs_mpool_ops_t urom_rdmo_req_mpool_ops = {.chunk_alloc = ucs_mpool_chunk_malloc,
						  .chunk_release = ucs_mpool_chunk_free,
						  .obj_init = NULL,
						  .obj_cleanup = NULL,
						  .obj_str = NULL};

/*
 * RDMO active message callback
 *
 * @ctx [in]: RDMO worker context
 * @header [in]: RDMO header
 * @header_length [in]: RDMO header length
 * @data [in]: RDMO data
 * @length [in]: RDMO data length
 * @param [in]: UCX message parameters
 */
static ucs_status_t urom_worker_rdmo_am_cb(void *ctx,
					   const void *header,
					   size_t header_length,
					   void *data,
					   size_t length,
					   const ucp_am_recv_param_t *param)
{
	khint_t k;
	doca_error_t status;
	struct urom_worker_rdmo_ep *ep;
	struct urom_worker_rdmo_req *req;
	struct urom_worker_rdmo_client *client;
	struct urom_rdmo_hdr *rdmo_hdr = (struct urom_rdmo_hdr *)header;
	struct urom_worker_rdmo *rdmo_worker = (struct urom_worker_rdmo *)ctx;

	k = kh_get(client, rdmo_worker->clients, rdmo_hdr->id);
	if (k == kh_end(rdmo_worker->clients)) {
		DOCA_LOG_ERR("Unknown client ID: %#lx", rdmo_hdr->id);
		return UCS_OK;
	}

	client = kh_value(rdmo_worker->clients, k);
	k = kh_get(ep, rdmo_worker->eps, (int64_t)param->reply_ep);
	if (k == kh_end(rdmo_worker->eps)) {
		DOCA_LOG_ERR("Unknown EP: %#lx", rdmo_hdr->id);
		return UCS_OK;
	}

	ep = kh_value(rdmo_worker->eps, k);
	if (rdmo_hdr->op_id > UROM_RDMO_OP_SCATTER) {
		DOCA_LOG_ERR("Invalid op_id: %d", rdmo_hdr->op_id);
		return UCS_OK;
	}

	req = ucs_mpool_get(&rdmo_worker->req_mp);
	if (req == NULL)
		return UCS_ERR_NO_ELEM;

	memset(req, 0, sizeof(*req));

	if (header_length > UROM_RDMO_HDR_LEN_MAX)
		return UCS_ERR_INVALID_PARAM;
	req->client = client;
	req->ep = ep;
	memcpy(req->header, header, header_length); /* AM header does not persist */
	req->data = data;
	req->length = length;
	req->param = *param;
	req->ops = urom_worker_rdmo_ops_table[rdmo_hdr->op_id];
	if (req->ops == NULL || req->ops->progress == NULL)
		return UCS_ERR_NO_RESOURCE;

	status = urom_worker_rdmo_req_queue(req);
	if (status == DOCA_SUCCESS)
		return UCS_OK;
	return UCS_INPROGRESS;
}

/*
 * Close RDMO worker plugin
 *
 * @worker_ctx [in]: DOCA UROM worker context
 */
static void urom_worker_rdmo_close(struct urom_worker_ctx *worker_ctx)
{
	struct urom_worker_rdmo *rdmo_worker = worker_ctx->plugin_ctx;

	if (rdmo_worker == NULL)
		return;

	kh_destroy(ep, rdmo_worker->eps);
	kh_destroy(client, rdmo_worker->clients);
	ucs_mpool_cleanup(&rdmo_worker->req_mp, 0);
	ucp_worker_release_address(rdmo_worker->ucp_data.ucp_worker, rdmo_worker->ucp_data.worker_address);
	ucp_worker_destroy(rdmo_worker->ucp_data.ucp_worker);
	ucp_cleanup(rdmo_worker->ucp_data.ucp_context);
	free(rdmo_worker);
}

/*
 * Open RDMO worker plugin
 *
 * @ctx [in]: DOCA UROM worker context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_open(struct urom_worker_ctx *ctx)
{
	ucs_status_t status;
	ucp_params_t ucp_params;
	ucp_config_t *ucp_config;
	ucp_context_h ucp_context;
	ucp_worker_h ucp_worker;
	ucs_mpool_params_t mp_params;
	ucp_am_handler_param_t am_param;
	ucp_worker_params_t worker_params;
	struct urom_worker_rdmo *rdmo_worker;

	rdmo_worker = calloc(1, sizeof(*rdmo_worker));
	if (rdmo_worker == NULL)
		return DOCA_ERROR_NO_MEMORY;

	ctx->plugin_ctx = rdmo_worker;

	status = ucp_config_read(NULL, NULL, &ucp_config);
	if (status != UCS_OK)
		goto err_cfg;

		/* Use TCP for CM, but do not allow TCP for RDMA */
#if UCP_API_VERSION >= UCP_VERSION(1, 17)
	status = ucp_config_modify(ucp_config, "TCP_PUT_ENABLE", "n");
#else
	status = ucp_config_modify(ucp_config, "PUT_ENABLE", "n");
#endif
	if (status != UCS_OK) {
		ucp_config_release(ucp_config);
		goto err_cfg;
	}

	ucp_params.field_mask = UCP_PARAM_FIELD_FEATURES;
	ucp_params.features = UCP_FEATURE_AM | UCP_FEATURE_RMA | UCP_FEATURE_AMO64 | UCP_FEATURE_EXPORTED_MEMH;
	ucp_params.features |= UCP_FEATURE_WAKEUP;
	status = ucp_init(&ucp_params, ucp_config, &ucp_context);
	ucp_config_release(ucp_config);
	if (status != UCS_OK)
		goto err_cfg;

	worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
	worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
	status = ucp_worker_create(ucp_context, &worker_params, &ucp_worker);
	if (status != UCS_OK)
		goto err_worker_create;

	status = ucp_worker_get_address(ucp_worker,
					&rdmo_worker->ucp_data.worker_address,
					&rdmo_worker->ucp_data.ucp_addrlen);
	if (status != UCS_OK)
		goto err_worker_address;

	DOCA_LOG_DBG("Worker addr length: %lu", rdmo_worker->ucp_data.ucp_addrlen);
	rdmo_worker->ucp_data.ucp_context = ucp_context;
	rdmo_worker->ucp_data.ucp_worker = ucp_worker;
	am_param.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
			      UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG;
	am_param.id = UROM_RDMO_AM_ID;
	am_param.flags = UCP_AM_FLAG_PERSISTENT_DATA;
	am_param.cb = urom_worker_rdmo_am_cb;
	am_param.arg = rdmo_worker;

	status = ucp_worker_set_am_recv_handler(ucp_worker, &am_param);
	if (status != UCS_OK)
		goto err_am;

	rdmo_worker->clients = kh_init(client);
	if (!rdmo_worker->clients)
		goto err_clients;

	rdmo_worker->eps = kh_init(ep);
	if (!rdmo_worker->eps)
		goto err_eps;

	ucs_mpool_params_reset(&mp_params);
	mp_params.elem_size = sizeof(struct urom_worker_rdmo_req);
	mp_params.elems_per_chunk = 1024;
	mp_params.ops = &urom_rdmo_req_mpool_ops;
	mp_params.name = "urom_rdmo_requests";

	/* Create memory pool for requests */
	status = ucs_mpool_init(&mp_params, &rdmo_worker->req_mp);
	if (status != UCS_OK)
		goto err_reqs;

	ucs_list_head_init(&rdmo_worker->completed_reqs);

	return DOCA_SUCCESS;

err_reqs:
	kh_destroy(ep, rdmo_worker->eps);
err_eps:
	kh_destroy(client, rdmo_worker->clients);
err_clients:
err_am:
	ucp_worker_release_address(ucp_worker, rdmo_worker->ucp_data.worker_address);
err_worker_address:
	ucp_worker_destroy(ucp_worker);
err_worker_create:
	ucp_cleanup(ucp_context);
err_cfg:
	free(rdmo_worker);
	return DOCA_ERROR_DRIVER;
}

/*
 * Get RDMO end-point according to UCP worker address
 *
 * @rdmo_worker [in]: RDMO worker context
 * @peer_addr [in]: Client peer address
 * @rdmo_ep [out]: Set RDMO end-point
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_ep_get(struct urom_worker_rdmo *rdmo_worker,
					    void *peer_addr,
					    struct urom_worker_rdmo_ep **rdmo_ep)
{
	ucs_status_t ucs_status;
	khint_t k;
	int ret;
	ucp_ep_params_t ep_params;
	struct urom_worker_rdmo_ep *ep;
	ucp_worker_address_attr_t addr_attr = {
		.field_mask = UCP_WORKER_ADDRESS_ATTR_FIELD_UID,
	};

	ep = calloc(1, sizeof(*ep));
	if (ep == NULL)
		return DOCA_ERROR_NO_MEMORY;

	ep->ref_cnt = 1;
	ep->peer_uid = addr_attr.worker_uid;
	ucs_list_head_init(&ep->fenced_ops);
	ep_params.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
	ep_params.address = peer_addr;

	/* Create end-point on UCX side */
	ucs_status = ucp_ep_create(rdmo_worker->ucp_data.ucp_worker, &ep_params, &ep->ep);
	if (ucs_status != UCS_OK) {
		free(ep);
		return DOCA_ERROR_DRIVER;
	}

	k = kh_put(ep, rdmo_worker->eps, (int64_t)ep->ep, &ret);
	if (ret <= 0) {
		ucp_ep_destroy(ep->ep);
		free(ep);
		return DOCA_ERROR_DRIVER;
	}
	kh_value(rdmo_worker->eps, k) = ep;
	*rdmo_ep = ep;

	DOCA_LOG_DBG("New RDMO EP: %p UCP EP: %p UID: %#lx", ep, ep->ep, ep->peer_uid);
	return DOCA_SUCCESS;
}

/*
 * Get RDMO client structure by destination id.
 *
 * @rdmo_worker [in]: RDMO worker context
 * @dest_id [in]: The destination id
 * @return: A pointer to RDMO client structure and NULL otherwise
 */
static struct urom_worker_rdmo_client *urom_worker_rdmo_dest_to_client(struct urom_worker_rdmo *rdmo_worker,
								       uint64_t dest_id)
{
	khiter_t k;
	struct urom_worker_rdmo_client *client;

	for (k = kh_begin(rdmo_worker->clients); k != kh_end(rdmo_worker->clients); ++k) {
		if (!kh_exist(rdmo_worker->clients, k))
			continue;

		client = kh_value(rdmo_worker->clients, k);
		if (client->dest_id == dest_id)
			return kh_value(rdmo_worker->clients, k);
	}
	return NULL;
}

/*
 * Handle RDMO client init command
 *
 * @rdmo_worker [in]: RDMO worker context
 * @cmd_desc [in]: RDMO command descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_client_init_cmd(struct urom_worker_rdmo *rdmo_worker,
						     struct urom_worker_cmd_desc *cmd_desc)
{
	int ret;
	khint_t k;
	doca_error_t status;
	struct urom_worker_notify *notif;
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo_client *client;
	struct urom_worker_notify_rdmo *rdmo_notif;
	struct urom_worker_cmd *cmd = (struct urom_worker_cmd *)&cmd_desc->worker_cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;

	/* Prepare notification */
	nd = calloc(1, sizeof(*nd) + sizeof(*rdmo_notif));
	if (nd == NULL)
		return DOCA_ERROR_NO_MEMORY;

	nd->dest_id = cmd_desc->dest_id;

	notif = (struct urom_worker_notify *)&nd->worker_notif;
	notif->type = cmd->type;
	notif->urom_context = cmd->urom_context;
	notif->len = sizeof(*rdmo_notif) + rdmo_worker->ucp_data.ucp_addrlen;
	notif->status = DOCA_SUCCESS;

	rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;
	rdmo_notif->type = UROM_WORKER_NOTIFY_RDMO_CLIENT_INIT;

	/* Return UCP worker address */
	rdmo_notif->client_init.addr = rdmo_worker->ucp_data.worker_address;
	rdmo_notif->client_init.addr_len = rdmo_worker->ucp_data.ucp_addrlen;

	client = urom_worker_rdmo_dest_to_client(rdmo_worker, cmd_desc->dest_id);
	if (client != NULL) {
		DOCA_LOG_ERR("Client already initialized");
		status = DOCA_ERROR_ALREADY_EXIST;
		goto push_err;
	}

	k = kh_get(client, rdmo_worker->clients, rdmo_cmd->client_init.id);
	if (k != kh_end(rdmo_worker->clients)) {
		DOCA_LOG_ERR("Client ID unavailable: %#lx", rdmo_cmd->client_init.id);
		return DOCA_ERROR_IN_USE;
	}

	client = calloc(1, sizeof(*client));
	if (client == NULL) {
		status = DOCA_ERROR_NO_MEMORY;
		goto push_err;
	}

	client->rdmo_worker = rdmo_worker;
	client->id = rdmo_cmd->client_init.id;
	client->dest_id = cmd_desc->dest_id;
	ucs_list_head_init(&client->paused_ops);

	DOCA_LOG_DBG("Client worker address len: %lu", rdmo_cmd->client_init.addr_len);

	/* Create host access EP */
	status = urom_worker_rdmo_ep_get(rdmo_worker, rdmo_cmd->client_init.addr, &client->ep);
	if (status != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to get host access EP");
		goto client_destroy;
	}

	client->mkeys = kh_init(mkey);
	if (client->mkeys == NULL) {
		status = DOCA_ERROR_INITIALIZATION;
		goto ep_destroy;
	}

	client->rqs = kh_init(rq);
	if (client->rqs == NULL) {
		status = DOCA_ERROR_INITIALIZATION;
		goto mkeys_destroy;
	}

	k = kh_put(client, rdmo_worker->clients, client->id, &ret);
	if (ret <= 0) {
		status = DOCA_ERROR_DRIVER;
		goto rqs_destroy;
	}
	kh_value(rdmo_worker->clients, k) = client;

	DOCA_LOG_DBG("Local worker address len: %lu", rdmo_notif->client_init.addr_len);
	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);

	DOCA_LOG_DBG("RDMO client initialized (client: %p ep: %p)", client, client->ep);

	return DOCA_SUCCESS;

rqs_destroy:
	kh_destroy(rq, client->rqs);
mkeys_destroy:
	kh_destroy(mkey, client->mkeys);
ep_destroy:
	ucp_ep_destroy(client->ep->ep);
client_destroy:
	free(client);
push_err:
	notif->status = status;
	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);
	return status;
}

/*
 * Handle RDMO MR deregister command
 *
 * @rdmo_worker [in]: RDMO worker context
 * @cmd_desc [in]: RDMO command descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_mr_dereg_cmd(struct urom_worker_rdmo *rdmo_worker,
						  struct urom_worker_cmd_desc *cmd_desc)
{
	khint_t k;
	doca_error_t status;
	ucs_status_t ucs_status;
	struct urom_worker_rdmo_mkey *rdmo_mkey;
	struct urom_worker_notify *notif;
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo_client *client;
	struct urom_worker_notify_rdmo *rdmo_notif;
	struct urom_worker_cmd *cmd = (struct urom_worker_cmd *)&cmd_desc->worker_cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;

	client = urom_worker_rdmo_dest_to_client(rdmo_worker, cmd_desc->dest_id);
	if (client == NULL) {
		DOCA_LOG_ERR("Client is not initialized");
		return DOCA_ERROR_NOT_FOUND;
	}

	k = kh_get(mkey, client->mkeys, rdmo_cmd->mr_dereg.rkey);
	if (k == kh_end(client->mkeys)) {
		DOCA_LOG_ERR("Mkey does not exists for rkey: %lu", rdmo_cmd->mr_dereg.rkey);
		status = DOCA_ERROR_NOT_FOUND;
		goto push_notif;
	}

	rdmo_mkey = kh_value(client->mkeys, k);
	/* Unmap mem in case memh is present */
	if (rdmo_mkey->ucp_memh != NULL) {
		ucs_status = ucp_mem_unmap(rdmo_worker->ucp_data.ucp_context, rdmo_mkey->ucp_memh);
		if (ucs_status != UCS_OK) {
			DOCA_LOG_ERR("ucp_mem_unmap() returned error: %s", ucs_status_string(ucs_status));
			status = DOCA_ERROR_DRIVER;
			goto push_notif;
		}
	}

	ucp_rkey_destroy(rdmo_mkey->ucp_rkey);
	kh_destroy(mem_cache, rdmo_mkey->mem_cache);
	kh_del(mkey, client->mkeys, k);
	free(rdmo_mkey);

	status = DOCA_SUCCESS;

push_notif:
	/* Prepare notification */
	nd = calloc(1, sizeof(*nd) + sizeof(*rdmo_notif));
	if (nd == NULL) {
		kh_del(rq, client->rqs, k);
		return DOCA_ERROR_NO_MEMORY;
	}

	nd->dest_id = cmd_desc->dest_id;

	notif = (struct urom_worker_notify *)&nd->worker_notif;
	notif->type = cmd->type;
	notif->urom_context = cmd->urom_context;
	notif->len = sizeof(*rdmo_notif);
	notif->status = status;

	rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;
	rdmo_notif->type = UROM_WORKER_NOTIFY_RDMO_MR_DEREG;
	rdmo_notif->mr_dereg.rkey = rdmo_cmd->mr_dereg.rkey;

	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);

	DOCA_LOG_DBG("Dereg - rkey: %lu (client: %p)", rdmo_cmd->mr_dereg.rkey, client);
	return status;
}

/*
 * Handle RDMO RQ create command
 *
 * @rdmo_worker [in]: RDMO worker context
 * @cmd_desc [in]: RDMO command descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_rq_create_cmd(struct urom_worker_rdmo *rdmo_worker,
						   struct urom_worker_cmd_desc *cmd_desc)
{
	int ret;
	khint_t k;
	doca_error_t status;
	struct urom_worker_rdmo_rq *rq;
	struct urom_worker_notify *notif;
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo_client *client;
	struct urom_worker_notify_rdmo *rdmo_notif;
	struct urom_worker_cmd *cmd = (struct urom_worker_cmd *)&cmd_desc->worker_cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;

	/* Prepare notification */
	nd = calloc(1, sizeof(*nd) + sizeof(*rdmo_notif));
	if (nd == NULL)
		return DOCA_ERROR_NO_MEMORY;

	nd->dest_id = cmd_desc->dest_id;

	notif = (struct urom_worker_notify *)&nd->worker_notif;
	notif->type = cmd->type;
	notif->urom_context = cmd->urom_context;
	notif->len = sizeof(*rdmo_notif);

	rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;
	rdmo_notif->type = UROM_WORKER_NOTIFY_RDMO_RQ_CREATE;

	client = urom_worker_rdmo_dest_to_client(rdmo_worker, cmd_desc->dest_id);
	if (client == NULL) {
		DOCA_LOG_ERR("Client is not initialized");
		status = DOCA_ERROR_NOT_FOUND;
		goto push_notif;
	}

	rq = calloc(1, sizeof(*rq));
	if (rq == NULL) {
		status = DOCA_ERROR_NO_MEMORY;
		goto push_notif;
	}

	rq->id = client->next_rq_id++;
	k = kh_put(rq, client->rqs, rq->id, &ret);
	if (ret <= 0) {
		free(rq);
		status = DOCA_ERROR_DRIVER;
		goto push_notif;
	}
	kh_value(client->rqs, k) = rq;

	status = urom_worker_rdmo_ep_get(rdmo_worker, rdmo_cmd->rq_create.addr, &rq->ep);
	if (status != DOCA_SUCCESS) {
		kh_del(rq, client->rqs, k);
		free(rq);
		goto push_notif;
	}

	/* Set RQ id */
	rdmo_notif->rq_create.rq_id = rq->id;
	DOCA_LOG_DBG("Created RQ: %p ID: %#lx (client: %p ep: %p)", rq, rq->id, client, rq->ep);
	status = DOCA_SUCCESS;

push_notif:
	notif->status = status;
	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);
	return status;
}

/*
 * RDMO UCP EP destroy function
 *
 * @rdmo_worker [in]: RDMO worker context
 * @rdmo_ep [in]: RDMO endpoint
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_ep_del(struct urom_worker_rdmo *rdmo_worker, struct urom_worker_rdmo_ep *rdmo_ep)
{
	khint_t k;
	ucs_status_ptr_t ucs_status_ptr;

	k = kh_get(ep, rdmo_worker->eps, (int64_t)rdmo_ep->ep);
	if (k == kh_end(rdmo_worker->eps)) {
		DOCA_LOG_ERR("EP does not exist - RDMO EP: %p UCP EP: %p", rdmo_ep, rdmo_ep->ep);
		return DOCA_ERROR_NOT_FOUND;
	}
	ucs_status_ptr = ucp_ep_close_nb(rdmo_ep->ep, UCP_EP_CLOSE_MODE_FLUSH);
	if (UCS_PTR_IS_ERR(ucs_status_ptr))
		return DOCA_ERROR_DRIVER;

	kh_del(ep, rdmo_worker->eps, k);
	ucp_request_release(ucs_status_ptr);
	free(rdmo_ep);

	return DOCA_SUCCESS;
}

/*
 * Handle RDMO RQ Destroy command
 *
 * @rdmo_worker [in]: RDMO worker context
 * @cmd_desc [in]: RDMO command descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_rq_destroy_cmd(struct urom_worker_rdmo *rdmo_worker,
						    struct urom_worker_cmd_desc *cmd_desc)
{
	khint_t k;
	doca_error_t status;
	struct urom_worker_rdmo_rq *rq = NULL;
	struct urom_worker_notify *notif;
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo_client *client;
	struct urom_worker_notify_rdmo *rdmo_notif;
	struct urom_worker_cmd *cmd = (struct urom_worker_cmd *)&cmd_desc->worker_cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;

	client = urom_worker_rdmo_dest_to_client(rdmo_worker, cmd_desc->dest_id);
	if (client == NULL) {
		DOCA_LOG_ERR("Client is not initialized");
		return DOCA_ERROR_NOT_FOUND;
	}

	k = kh_get(rq, client->rqs, rdmo_cmd->rq_destroy.rq_id);
	if (k == kh_end(client->rqs)) {
		DOCA_LOG_ERR("RQ does not exist for ID: %lu", rdmo_cmd->rq_destroy.rq_id);
		status = DOCA_ERROR_NOT_FOUND;
		goto push_notif;
	}
	rq = kh_value(client->rqs, k);

	status = urom_worker_rdmo_ep_del(rdmo_worker, rq->ep);
	if (status != DOCA_SUCCESS)
		goto push_notif;

	kh_del(rq, client->rqs, k);

	status = DOCA_SUCCESS;

push_notif:

	/* Prepare notification */
	nd = calloc(1, sizeof(*nd) + sizeof(*rdmo_notif));
	if (nd == NULL) {
		kh_del(rq, client->rqs, k);
		return DOCA_ERROR_NO_MEMORY;
	}

	nd->dest_id = cmd_desc->dest_id;

	notif = (struct urom_worker_notify *)&nd->worker_notif;
	notif->type = cmd->type;
	notif->urom_context = cmd->urom_context;
	notif->len = sizeof(*rdmo_notif);
	notif->status = status;

	rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;
	rdmo_notif->type = UROM_WORKER_NOTIFY_RDMO_RQ_DESTROY;
	rdmo_notif->rq_destroy.rq_id = rdmo_cmd->rq_destroy.rq_id;

	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);

	DOCA_LOG_DBG("Destroyed RQ for ID: %#lx (client: %p)", rdmo_notif->rq_destroy.rq_id, client);
	return status;
}

/*
 * Handle RDMO MR register command
 *
 * @rdmo_worker [in]: RDMO worker context
 * @cmd_desc [in]: RDMO command descriptor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_mr_reg_cmd(struct urom_worker_rdmo *rdmo_worker,
						struct urom_worker_cmd_desc *cmd_desc)
{
	int ret;
	khint_t k;
	uint64_t rkey;
	ucp_rkey_h ucp_rkey;
	doca_error_t status;
	ucs_status_t ucs_status;
	ucp_mem_h ucp_memh = NULL;
	ucp_mem_map_params_t map_params;
	struct urom_worker_notify *notif;
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo_client *client;
	struct urom_worker_rdmo_mkey *rdmo_mkey;
	struct urom_worker_notify_rdmo *rdmo_notif;
	struct urom_worker_cmd *cmd = (struct urom_worker_cmd *)&cmd_desc->worker_cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;

	/* Prepare notification */
	nd = calloc(1, sizeof(*nd) + sizeof(*rdmo_notif));
	if (nd == NULL)
		return DOCA_ERROR_NO_MEMORY;

	nd->dest_id = cmd_desc->dest_id;

	notif = (struct urom_worker_notify *)&nd->worker_notif;
	notif->type = cmd->type;
	notif->urom_context = cmd->urom_context;
	notif->len = sizeof(*rdmo_notif);

	rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;
	rdmo_notif->type = UROM_WORKER_NOTIFY_RDMO_MR_REG;

	rdmo_mkey = malloc(sizeof(*rdmo_mkey));
	if (rdmo_mkey == NULL) {
		status = DOCA_ERROR_NO_MEMORY;
		goto push_notif;
	}

	client = urom_worker_rdmo_dest_to_client(rdmo_worker, cmd_desc->dest_id);
	if (client == NULL) {
		DOCA_LOG_ERR("Client is not initialized");
		free(rdmo_mkey);
		status = DOCA_ERROR_NOT_FOUND;
		goto push_notif;
	}

	ucs_status = ucp_ep_rkey_unpack(client->ep->ep, rdmo_cmd->mr_reg.packed_rkey, &ucp_rkey);
	if (ucs_status != UCS_OK) {
		status = DOCA_ERROR_DRIVER;
		free(rdmo_mkey);
		goto push_notif;
	}

	if (rdmo_cmd->mr_reg.packed_memh) {
		map_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_EXPORTED_MEMH_BUFFER;
		map_params.exported_memh_buffer = rdmo_cmd->mr_reg.packed_memh;
		ucs_status = ucp_mem_map(rdmo_worker->ucp_data.ucp_context, &map_params, &ucp_memh);
		if (ucs_status == UCS_ERR_UNREACHABLE) {
			DOCA_LOG_ERR("EXPORTED_MEMH unsupported");
			ucp_memh = NULL;
		} else if (ucs_status != UCS_OK) {
			status = DOCA_ERROR_DRIVER;
			free(rdmo_mkey);
			goto push_notif;
		}
	}

	rdmo_mkey->mem_cache = kh_init(mem_cache);
	if (rdmo_mkey->mem_cache == NULL) {
		free(rdmo_mkey);
		status = DOCA_ERROR_INITIALIZATION;
		goto push_notif;
	}

	rdmo_mkey->ucp_rkey = ucp_rkey;
	rdmo_mkey->ucp_memh = ucp_memh;
	rdmo_mkey->va = rdmo_cmd->mr_reg.va;
	rdmo_mkey->len = rdmo_cmd->mr_reg.len;
	rkey = (uintptr_t)ucp_rkey;

	k = kh_put(mkey, client->mkeys, rkey, &ret);
	if (ret <= 0) {
		free(rdmo_mkey);
		status = DOCA_ERROR_DRIVER;
		goto push_notif;
	}
	kh_value(client->mkeys, k) = rdmo_mkey;

	rdmo_notif->mr_reg.rkey = rkey;
	status = DOCA_SUCCESS;
	DOCA_LOG_DBG("Allocated rkey: %lu ucp_memh: %p (client: %p)", rkey, ucp_memh, client);

push_notif:
	notif->status = status;
	ucs_list_add_tail(&rdmo_worker->completed_reqs, &nd->entry);

	return DOCA_SUCCESS;
}

/*
 * Unpacking RDMO worker command
 *
 * @packed_cmd [in]: packed worker command
 * @packed_cmd_len [in]: packed worker command length
 * @cmd [out]: set unpacked UROM worker command
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_cmd_unpack(void *packed_cmd, size_t packed_cmd_len, struct urom_worker_cmd **cmd)
{
	void *ptr;
	struct urom_worker_rdmo_cmd *rdmo_cmd;
	uint64_t extended_mem = 0;

	if (packed_cmd_len < sizeof(struct urom_worker_rdmo_cmd)) {
		DOCA_LOG_INFO("Invalid packed command length");
		return DOCA_ERROR_INVALID_VALUE;
	}

	*cmd = packed_cmd;
	ptr = packed_cmd + ucs_offsetof(struct urom_worker_cmd, plugin_cmd) + sizeof(struct urom_worker_rdmo_cmd);
	rdmo_cmd = (struct urom_worker_rdmo_cmd *)(*cmd)->plugin_cmd;

	switch (rdmo_cmd->type) {
	case UROM_WORKER_CMD_RDMO_CLIENT_INIT:
		rdmo_cmd->client_init.addr = ptr;
		extended_mem += rdmo_cmd->client_init.addr_len;
		break;
	case UROM_WORKER_CMD_RDMO_RQ_CREATE:
		rdmo_cmd->rq_create.addr = ptr;
		extended_mem += rdmo_cmd->rq_create.addr_len;
		break;
	case UROM_WORKER_CMD_RDMO_MR_REG:
		rdmo_cmd->mr_reg.packed_rkey = ptr;
		ptr += rdmo_cmd->mr_reg.packed_rkey_len;
		extended_mem += rdmo_cmd->mr_reg.packed_rkey_len;
		rdmo_cmd->mr_reg.packed_memh = ptr;
		extended_mem += rdmo_cmd->mr_reg.packed_memh_len;
		break;
	}

	if ((*cmd)->len != extended_mem + sizeof(struct urom_worker_rdmo_cmd)) {
		DOCA_LOG_ERR("Invalid RDMO command length");
		return DOCA_ERROR_INVALID_VALUE;
	}
	return DOCA_SUCCESS;
}

/*
 * Handle UROM RDMO worker commands function
 *
 * @ctx [in]: DOCA UROM worker context
 * @cmd_list [in]: command descriptor list to handle
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_worker_cmd(struct urom_worker_ctx *ctx, ucs_list_link_t *cmd_list)
{
	doca_error_t status = DOCA_SUCCESS;
	struct urom_worker_cmd_desc *cmd_desc;
	struct urom_worker_cmd *cmd;
	struct urom_worker_rdmo_cmd *rdmo_cmd;
	struct urom_worker_rdmo *rdmo_worker = (struct urom_worker_rdmo *)ctx->plugin_ctx;

	while (!ucs_list_is_empty(cmd_list)) {
		cmd_desc = ucs_list_extract_head(cmd_list, struct urom_worker_cmd_desc, entry);

		status = urom_worker_rdmo_cmd_unpack(&cmd_desc->worker_cmd, cmd_desc->worker_cmd.len, &cmd);
		if (status != DOCA_SUCCESS) {
			free(cmd_desc);
			return status;
		}
		rdmo_cmd = (struct urom_worker_rdmo_cmd *)cmd->plugin_cmd;
		switch (rdmo_cmd->type) {
		case UROM_WORKER_CMD_RDMO_CLIENT_INIT:
			status = urom_worker_rdmo_client_init_cmd(rdmo_worker, cmd_desc);
			break;
		case UROM_WORKER_CMD_RDMO_RQ_CREATE:
			status = urom_worker_rdmo_rq_create_cmd(rdmo_worker, cmd_desc);
			break;
		case UROM_WORKER_CMD_RDMO_RQ_DESTROY:
			status = urom_worker_rdmo_rq_destroy_cmd(rdmo_worker, cmd_desc);
			break;
		case UROM_WORKER_CMD_RDMO_MR_REG:
			status = urom_worker_rdmo_mr_reg_cmd(rdmo_worker, cmd_desc);
			break;
		case UROM_WORKER_CMD_RDMO_MR_DEREG:
			status = urom_worker_rdmo_mr_dereg_cmd(rdmo_worker, cmd_desc);
			break;
		default:
			DOCA_LOG_INFO("Invalid RDMO command type: %lu", rdmo_cmd->type);
			status = DOCA_ERROR_INVALID_VALUE;
			break;
		}
		free(cmd_desc);
		if (status != DOCA_SUCCESS)
			return status;
	}
	return status;
}

/*
 * Check RDMO worker tasks progress to get notifications
 *
 * @ctx [in]: DOCA UROM worker context
 * @notif_list [out]: set notification descriptors for completed tasks
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_progress(struct urom_worker_ctx *ctx, ucs_list_link_t *notif_list)
{
	struct urom_worker_notif_desc *nd;
	struct urom_worker_rdmo *rdmo_worker = (struct urom_worker_rdmo *)ctx->plugin_ctx;

	ucp_worker_progress(rdmo_worker->ucp_data.ucp_worker);

	if (ucs_list_is_empty(&rdmo_worker->completed_reqs))
		return DOCA_ERROR_EMPTY;

	while (!ucs_list_is_empty(&rdmo_worker->completed_reqs)) {
		nd = ucs_list_extract_head(&rdmo_worker->completed_reqs, struct urom_worker_notif_desc, entry);
		ucs_list_add_tail(notif_list, &nd->entry);
		/* Caller must free entries in notif_list */
	}
	return DOCA_SUCCESS;
}

/*
 * Packing RDMO notification
 *
 * @notif [in]: RDMO notification to pack
 * @packed_notif_len [in/out]: set packed notification command buffer size
 * @packed_notif [out]: set packed notification command buffer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_notif_pack(struct urom_worker_notify *notif,
						size_t *packed_notif_len,
						void *packed_notif)
{
	int pack_len;
	void *pack_head;
	void *pack_tail = packed_notif;
	struct urom_worker_notify_rdmo *rdmo_notif = (struct urom_worker_notify_rdmo *)notif->plugin_notif;

	/* Pack base command */
	pack_len = ucs_offsetof(struct urom_worker_notify, plugin_notif) + sizeof(struct urom_worker_notify_rdmo);
	pack_head = urom_rdmo_serialize_next_raw(&pack_tail, void, pack_len);
	memcpy(pack_head, notif, pack_len);
	*packed_notif_len = pack_len;

	/* Pack inline data */
	switch (rdmo_notif->type) {
	case UROM_WORKER_NOTIFY_RDMO_CLIENT_INIT:
		pack_len = rdmo_notif->client_init.addr_len;
		pack_head = urom_rdmo_serialize_next_raw(&pack_tail, void, pack_len);
		memcpy(pack_head, rdmo_notif->client_init.addr, pack_len);
		*packed_notif_len += pack_len;
		break;
	}
	return DOCA_SUCCESS;
}

/*
 * Get RDMO worker address
 *
 * UROM worker calls the function twice, first one to get address length and second one to get address data
 *
 * @worker_ctx [in]: DOCA UROM worker context
 * @addr [out]: set worker address
 * @addr_len [out]: set worker address length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_addr(struct urom_worker_ctx *worker_ctx, void *addr, uint64_t *addr_len)
{
	struct urom_worker_rdmo *rdmo_worker = (struct urom_worker_rdmo *)worker_ctx->plugin_ctx;

	/* Always return address size */
	if (*addr_len < rdmo_worker->ucp_data.ucp_addrlen) {
		/* Return required buffer size on error */
		*addr_len = rdmo_worker->ucp_data.ucp_addrlen;
		return DOCA_ERROR_INVALID_VALUE;
	}

	*addr_len = rdmo_worker->ucp_data.ucp_addrlen;
	memcpy(addr, rdmo_worker->ucp_data.worker_address, *addr_len);
	return DOCA_SUCCESS;
}

/* Define UROM RDMO plugin interface, set plugin functions */
static struct urom_worker_rdmo_iface urom_worker_rdmo = {
	.super.open = urom_worker_rdmo_open,
	.super.close = urom_worker_rdmo_close,
	.super.addr = urom_worker_rdmo_addr,
	.super.worker_cmd = urom_worker_rdmo_worker_cmd,
	.super.progress = urom_worker_rdmo_progress,
	.super.notif_pack = urom_worker_rdmo_notif_pack,
};

doca_error_t urom_plugin_get_iface(struct urom_plugin_iface *iface)
{
	if (iface == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	DOCA_STRUCT_CTOR(urom_worker_rdmo.super);
	*iface = urom_worker_rdmo.super;
	return DOCA_SUCCESS;
}

doca_error_t urom_plugin_get_version(uint64_t *version)
{
	if (version == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	*version = plugin_version;
	return DOCA_SUCCESS;
}
