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

#ifndef ALLREDUCE_MEM_POOL_H_
#define ALLREDUCE_MEM_POOL_H_

#include <stddef.h>
#include <stdbool.h>

#include <doca_error.h>

#ifdef __CUDACC__
extern "C" {
#endif

typedef enum memory_types {
	CPU,		/* Memory allocation should be on the CPU memory using malloc */
	CUDA,		/* Memory allocation should be on the GPU memory using cudaMalloc */
	CUDA_MANAGED,	/* Memory allocation should be on a shared memory page of GPU and CPU using cudaMallocManaged */
	CUSTOM,		/* Memory allocation and deallocation should be done with custom functions */
} mem_type;

/*
 * Pointer type a custom memory allocation function
 */
typedef void *(*generator)(void);
/*
 * Pointer type a custom memory deallocation function
 */
typedef void (*destructor)(void *);

/* Declares the methods of a specific memory pool */
#define DECLARE_MPOOL_METHODS(name) \
	doca_error_t allreduce_aloc_vec_##name(void **vec_p); \
	void allreduce_free_vec_##name(void *vec); \
	void allreduce_free_vecs_##name(void **vecs, size_t n); \
	doca_error_t allreduce_create_##name(size_t nb_elems, size_t elem_size, mem_type mtype); \
	doca_error_t allreduce_destroy_##name(void)

/* Declares the methods of a specific memory pool with a custom memory handlers */
#define DECLARE_GENERATOR_MPOOL_METHODS(name, type) \
	doca_error_t allreduce_aloc_vec_##name(type **vec_p); \
	void allreduce_free_vec_##name(type *vec); \
	void allreduce_free_vecs_##name(type **vecs, size_t n); \
	doca_error_t allreduce_create_##name(size_t nb_elems, generator, destructor); \
	doca_error_t allreduce_destroy_##name(void)

/* Declare methods for each exisitng memory pool  */
DECLARE_MPOOL_METHODS(vecs_pool);
DECLARE_MPOOL_METHODS(bufs_pool);
DECLARE_MPOOL_METHODS(reqs_pool);
DECLARE_MPOOL_METHODS(super_reqs_pool);
#ifdef GPU_SUPPORT
DECLARE_GENERATOR_MPOOL_METHODS(streams_pool, cudaStream_t);
DECLARE_MPOOL_METHODS(clients_bufs_pool);
#endif

#ifdef __CUDACC__
}
#endif

#endif /* ALLREDUCE_MEM_POOL_H_ */
