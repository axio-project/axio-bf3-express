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
#include "allreduce_reducer.h"

DOCA_LOG_REGISTER(ALLREDUCE::Reducer::CPU);

/*
 * Function that gets dst and src vector, and the vectors datatype,
 * then produce them into dst.
 */
typedef void (*prod_func)(void *, void *, size_t, enum allreduce_datatype);

/*
 * Sums the vectors into dst_vector
 *
 * @dst_vector [in]: Array of numbers
 * @src_vector [in]: Array of numbers
 * @length [in]: Number of elements in each vectors
 * @datatype [in]: Identify the type of the vectors elements
 */
static inline void
summation(void *dst_vector, void *src_vector, size_t length, enum allreduce_datatype datatype)
{
	size_t i;

	switch (datatype) {
	case ALLREDUCE_BYTE:
		for (i = 0; i < length; ++i)
			((uint8_t *)dst_vector)[i] += ((uint8_t *)src_vector)[i];
		break;
	case ALLREDUCE_INT:
		for (i = 0; i < length; ++i)
			((int *)dst_vector)[i] += ((int *)src_vector)[i];
		break;
	case ALLREDUCE_FLOAT:
		for (i = 0; i < length; ++i)
			((float *)dst_vector)[i] += ((float *)src_vector)[i];
		break;
	case ALLREDUCE_DOUBLE:
		for (i = 0; i < length; ++i)
			((double *)dst_vector)[i] += ((double *)src_vector)[i];
		break;
	}
}

/*
 * Multiply the vectors into dst_vector
 *
 * @dst_vector [in]: Array of numbers
 * @src_vector [in]: Array of numbers
 * @length [in]: Number of elements in each vectors
 * @datatype [in]: Identify the type of the vectors elements
 */
static inline void
product(void *dst_vector, void *src_vector, size_t length, enum allreduce_datatype datatype)
{
	size_t i;

	switch (datatype) {
	case ALLREDUCE_BYTE:
		for (i = 0; i < length; ++i)
			((uint8_t *)dst_vector)[i] *= ((uint8_t *)src_vector)[i];
		break;
	case ALLREDUCE_INT:
		for (i = 0; i < length; ++i)
			((int *)dst_vector)[i] *= ((int *)src_vector)[i];
		break;
	case ALLREDUCE_FLOAT:
		for (i = 0; i < length; i++)
			((float *)dst_vector)[i] *= ((float *)src_vector)[i];
		break;
	case ALLREDUCE_DOUBLE:
		for (i = 0; i < length; ++i)
			((double *)dst_vector)[i] *= ((double *)src_vector)[i];
		break;
	}
}

void
allreduce_reduce(struct allreduce_super_request *allreduce_super_request, void *src_vec, bool is_peer)
{
	void *dst_vec = is_peer ? allreduce_super_request->peer_result_vector : allreduce_super_request->result_vector;
	size_t dst_vec_len = allreduce_super_request->result_vector_size;

	if (ucs_unlikely(dst_vec_len == 0))
		return;

	switch (allreduce_config.operation) {
	case ALLREDUCE_SUM:
		summation(dst_vec, src_vec, dst_vec_len, allreduce_config.datatype);
		break;
	case ALLREDUCE_PROD:
		product(dst_vec, src_vec, dst_vec_len, allreduce_config.datatype);
		break;
	default:
		DOCA_LOG_ERR("Unknown operation was requested: %d", allreduce_config.operation);
	}
}

void
allreduce_reduce_all(struct allreduce_super_request *allreduce_super_request, bool is_peers)
{
	size_t i;
	size_t n;
	void **vectors = allreduce_super_request->recv_vectors;

	if (is_peers)
		n = allreduce_super_request->recv_vector_iter;
	else if (allreduce_super_request->result_vector_owner) /* If the result vector was taken from a client */
		n = allreduce_config.num_clients - 1;
	else
		n = allreduce_config.num_clients;

	for (i = 0; i < n; ++i)
		allreduce_reduce(allreduce_super_request, vectors[i], is_peers);
}
