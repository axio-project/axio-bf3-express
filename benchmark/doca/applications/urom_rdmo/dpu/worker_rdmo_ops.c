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

DOCA_LOG_REGISTER(UROM::WORKER::RDMO::OPS);

#define urom_rdmo_compiler_fence() asm volatile("" ::: "memory") /* Memory barrier */

/*
 * Get memory address by id from mkey cache
 *
 * @rdmo_mkey [in]: RDMO mkey structure
 * @addr [in]: address id
 * @val [out]: set address value from cache
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_mem_cache_get(struct urom_worker_rdmo_mkey *rdmo_mkey,
						   uint64_t addr,
						   uint64_t *val)
{
	khint_t k;

	k = kh_get(mem_cache, rdmo_mkey->mem_cache, addr);
	if (k == kh_end(rdmo_mkey->mem_cache)) {
		DOCA_LOG_DBG("Cache miss addr: %#lx", addr);
		return DOCA_ERROR_NOT_FOUND;
	}

	*val = kh_value(rdmo_mkey->mem_cache, k);
	DOCA_LOG_DBG("Cache hit addr: %#lx val: %#lx", addr, *val);

	return DOCA_SUCCESS;
}

/*
 * Set memory address by id in mkey cache
 *
 * @rdmo_mkey [in]: RDMO mkey structure
 * @addr [in]: address id
 * @val [in]: address value
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_mem_cache_put(struct urom_worker_rdmo_mkey *rdmo_mkey, uint64_t addr, uint64_t val)
{
	khint_t k;
	int ret;

	k = kh_get(mem_cache, rdmo_mkey->mem_cache, addr);
	if (k != kh_end(rdmo_mkey->mem_cache)) {
		kh_value(rdmo_mkey->mem_cache, k) = val;
		DOCA_LOG_DBG("Cache update addr: %#lx val: %#lx", addr, val);
	} else {
		k = kh_put(mem_cache, rdmo_mkey->mem_cache, addr, &ret);
		if (ret < 0) {
			DOCA_LOG_ERR("Failed to rdmo mkey");
			return DOCA_ERROR_DRIVER;
		}
		kh_value(rdmo_mkey->mem_cache, k) = val;
		DOCA_LOG_DBG("Cache insert addr: %#lx val: %#lx", addr, val);
	}
	return DOCA_SUCCESS;
}

/*
 * RDMO address flush callback
 *
 * @request [in]: flush request
 * @ucs_status [in]: operation status
 * @user_data [in]: user data
 */
static void urom_worker_rdmo_addr_flush_cb(void *request, ucs_status_t ucs_status, void *user_data)
{
	if (ucs_status != UCS_OK)
		return;

	ucs_mpool_put(user_data);
	ucp_request_free(request);
}

/*
 * Register address flush handle
 *
 * @client [in]: RDMO client
 * @addr [in]: address id
 * @val [in]: address value
 * @rdmo_mkey [in]: RDMO mkey
 * @return: UCX_OK on success and error status otherwise
 */
static ucs_status_ptr_t urom_worker_rdmo_addr_flush_slow(struct urom_worker_rdmo_client *client,
							 uint64_t addr,
							 uint64_t val,
							 struct urom_worker_rdmo_mkey *rdmo_mkey)
{
	ucp_request_param_t req_param = {0};
	uint64_t *req;

	DOCA_LOG_DBG("Slow flush: %#lx = %#lx (client: %p)", addr, val, client);

	req = ucs_mpool_get(&client->rdmo_worker->req_mp);
	if (req == NULL)
		return NULL;
	*req = val;

	req_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
	req_param.cb.send = urom_worker_rdmo_addr_flush_cb;
	req_param.user_data = req;

	return ucp_put_nbx(client->ep->ep, req, 8, addr, rdmo_mkey->ucp_rkey, &req_param);
}

