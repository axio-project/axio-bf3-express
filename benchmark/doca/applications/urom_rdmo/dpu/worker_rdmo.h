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

#ifndef WORKER_RDMO_H_
#define WORKER_RDMO_H_

#include <doca_log.h>
#include <doca_error.h>

#include <ucp/api/ucp.h>
#include <ucs/datastruct/khash.h>
#include <ucs/datastruct/list.h>
#include <ucs/datastruct/mpool.h>

#include <doca_urom_plugin.h>

#define UROM_RDMO_AM_ID 0	 /* RDMO AM id */
#define UROM_RDMO_HDR_LEN_MAX 64 /* Must fit struct urom_rdmo_hdr and urom_rdmo_xxx_hdr_t */

/* RDMO UCP data structure */
struct ucp_data {
	ucp_context_h ucp_context;     /* UCP context */
	ucp_worker_h ucp_worker;       /* UCP worker instance */
	ucp_address_t *worker_address; /* UCP worker address */
	size_t ucp_addrlen;	       /* UCP worker address length */
};

/* RDMO EP structure */
struct urom_worker_rdmo_ep {
	ucp_ep_h ep;		    /* UCP ep structure */
	uint64_t peer_uid;	    /* Peer id */
	int ref_cnt;		    /* Reference counter */
	ucs_list_link_t fenced_ops; /* Fenced ops list */
	int oreqs;		    /* Operation requests number */
};

/* Init RDMO endpoints UCX map */
KHASH_MAP_INIT_INT64(ep, struct urom_worker_rdmo_ep *);

/* RDMO RQ structure */
struct urom_worker_rdmo_rq {
	uint64_t id;			/* RQ id */
	struct urom_worker_rdmo_ep *ep; /* RQ's end-point */
};

/* Init RDMO RQs map */
KHASH_MAP_INIT_INT64(rq, struct urom_worker_rdmo_rq *);

/* Init RDMO RQs memory cache */
KHASH_MAP_INIT_INT64(mem_cache, uint64_t);

/* RDMO memory key structure */
struct urom_worker_rdmo_mkey {
	ucp_rkey_h ucp_rkey;		/* UCP remote key */
	ucp_mem_h ucp_memh;		/* UCP memory handle */
	khash_t(mem_cache) * mem_cache; /* Memory cache */
	uint64_t va;			/* Host memory address */
	size_t len;			/* Data length */
};

/* Init RDMO mkey map */
KHASH_MAP_INIT_INT64(mkey, struct urom_worker_rdmo_mkey *);

/* RDMO client structure */
struct urom_worker_rdmo_client {
	struct urom_worker_rdmo *rdmo_worker; /* RDMO worker context */
	uint64_t id;			      /* Client id */
	uint64_t dest_id;		      /* Destination id */
	struct urom_worker_rdmo_ep *ep;	      /* Host memory access EP */
	khash_t(rq) * rqs;		      /* Initiator connections */
	uint64_t next_rq_id;		      /* Next request id */
	khash_t(mkey) * mkeys;		      /* Registered memory regions */
	ucs_list_link_t paused_ops;	      /* Paused operations list */
	int pause;			      /* If client is paused */
	uint64_t get_result;		      /* Client result */
};

/* Init RDMO clients map */
KHASH_MAP_INIT_INT64(client, struct urom_worker_rdmo_client *);

/* RDMO request structure */
struct urom_worker_rdmo_req;

/* RDMO request ops structure */
struct urom_worker_rdmo_req_ops {
	doca_error_t (*progress)(struct urom_worker_rdmo_req *req); /* RDMO request's progress handler */
};

/* RDMO request structure */
struct urom_worker_rdmo_req {
	ucs_list_link_t entry;			/* Request entry in the list */
	struct urom_worker_rdmo_client *client; /* RDMO client */
	struct urom_worker_rdmo_ep *ep;		/* RDMO endpoint */
	/* AM */
	uint8_t header[UROM_RDMO_HDR_LEN_MAX]; /* RDMO header data */
	void *data;			       /* RDMO data */
	uint64_t length;		       /* RDMO data length */
	ucp_am_recv_param_t param;	       /* UCP recv parameters */
	struct urom_worker_rdmo_req_ops *ops;  /* RDMO ops */
	uint64_t ctx[4];		       /* Request context */
};

/* UROM RDMO worker context structure */
struct urom_worker_rdmo {
	struct ucp_data ucp_data;	/* UCP data structure */
	ucs_mpool_t req_mp;		/* Requests memory map */
	khash_t(client) * clients;	/* local client connections */
	khash_t(ep) * eps;		/* Peer endpoints */
	ucs_list_link_t completed_reqs; /* RDMO worker commands completion list */
};

/* RDMO worker requests operations */
extern struct urom_worker_rdmo_req_ops *urom_worker_rdmo_ops_table[];

/*
 * Handle request in client RDMO queue
 *
 * @req [in]: Client RDMO request
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t urom_worker_rdmo_req_queue(struct urom_worker_rdmo_req *req);

/* UROM RDMO worker interface */
struct urom_worker_rdmo_iface {
	struct urom_plugin_iface super; /* DOCA UROM worker plugin interface */
};

/*
 * Get DOCA worker plugin interface for RDMO plugin.
 * DOCA UROM worker will load the urom_plugin_get_iface symbol to get the RDMO interface
 *
 * @iface [out]: Set DOCA UROM plugin interface for RDMO
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t urom_plugin_get_iface(struct urom_plugin_iface *iface);

/*
 * Get RDMO plugin version, will be used to verify that the host and DPU plugin versions are compatible
 *
 * @version [out]: Set the RDMO worker plugin version
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t urom_plugin_get_version(uint64_t *version);

#endif /* WORKER_RDMO_H_ */
