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

#ifndef ALLREDUCE_REDUCER_H_
#define ALLREDUCE_REDUCER_H_

#include <stddef.h>

#ifdef GPU_SUPPORT
/* Disable glib warnings */
#define gnu_printf printf
#include <cuda_runtime_api.h>
#include <cuda.h>
#endif  /* GPU_SUPPORT */

#include "allreduce_core.h"

#ifdef __CUDACC__
extern "C" {
#endif

#ifdef GPU_SUPPORT
/*
 * Execute "stmt" and exit if it returned a CUDA error
 */
#define CUDA_ASSERT(stmt)                                                                                               \
	{                                                                                                               \
		cudaError_t result = (stmt);                                                                            \
		if (cudaSuccess != result)                                                                              \
			ALLREDUCE_EXIT("Cuda error %s: %s", cudaGetErrorName(result), cudaGetErrorString(result));      \
	}

/*
 * Sets GPU related globals for proper behavior of the code
 */
void set_cuda_globals(void);
#endif  /* GPU_SUPPORT */

/*
 * Reduces a single vector with the appropriate result vector in "allreduce_super_request", saving the outcome
 * into the result vector
 *
 * @allreduce_super_request [in]: The super-request with a result vector or a peers result vector
 * @src_vec [in]: The vector to be reduced
 * @is_peer [in]: Whether the input vector should be reduced with "peer_result_vector" instead of "result_vector"
 */
void allreduce_reduce(struct allreduce_super_request *allreduce_super_request, void *src_vec, bool is_peer);

/*
 * Reduces all clients/peers vectors with the appropriate result vector in "allreduce_super_request", saving the outcome
 * into the result vector
 *
 * @allreduce_super_request [in]: The super-request with valid result_vector and clients_recv_vectors buffer, or valid
 *				  peer_result_vector and recv_vectors
 * @is_peers [in]: Whether to reduce the peers-vectors with "peer_result_vector" instead of clients-vectors with
 *		   "result_vector"
 */
void allreduce_reduce_all(struct allreduce_super_request *allreduce_super_request, bool is_peers);

#ifdef __CUDACC__
}
#endif

#endif /* ALLREDUCE_REDUCER_H_ */