/*
 * Handle memory cache flush
 *
 * @client [in]: RDMO client
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_mem_cache_flush(struct urom_worker_rdmo_client *client)
{
	khint_t k;
	khint_t j;
	struct urom_worker_rdmo_mkey *rdmo_mkey;
	uint64_t addr;
	uint64_t val;
	ucp_request_param_t req_param;
	ucs_status_ptr_t ucs_status_ptr;
	uint64_t get_addr = 0;
	ucp_rkey_h get_rkey;

	/* For each client mkey */
	for (k = kh_begin(client->mkeys); k != kh_end(client->mkeys); ++k) {
		if (!kh_exist(client->mkeys, k))
			continue;

		rdmo_mkey = kh_value(client->mkeys, k);

		/* flush each cached addr */
		for (j = kh_begin(rdmo_mkey->mem_cache); j != kh_end(rdmo_mkey->mem_cache); ++j) {
			if (!kh_exist(rdmo_mkey->mem_cache, j))
				continue;

			addr = kh_key(rdmo_mkey->mem_cache, j);
			val = kh_value(rdmo_mkey->mem_cache, j);
			kh_del(mem_cache, rdmo_mkey->mem_cache, j);

			/* Try send from stack */
			memset(&req_param, 0, sizeof(req_param));
			req_param.op_attr_mask = UCP_OP_ATTR_FLAG_FORCE_IMM_CMPL;

			ucs_status_ptr = ucp_put_nbx(client->ep->ep, &val, 8, addr, rdmo_mkey->ucp_rkey, &req_param);
			if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_ERR_NO_RESOURCE) {
				/* Fall back to send from heap */
				ucs_status_ptr = urom_worker_rdmo_addr_flush_slow(client, addr, val, rdmo_mkey);
			} else {
				if (UCS_PTR_IS_PTR(ucs_status_ptr))
					ucp_request_free(ucs_status_ptr);
			}

			if (UCS_PTR_IS_ERR(ucs_status_ptr))
				return DOCA_ERROR_DRIVER;

			DOCA_LOG_DBG("Flushed %#lx = %#lx (client: %p)", addr, val, client);

			/* Save one of t	he flushed addresses to use with a flushing get */
			if (!get_addr) {
				get_addr = addr;
				get_rkey = rdmo_mkey->ucp_rkey;
			}
		}
	}

	/* Use a Get to make Put data visibility */
	if (get_addr) {
		memset(&req_param, 0, sizeof(req_param));
		ucs_status_ptr = ucp_get_nbx(client->ep->ep, &client->get_result, 8, get_addr, get_rkey, &req_param);

		if (UCS_PTR_IS_ERR(ucs_status_ptr))
			return DOCA_ERROR_DRIVER;

		if (UCS_PTR_IS_PTR(ucs_status_ptr))
			ucp_request_free(ucs_status_ptr);

		DOCA_LOG_DBG("Issued flushing Get to %#lx", addr);
	}

	return DOCA_SUCCESS;
}

/*
 * Free RDMO request
 *
 * @req [in]: RDMO request
 */
static void urom_worker_rdmo_req_free(struct urom_worker_rdmo_req *req)
{
	req->ep->oreqs--;
	ucs_mpool_put(req);
}

/*
 * Free RDMO request's data
 *
 * @req [in]: RDMO request
 */
static void urom_worker_rdmo_req_free_data(struct urom_worker_rdmo_req *req)
{
	struct urom_worker_rdmo *rdmo_worker = req->client->rdmo_worker;

	if (!(req->param.recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV))
		ucp_am_data_release(rdmo_worker->ucp_data.ucp_worker, req->data);
}

/*
 * Launch RDMO request
 *
 * @req [in]: RDMO request
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_req_start(struct urom_worker_rdmo_req *req)
{
	struct urom_worker_rdmo_ep *ep = req->ep;
	struct urom_rdmo_hdr *rdmo_hdr = (struct urom_rdmo_hdr *)req->header;
	doca_error_t status;

	if (!ucs_list_is_empty(&ep->fenced_ops) || (rdmo_hdr->flags & UROM_RDMO_REQ_FLAG_FENCE && ep->oreqs)) {
		DOCA_LOG_DBG("New fenced request: %p", req);
		ucs_list_add_tail(&ep->fenced_ops, &req->entry);
		return DOCA_ERROR_IN_PROGRESS;
	}

	ep->oreqs++;
	status = req->ops->progress(req);

	return status;
}

/*
 * Check if fence operation is required for the end-point
 *
 * @ep [in]: RDMO end-point
 */
