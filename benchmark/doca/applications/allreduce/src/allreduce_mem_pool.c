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

#include <stddef.h>
#include <stdlib.h>
#ifdef GPU_SUPPORT
#include <cuda_runtime_api.h>
#include <cuda.h>
#endif

#include <doca_log.h>

#include "allreduce_mem_pool.h"
#include "allreduce_utils.h"

DOCA_LOG_REGISTER(ALLREDUCE::MEM_POOL);

/* Add implementation to the wrapper methods of a specific memory pool */
#define DECLARE_MPOOL_WRAPPERS(name)					\
	doca_error_t allreduce_aloc_vec_##name(void **vec_p)		\
	{								\
		return allreduce_aloc_vec(&name, vec_p);		\
	}								\
	void allreduce_free_vec_##name(void *vec)			\
	{								\
		allreduce_free_vec(&name, vec);				\
	}								\
	void allreduce_free_vecs_##name(void **vecs, size_t n)		\
	{								\
		allreduce_free_vecs(&name, vecs, n);			\
	}								\
	doca_error_t allreduce_create_##name(size_t nb_elems, size_t elem_size, mem_type mtype)	\
	{											\
		return allreduce_create_mpool(&name, nb_elems, elem_size, mtype);		\
	}								\
	doca_error_t allreduce_destroy_##name(void)			\
	{								\
		return allreduce_destroy_mpool(&name, #name);		\
	}

/* Add implementation to the wrapper methods of a specific memory pool with a custom memory handlers */
#define DECLARE_GENERATOR_MPOOL_WRAPPERS(name, type)			\
	doca_error_t allreduce_aloc_vec_##name(type * *vec_p)		\
	{								\
		return allreduce_aloc_vec(&name, (void **) vec_p);	\
	}								\
	void allreduce_free_vec_##name(type *vec)			\
	{								\
		allreduce_free_vec(&name, (void *)vec);			\
	}								\
	void allreduce_free_vecs_##name(type **vecs, size_t n)		\
	{								\
		allreduce_free_vecs(&name, (void **)vecs, n);		\
	}								\
	doca_error_t allreduce_create_##name(size_t nb_elems, generator fgen, destructor fdes)	\
	{								\
		name.faloc = fgen;					\
		name.fdealoc = fdes;					\
		return allreduce_create_mpool(&name, nb_elems, 0, CUSTOM);			\
	}								\
	doca_error_t allreduce_destroy_##name(void)			\
	{								\
		return allreduce_destroy_mpool(&name, #name);		\
	}

struct mpool {
	/* members for ops on current pool */
	void **stack;		/* An array that holds all the free elements of the pool in a LRU order */
	size_t size;		/* Size of the "stack" array */
	size_t head;		/* index of the head of "stack", i.e., the most recently freed element  */
	/* members for modifying pool config */
	mem_type mtype;		/* The memory type of the elements */
	size_t elem_size;	/* The bytes size of each element */
	generator faloc;	/* Used only for "CUSTOM" memory type. Function to allocate elements into the pool */
	destructor fdealoc;	/* Used only for "CUSTOM" memory type. Function to free the memory of elements when
				 * memory pool is destroyed
				 */
};

/**** Inner logic functions ****/

/*
 * Allocates memory elements to the memory pool in the indices [start, end)
 *
 * @mpool [in]: Memory pool to hold the new allocations
 * @start [in]: First index in the mpool that is empty
 * @end [in]: First index in the mpool that is out of range or that isn't empty
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
_populate_mem_pool_range(struct mpool *mpool, size_t start, size_t end)
{
	size_t i = start;

	switch (mpool->mtype) {
	case CPU:
		for (; i < end; ++i) {
			mpool->stack[i] = malloc(mpool->elem_size);
			if (mpool->stack[i] == NULL)
				goto ERR;
		}
		break;
#ifdef GPU_SUPPORT
	case CUDA:
		for (; i < end; ++i) {
			if (cudaMalloc(&mpool->stack[i], mpool->elem_size) != cudaSuccess)
				goto ERR;
		}
		break;
	case CUDA_MANAGED:
		for (; i < end; ++i) {
			if (cudaMallocManaged(&mpool->stack[i], mpool->elem_size, cudaMemAttachGlobal) != cudaSuccess)
				goto ERR;
		}
		break;
#endif
	case CUSTOM:
		for (; i < end; ++i) {
			mpool->stack[i] = mpool->faloc();
			if (mpool->stack[i] == NULL)
				goto ERR;
		}
		break;
	default:
		DOCA_LOG_ERR("Memory allocation failed. Unknown type: %d (should be from 'enum mem_type')", mpool->mtype);
		return DOCA_ERROR_INVALID_VALUE;
	}

	return DOCA_SUCCESS;
ERR:
	mpool->size = i;
	DOCA_LOG_ERR("Not enough memory to run");
	return DOCA_ERROR_NO_MEMORY;
}

/*
 * Initialize the memory pool
 *
 * @mp [in]: Pointer to uninitialized memory pool
 * @nb_elems [in]: Number of elements to allocate in the memory pool
 * @elem_size [in]: The size of each element in bytes
 * @mtype [in]: Enum value indicating what type of memory should be allocated for the elements
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
allreduce_create_mpool(struct mpool *mp, size_t nb_elems, size_t elem_size, mem_type mtype)
{
	mp->stack = (void **)malloc(nb_elems * sizeof(*mp->stack));
	mp->size = nb_elems;
	mp->head = 0;
	mp->mtype = mtype;
	mp->elem_size = elem_size;

	if (mp->stack == NULL)
		return DOCA_ERROR_NO_MEMORY;

	return _populate_mem_pool_range(mp, 0, nb_elems);
}

/*
 * Deallocates the memory pool
 *
 * @mp [in]: Pointer to an initialized memory pool
 * @mp_name [in]: The memory pool name, used for better error messages
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static inline doca_error_t
allreduce_destroy_mpool(struct mpool *mp, const char *mp_name)
{
	size_t i = mp->head;

	if (mp->stack == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	if (mp->head > 0)
		DOCA_LOG_WARN("Mem pool %s is destroyed before all items return to it. Missing %lu items", mp_name, i);

	switch (mp->mtype) {
	case CPU:
		for (; i < mp->size; ++i)
			free(mp->stack[i]);
		break;
#ifdef GPU_SUPPORT
	case CUDA:
	case CUDA_MANAGED:
		for (; i < mp->size; ++i)
			cudaFree(mp->stack[i]);
		break;
#endif
	case CUSTOM:
		for (; i < mp->size; ++i)
			mp->fdealoc(mp->stack[i]);
		break;
	default:
		return DOCA_ERROR_INVALID_VALUE;
	}

	free(mp->stack);
	return DOCA_SUCCESS;
}

/*
 * Takes out an element from the memory pool and return it
 *
 * @mp [in]: Pointer to an initialized memory pool
 * @vec_p [out]: Pointer to a location to store a memory buffer taken out of the memory pool
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 *
 * @NOTE: if the pool is empty, this function will dynamically increase the pool and display a warning in the LOG
 */
static inline doca_error_t
allreduce_aloc_vec(struct mpool *mp, void **vec_p)
{
	if (__builtin_expect(mp->head == mp->size, 0)) {
		DOCA_LOG_WARN("Insufficient space was allocated in a memory pool. Dynamic growth is expensive");
		void **tmp = (void **) realloc(mp->stack, 2 * mp->size * sizeof(*mp->stack));

		if (tmp == NULL) {
			DOCA_LOG_ERR("Memory allocation failed");
			return DOCA_ERROR_NO_MEMORY;
		}
		mp->size *= 2;
		mp->stack = tmp;

		doca_error_t result = _populate_mem_pool_range(mp, mp->head, mp->size);

		if (result != DOCA_SUCCESS)
			return result;  /* Return here to avoid changing the head */
	}

	*vec_p = mp->stack[mp->head++];
	return DOCA_SUCCESS;
}

/*
 * Returns a vector to the pool
 *
 * @mp [in]: Pointer to an initialized memory pool
 * @vec [in]: Pointer to an element that was taken out of the pool
 */
static inline void
allreduce_free_vec(struct mpool *mp, void *vec)
{
	--mp->head;
	mp->stack[mp->head] = vec;
}

/*
 * Returns a batch of vectors to the pool
 *
 * @mp [in]: Pointer to an initialized memory pool
 * @vecs [in]: Pointer to an array of elements that were taken out of the pool
 * @n [in]: Number of elements in the given array
 */
static inline void
allreduce_free_vecs(struct mpool *mp, void **vecs, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		--mp->head;
		mp->stack[mp->head] = vecs[i];
	}
}

/**** Definition of the supported memory pools and thier exported functions ****/

static struct mpool vecs_pool;
static struct mpool bufs_pool;
static struct mpool reqs_pool;
static struct mpool super_reqs_pool;

DECLARE_MPOOL_WRAPPERS(vecs_pool);
DECLARE_MPOOL_WRAPPERS(bufs_pool);
DECLARE_MPOOL_WRAPPERS(reqs_pool);
DECLARE_MPOOL_WRAPPERS(super_reqs_pool);
#ifdef GPU_SUPPORT
static struct mpool streams_pool;
static struct mpool clients_bufs_pool;

DECLARE_MPOOL_WRAPPERS(clients_bufs_pool);
DECLARE_GENERATOR_MPOOL_WRAPPERS(streams_pool, cudaStream_t);
#endif