static void urom_worker_rdmo_check_fenced(struct urom_worker_rdmo_ep *ep)
{
	struct urom_worker_rdmo_req *req;
	const struct urom_rdmo_hdr *rdmo_hdr;
	doca_error_t status;

	if (ep->oreqs || ucs_list_is_empty(&ep->fenced_ops))
		return;

	/* Progress fenced ops */
	while (!ucs_list_is_empty(&ep->fenced_ops)) {
		req = ucs_list_head(&ep->fenced_ops, struct urom_worker_rdmo_req, entry);

		rdmo_hdr = (const struct urom_rdmo_hdr *)req->header;
		if (rdmo_hdr->flags & UROM_RDMO_REQ_FLAG_FENCE && ep->oreqs) {
			DOCA_LOG_DBG("Fenced on req: %p", req);
			break;
		}

		ucs_list_del(&req->entry);

		DOCA_LOG_DBG("Starting req: %p", req);
		ep->oreqs++;
		status = req->ops->progress(req);
		if (status == DOCA_SUCCESS) {
			urom_worker_rdmo_req_free_data(req);
			urom_worker_rdmo_req_free(req);
		}
	}
}

/*
 * Progress client paused RDMO operations
 *
 * @client [in]: RDMO client
 */
static void urom_worker_rdmo_check_paused(struct urom_worker_rdmo_client *client)
{
	struct urom_worker_rdmo_req *req;
	doca_error_t status;

	if (client->pause || ucs_list_is_empty(&client->paused_ops))
		return;

	/* Progress paused ops */
	while (!client->pause && !ucs_list_is_empty(&client->paused_ops)) {
		req = ucs_list_extract_head(&client->paused_ops, struct urom_worker_rdmo_req, entry);

		DOCA_LOG_DBG("Starting req: %p", req);

		status = urom_worker_rdmo_req_start(req);
		if (status == DOCA_SUCCESS) {
			urom_worker_rdmo_req_free_data(req);
			urom_worker_rdmo_req_free(req);
		}
	}
}

/*
 * RDMO request completion callback
 *
 * @req [in]: scatter request
 */
static void urom_worker_rdmo_req_complete(struct urom_worker_rdmo_req *req)
{
	urom_worker_rdmo_req_free(req);
	urom_worker_rdmo_check_paused(req->client);
	urom_worker_rdmo_check_fenced(req->ep);
}

/*
 * RDMO operation callback
 *
 * @request [in]: scatter request
 * @ucs_status [in]: operation status
 * @user_data [in]: user data
 */
static void urom_worker_rdmo_op_cb(void *request, ucs_status_t ucs_status, void *user_data)
{
	(void)ucs_status;

	doca_error_t status;
	struct urom_worker_rdmo_req *req = (struct urom_worker_rdmo_req *)user_data;

	status = req->ops->progress(req);
	if (status == DOCA_SUCCESS) {
		urom_worker_rdmo_req_free_data(req);
		urom_worker_rdmo_req_complete(req);
	}

	ucp_request_free(request);
}

/*
 * RDMO send operation callback
 *
 * @request [in]: scatter request
 * @ucs_status [in]: operation status
 * @user_data [in]: user data
 */
static void urom_worker_rdmo_op_send_cb(void *request, ucs_status_t ucs_status, void *user_data)
{
	urom_worker_rdmo_op_cb(request, ucs_status, user_data);
}

/*
 * RDMO receive data operation callback
 *
 * @request [in]: scatter request
 * @ucs_status [in]: operation status
 * @length [in]: data length
 * @user_data [in]: user data
 */
static void urom_worker_rdmo_am_recv_data_cb(void *request, ucs_status_t ucs_status, size_t length, void *user_data)
{
	(void)length;
	urom_worker_rdmo_op_cb(request, ucs_status, user_data);
}

/*
 * RDMO scatter operation callback
 *
 * @request [in]: scatter request
 * @ucs_status [in]: operation status
 * @user_data [in]: user data
 */
static void urom_worker_rdmo_scatter_op_send_cb(void *request, ucs_status_t ucs_status, void *user_data)
{
	struct urom_worker_rdmo_req *req __attribute__((unused)) = (struct urom_worker_rdmo_req *)user_data;

	req->ctx[1]--; /* pending completions */
	DOCA_LOG_DBG("Pending completions: %lu req: %p", req->ctx[1], req);
	urom_worker_rdmo_op_cb(request, ucs_status, user_data);
}

/*
 * Progress function for flush operations
 *
 * @req [in]: RDMO request
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_flush_progress(struct urom_worker_rdmo_req *req)
{
	ucs_status_t ucs_status;
	ucs_status_ptr_t ucs_status_ptr;
	ucp_request_param_t req_param = {
		.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
		.cb.send = urom_worker_rdmo_op_send_cb,
		.user_data = req,
	};
	const struct urom_rdmo_hdr *rdmo_hdr = (const struct urom_rdmo_hdr *)req->header;
	const struct urom_rdmo_flush_hdr *flush_hdr = (struct urom_rdmo_flush_hdr *)(rdmo_hdr + 1);
	struct urom_rdmo_rsp_hdr *rsp_hdr;
	struct urom_rdmo_flush_rsp_hdr *flush_rsp;
	size_t rsp_len = sizeof(*rsp_hdr) + sizeof(*flush_rsp);

	if (req->ctx[0] == 0) {
		/* Stage 1: flush RMA */
		/* Ensure payload is written before the queue pointer */
		ucs_status = ucp_worker_fence(req->client->rdmo_worker->ucp_data.ucp_worker);
		if (ucs_status != UCS_OK)
			return DOCA_ERROR_DRIVER;

		urom_worker_rdmo_mem_cache_flush(req->client);

		ucs_status_ptr = ucp_ep_flush_nbx(req->client->ep->ep, &req_param);
		if (UCS_PTR_IS_ERR(ucs_status_ptr))
			return DOCA_ERROR_DRIVER;

		req->ctx[0] = 1;

		if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_INPROGRESS) {
			DOCA_LOG_DBG("Initiated RMA flush, req: %p", req);
			return DOCA_ERROR_IN_PROGRESS;
		}

		DOCA_LOG_DBG("Completed RMA flush, req: %p", req);
		if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_OK)
			return DOCA_ERROR_DRIVER;

		/* Fall through */
	}

	if (req->ctx[0] == 1) {
		rsp_hdr = (struct urom_rdmo_rsp_hdr *)&req->ctx[1];
		flush_rsp = (struct urom_rdmo_flush_rsp_hdr *)(rsp_hdr + 1);

		rsp_hdr->rsp_id = UROM_RDMO_OP_FLUSH;
		flush_rsp->flush_id = flush_hdr->flush_id;

		/* Stage 2: send response to initiator */
		ucs_status_ptr =
			ucp_am_send_nbx(req->param.reply_ep, UROM_RDMO_AM_ID, rsp_hdr, rsp_len, NULL, 0, &req_param);
		if (UCS_PTR_IS_ERR(ucs_status_ptr))
			return DOCA_ERROR_DRIVER;

		req->ctx[0] = 2;

		if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_INPROGRESS) {
			DOCA_LOG_DBG("Initiated flush response, req: %p", req);
			return DOCA_ERROR_IN_PROGRESS;
		}
		if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_OK)
			return DOCA_ERROR_DRIVER;

		DOCA_LOG_DBG("Completed flush response, req: %p", req);

		/* Fall through */
	}

	/* Complete request */
	DOCA_LOG_DBG("Completed flush request: %p", req);

	return DOCA_SUCCESS;
}

/* RDMO flush operations */
static struct urom_worker_rdmo_req_ops urom_worker_rdmo_flush_ops = {
	.progress = urom_worker_rdmo_flush_progress,
};

/*
 * Progress function for append operations
 *
 * @req [in]: RDMO request
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_append_progress(struct urom_worker_rdmo_req *req)
{
	ucs_status_ptr_t ucs_status_ptr;
	ucp_request_param_t req_param;
	const struct urom_rdmo_hdr *rdmo_hdr = (const struct urom_rdmo_hdr *)req->header;
	const struct urom_rdmo_append_hdr *append_hdr = (struct urom_rdmo_append_hdr *)(rdmo_hdr + 1);
	khint_t k;
	struct urom_worker_rdmo_mkey *rdmo_mkey;
	ucp_rkey_h ucp_rkey;
	uint64_t *sm_addr;
	doca_error_t result = DOCA_SUCCESS;
	uint64_t *sm_ptr = NULL;

	if (req->ctx[0] == 0) {
		/* Stage 1: FADD */

		k = kh_get(mkey, req->client->mkeys, append_hdr->ptr_rkey);
		if (k == kh_end(req->client->mkeys)) {
			DOCA_LOG_ERR("Unknown ptr_rkey: %lu", append_hdr->ptr_rkey);
			return DOCA_SUCCESS;
		}

		rdmo_mkey = kh_value(req->client->mkeys, k);
		req->ctx[2] = (uint64_t)rdmo_mkey;
		ucp_rkey = rdmo_mkey->ucp_rkey;
		if ((uintptr_t)ucp_rkey != append_hdr->ptr_rkey)
			return DOCA_ERROR_INVALID_VALUE;

		if (ucp_rkey_ptr(ucp_rkey, append_hdr->ptr_addr, (void **)&sm_addr) == UCS_OK) {
			sm_ptr = sm_addr;
			req->ctx[1] = *sm_addr;
			req->ctx[0] = 2; /* Next: put */
			DOCA_LOG_DBG("Performed SM FADD, req: %p", req);
		} else if (urom_worker_rdmo_mem_cache_get(rdmo_mkey, append_hdr->ptr_addr, &req->ctx[1]) ==
			   DOCA_SUCCESS) {
			DOCA_LOG_DBG("Using cached pointer val: %#lx, req: %p", req->ctx[1], req);
			req->ctx[0] = 1; /* Next: cache update */
		} else {
			memset(&req_param, 0, sizeof(req_param));
			req_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
			req_param.cb.send = urom_worker_rdmo_op_send_cb;
			req_param.user_data = req;

			ucs_status_ptr = ucp_get_nbx(req->client->ep->ep,
						     &req->ctx[1],
						     8,
						     append_hdr->ptr_addr,
						     ucp_rkey,
						     &req_param);
			if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_INPROGRESS)
				return DOCA_ERROR_DRIVER;

			DOCA_LOG_DBG("Initiated Get, req: %p", req);
			req->ctx[0] = 1; /* Next: cache update */

			req->client->pause = 1; /* Prevent concurrent AMOs */

			return DOCA_ERROR_IN_PROGRESS;
		}
	}

	if (req->ctx[0] == 1) {
		/* Stage 2: update cache */
		rdmo_mkey = (struct urom_worker_rdmo_mkey *)req->ctx[2];
		result = urom_worker_rdmo_mem_cache_put(rdmo_mkey, append_hdr->ptr_addr, req->ctx[1] + req->length);
		if (result != DOCA_SUCCESS)
			return result;
		req->ctx[0] = 2; /* Next: put */
		req->client->pause = 0;
	}

	if (req->ctx[0] == 2) {
		/* Stage 3: Put */
		if (append_hdr->ptr_rkey != append_hdr->data_rkey) {
			k = kh_get(mkey, req->client->mkeys, append_hdr->data_rkey);
			if (k == kh_end(req->client->mkeys)) {
				DOCA_LOG_ERR("Unknown data_rkey: %lu", append_hdr->data_rkey);
				return DOCA_SUCCESS;
			}

			rdmo_mkey = kh_value(req->client->mkeys, k);
			ucp_rkey = rdmo_mkey->ucp_rkey;
		} else {
			rdmo_mkey = (struct urom_worker_rdmo_mkey *)req->ctx[2];
			ucp_rkey = rdmo_mkey->ucp_rkey;
		}

		if (req->ctx[1] < rdmo_mkey->va || (req->ctx[1] + req->length) > (rdmo_mkey->va + rdmo_mkey->len)) {
			DOCA_LOG_ERR("Append out of bounds, put: %#lx-%#lx mkey: %#lx-%#lx",
				     req->ctx[1],
				     req->ctx[1] + req->length,
				     rdmo_mkey->va,
				     rdmo_mkey->va + rdmo_mkey->len);
			return DOCA_ERROR_UNEXPECTED;
		}

		if (req->param.recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
			if (rdmo_mkey->ucp_memh == NULL) {
				DOCA_LOG_ERR("RNDV AM requires xGVMI support");
				return DOCA_ERROR_INVALID_VALUE;
			}

			memset(&req_param, 0, sizeof(req_param));
			req_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA |
						 UCP_OP_ATTR_FIELD_MEMH;
			req_param.cb.recv_am = urom_worker_rdmo_am_recv_data_cb;
			req_param.user_data = req;
			req_param.memh = rdmo_mkey->ucp_memh;

			ucs_status_ptr = ucp_am_recv_data_nbx(req->client->rdmo_worker->ucp_data.ucp_worker,
							      req->data,
							      (void *)req->ctx[1],
							      req->length,
							      &req_param);
			if (UCS_PTR_IS_ERR(ucs_status_ptr))
				return DOCA_ERROR_DRIVER;

			req->ctx[0] = 3;

			if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_INPROGRESS) {
				DOCA_LOG_DBG("Initiated Get to: %#lx len: %lu req %p", req->ctx[1], req->length, req);
				return DOCA_ERROR_IN_PROGRESS;
			}
			if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_OK)
				return DOCA_ERROR_DRIVER;

			DOCA_LOG_DBG("Completed Get, req: %p", req);
		} else if (ucp_rkey_ptr(ucp_rkey, req->ctx[1], (void **)&sm_addr) != UCS_OK) {
			memset(&req_param, 0, sizeof(req_param));
			req_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
			req_param.cb.send = urom_worker_rdmo_op_send_cb;
			req_param.user_data = req;

			/* Estimate ceiling benefit of Put aggregation */
			ucs_status_ptr = ucp_put_nbx(req->client->ep->ep,
						     req->data,
						     req->length,
						     req->ctx[1],
						     ucp_rkey,
						     &req_param);

			if (UCS_PTR_IS_ERR(ucs_status_ptr))
				return DOCA_ERROR_DRIVER;

			req->ctx[0] = 3;

			if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_INPROGRESS) {
				DOCA_LOG_DBG("Initiated Put to: %#lx len: %lu req %p", req->ctx[1], req->length, req);
				return DOCA_ERROR_IN_PROGRESS;
			}
			if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_OK)
				return DOCA_ERROR_DRIVER;

			DOCA_LOG_DBG("Completed Put, req: %p", req);
		} else {
			/* Buffer is in shared memory between client and urom_worker */
			memcpy((void *)sm_addr, req->data, req->length);
			urom_rdmo_compiler_fence();
			*sm_ptr += req->length;
			DOCA_LOG_DBG("Completed copy, req: %p", req);
		}

		/* Send complete, fall through to completion */
	}

	/* Stage 3: Put ack */
	DOCA_LOG_DBG("Completed Append request: %p", req);

	return DOCA_SUCCESS;
}

/* RDMO append operations */
static struct urom_worker_rdmo_req_ops urom_worker_rdmo_append_ops = {
	.progress = urom_worker_rdmo_append_progress,
};

/*
 * Progress function for scatter operations
 *
 * @req [in]: RDMO request
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t urom_worker_rdmo_scatter_progress(struct urom_worker_rdmo_req *req)
{
	ucs_status_ptr_t ucs_status_ptr;
	ucp_request_param_t req_param;
	const struct urom_rdmo_hdr *rdmo_hdr = (const struct urom_rdmo_hdr *)req->header;
	const struct urom_rdmo_scatter_hdr *scatter_hdr = (struct urom_rdmo_scatter_hdr *)(rdmo_hdr + 1);
	khint_t k;
	uint64_t prev_rkey = UINT64_MAX;
	struct urom_worker_rdmo_mkey *rdmo_mkey = NULL;
	ucp_rkey_h ucp_rkey = NULL;
	uint64_t *sm_addr;
	struct urom_rdmo_scatter_iov *iov;
	void *iov_data;
	uint64_t i;

	if (req->ctx[0] == 0) {
		if (req->param.recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
			DOCA_LOG_ERR("Rendezvous scatter not supported");
			return DOCA_ERROR_NOT_SUPPORTED;
		}

		/* Stage 2: Do Puts */
		iov = (struct urom_rdmo_scatter_iov *)req->data;
		iov_data = (void *)(iov + 1);

		for (i = 0; i < scatter_hdr->count; i++) {
			if (iov->rkey != prev_rkey) {
				k = kh_get(mkey, req->client->mkeys, iov->rkey);
				if (k == kh_end(req->client->mkeys)) {
					DOCA_LOG_ERR("Unknown rkey: %lu", iov->rkey);
					return DOCA_SUCCESS;
				}

				rdmo_mkey = kh_value(req->client->mkeys, k);
				ucp_rkey = rdmo_mkey->ucp_rkey;
				prev_rkey = iov->rkey;
			}

			if (iov->addr < rdmo_mkey->va || (iov->addr + iov->len) > (rdmo_mkey->va + rdmo_mkey->len)) {
				DOCA_LOG_ERR("Scatter IOV out of bounds, put: %#lx-%#lx mkey: %#lx-%#lx",
					     iov->addr,
					     iov->addr + iov->len,
					     rdmo_mkey->va,
					     rdmo_mkey->va + rdmo_mkey->len);
				return DOCA_ERROR_UNEXPECTED;
			}

			if (ucp_rkey_ptr(ucp_rkey, iov->addr, (void **)&sm_addr) != UCS_OK) {
				memset(&req_param, 0, sizeof(req_param));
				req_param.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA;
				req_param.cb.send = urom_worker_rdmo_scatter_op_send_cb;
				req_param.user_data = req;

				ucs_status_ptr = ucp_put_nbx(req->client->ep->ep,
							     iov_data,
							     iov->len,
							     iov->addr,
							     ucp_rkey,
							     &req_param);
				if (UCS_PTR_IS_ERR(ucs_status_ptr))
					return DOCA_ERROR_DRIVER;

				if (UCS_PTR_STATUS(ucs_status_ptr) == UCS_INPROGRESS) {
					DOCA_LOG_DBG("Initiated Put to: %#lx len: %u req %p", iov->addr, iov->len, req);
					req->ctx[1]++; /* Pending completion */
				} else {
					if (UCS_PTR_STATUS(ucs_status_ptr) != UCS_OK)
						return DOCA_ERROR_DRIVER;

					DOCA_LOG_DBG("Completed Scatter Put, req: %p", req);
				}
			} else {
				/* Buffer is in shared memory between client and urom_worker */
				memcpy((void *)sm_addr, iov_data, iov->len);

				DOCA_LOG_DBG("Completed copy, req: %p", req);
			}

			iov = (struct urom_rdmo_scatter_iov *)((uintptr_t)iov + sizeof(*iov) + iov->len);
			iov_data = (void *)(iov + 1);
		}

		/* all sends complete */
		req->ctx[0] = 1;
	}

	if (req->ctx[0] == 1) {
		/* Stage 3: Wait for all completions */
		if (req->ctx[1])
			return DOCA_ERROR_IN_PROGRESS;
	}

	DOCA_LOG_DBG("Completed Scatter request: %p", req);

	return DOCA_SUCCESS;
}

/* RDMO scatter operations */
static struct urom_worker_rdmo_req_ops urom_worker_rdmo_scatter_ops = {
	.progress = urom_worker_rdmo_scatter_progress,
};

/* RDMO worker requests operations */
struct urom_worker_rdmo_req_ops *urom_worker_rdmo_ops_table[] = {
	[UROM_RDMO_OP_FLUSH] = &urom_worker_rdmo_flush_ops,
	[UROM_RDMO_OP_APPEND] = &urom_worker_rdmo_append_ops,
	[UROM_RDMO_OP_SCATTER] = &urom_worker_rdmo_scatter_ops,
};

doca_error_t urom_worker_rdmo_req_queue(struct urom_worker_rdmo_req *req)
{
	struct urom_worker_rdmo_client *client = req->client;
	doca_error_t status;

	if (!ucs_list_is_empty(&client->paused_ops) || client->pause) {
		DOCA_LOG_DBG("New paused request: %p", req);
		ucs_list_add_tail(&client->paused_ops, &req->entry);
		return DOCA_ERROR_IN_PROGRESS;
	}

	status = urom_worker_rdmo_req_start(req);
	if (status == DOCA_SUCCESS)
		urom_worker_rdmo_req_free(req);

	return status;
}
