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
#include <cuda_profiler_api.h>
#include <list>

#include <doca_log.h>

#include "allreduce_reducer.h"

#define CEIL_DEV(n, divisor) (((n) + (divisor) - 1) / (divisor)) /* Round-up integers division */
#define MIN_VEC_LEN_PER_THREAD 48 /* Should always be above 32 for best performance \
				   * Using 48 because of rounding up errors */

DOCA_LOG_REGISTER(ALLREDUCE::Reducer::GPU);

/*
 * Function that gets dst and src vector, start and end indices, step size then produces
 * the vectors into dst from "start" (inclusive) to "end" (exclusive) every "step"-th index
 */
typedef void (*prod_func)(void *, void *, size_t, size_t, size_t);

struct vectors {
	union {
		void **arr;	/* if n > 1, pointer to GPU memory containing pointers to n vectors (in GPU memory) */
		void *vec;	/* if n is 1, pointer to GPU memory containing the single vector */
	};
	size_t n;	/* Number of vectors in struct, determines union */
};

static struct {
	int dev_id;			/* GPU device ID */
	int max_thrds_per_blk;		/* Maximum supported number of threads in a single block */
	int max_grid_dim_x;		/* Maximum X dimension of a kernel grid */
	int max_grid_dim_y;		/* Maximum Y dimension of a kernel grid */
	int warp_size;			/* Number of parallel processing units in a single GPU multiprocessor */
} gpu_info;

__constant__ enum allreduce_operation operation;
__constant__ enum allreduce_datatype datatype;

/*
 * Sums the vectors into dst_vector from "from" (inclusive) to "to" (exclusive) every "step"-th index
 *
 * @dst_vector [in]: CUDA memory that holds an array of numbers
 * @src_vector [in]: CUDA memory that holds an array of numbers
 * @from [in]: Index to start from the summation process (inclusive)
 * @to [in]: Index to stop the summation process when reached (exclusive)
 * @step [in]: The step between two consecutive indexes
 */
__device__ static void
gpu_summation(void *dst_vector, void *src_vector, size_t from, size_t to, size_t step)
{
	size_t i = from;

	switch (datatype) {
	case ALLREDUCE_BYTE:
		for (; i < to; i += step)
			((uint8_t *)dst_vector)[i] += ((uint8_t *)src_vector)[i];
		break;
	case ALLREDUCE_INT:
		for (; i < to; i += step)
			((int *)dst_vector)[i] += ((int *)src_vector)[i];
		break;
	case ALLREDUCE_FLOAT:
		for (; i < to; i += step)
			((float *)dst_vector)[i] += ((float *)src_vector)[i];
		break;
	case ALLREDUCE_DOUBLE:
		for (; i < to; i += step)
			((double *)dst_vector)[i] += ((double *)src_vector)[i];
		break;
	}
}

/*
 * Multiply the vectors element-element into dst_vector from "from" (inclusive) to "to" (exclusive) every "step"-th index
 *
 * @dst_vector [in]: CUDA memory that holds an array of numbers
 * @src_vector [in]: CUDA memory that holds an array of numbers
 * @from [in]: Index to start from the product process (inclusive)
 * @to [in]: Index to stop the product process when reached (exclusive)
 * @step [in]: The step between two consecutive indexes
 */
__device__ static void
gpu_product(void *dst_vector, void *src_vector, size_t from, size_t to, size_t step)
{
	size_t i = from;

	switch (datatype) {
	case ALLREDUCE_BYTE:
		for (; i < to; i += step)
			((uint8_t *)dst_vector)[i] *= ((uint8_t *)src_vector)[i];
		break;
	case ALLREDUCE_INT:
		for (; i < to; i += step)
			((int *)dst_vector)[i] *= ((int *)src_vector)[i];
		break;
	case ALLREDUCE_FLOAT:
		for (; i < to; i += step)
			((float *)dst_vector)[i] *= ((float *)src_vector)[i];
		break;
	case ALLREDUCE_DOUBLE:
		for (; i < to; i += step)
			((double *)dst_vector)[i] *= ((double *)src_vector)[i];
		break;
	}
}

/*
 * Iterativly reduces the vectors with dst_vec on the GPU.
 * Every CUDA block operates on all vectors, but only in a specific index range
 *
 * Complexity: O( m * max(MIN_VEC_LEN_PER_THREAD, n/(B*T)) ) where n is vec_len, m is nb_vecs,
 * B is the number of blocks in the grid, T is the number of threads per block.
 *
 * @dst_vec [in]: CUDA memory that holds an array of numbers
 * @src [in]: Holds 1 or more vectors to be reduced with the dst_vec
 * @vec_len [in]: The length of all the vectors, in "datatype" units
 */
__global__ static void
_gpu_reduce(void *dst_vec, struct vectors src, size_t vec_len)
{
	size_t start, block_sub_vec_len, end;
	prod_func gpu_apply;
	size_t i;

	/* Choose reduce process */
	switch (operation) {
	case ALLREDUCE_SUM:
		gpu_apply = gpu_summation;
		break;
	case ALLREDUCE_PROD:
		gpu_apply = gpu_product;
		break;
	default:
		/* Can never happen, initialization check this value is a valid enum */
		return;
	}

	/* Calculate the number of elements each block reduces */
	block_sub_vec_len = CEIL_DEV(vec_len, gridDim.x * gridDim.y);
	/* Calculate the offset of the sub vector this block should work on */
	start = (blockIdx.x * gridDim.y + blockIdx.y) * block_sub_vec_len;
	end = start + block_sub_vec_len;
	/* Offset block start to thread start */
	start += threadIdx.x;
	if (start >= vec_len)
		return;  /* Can happen only in the 2D dim case and for very small vectors */
	/* True only for the last block, set end boundary with regard to real length */
	if (end > vec_len)
		end = vec_len;

	/* Invoke reduce process */
	if (src.n > 1)
		for (i = 0; i < src.n; ++i) {
			gpu_apply(dst_vec, src.arr[i], start, end, blockDim.x);
		}
	else
		gpu_apply(dst_vec, src.vec, start, end, blockDim.x);
}

/*
 * Launches a CUDA kernel that iterativly reduces the vectors with dst_vec on the GPU.
 * Every CUDA block operates on all vectors, but only in a specific index range
 *
 * Complexity: O( m * max(MIN_VEC_LEN_PER_THREAD, n/(B*T)) ) where n is vec_len, m is nb_vecs,
 * B is the number of blocks in the grid, T is the number of threads per block.
 *
 * @dst_vec [in]: CUDA memory that holds an array of numbers
 * @src [in]: Holds 1 or more vectors to be reduced with the dst_vec
 * @vec_len [in]: The length of all the vectors, in "datatype" units
 * @stream [in]: Launch the kernel with this CUDA stream
 */
static inline void
gpu_reduce(void *dst_vec, struct vectors src, size_t vec_len, cudaStream_t stream)
{
	dim3 dim;
	size_t nb_warps;
	size_t opt_nb_blks;
	const int recommended_nb_of_warps = 4;

	/* Choosing the parameters for the GPU */
	opt_nb_blks = CEIL_DEV(vec_len, MIN_VEC_LEN_PER_THREAD * gpu_info.warp_size * recommended_nb_of_warps);
	if (opt_nb_blks <= gpu_info.max_grid_dim_x) {
		dim.x = opt_nb_blks;
		nb_warps = recommended_nb_of_warps;
	} else if (opt_nb_blks <= gpu_info.max_grid_dim_x * gpu_info.max_grid_dim_y) {
		/* Prefer division by the lower limit, to problisticly minimaize the number of extra blocks
		 * that are out of range */
		dim.y = gpu_info.max_grid_dim_y;
		dim.x = CEIL_DEV(vec_len, gpu_info.max_grid_dim_y * MIN_VEC_LEN_PER_THREAD * gpu_info.warp_size *
						  recommended_nb_of_warps);
		nb_warps = recommended_nb_of_warps;
	} else {
		dim.x = gpu_info.max_grid_dim_x;
		dim.y = gpu_info.max_grid_dim_y;
		if (vec_len < dim.x * dim.y * gpu_info.max_thrds_per_blk * MIN_VEC_LEN_PER_THREAD) {
			/* "vec_len / (dim.x * dim.y * MIN_VEC_LEN_PER_THREAD)" is the optimal number of threads if
			 * warp size wasn't a concern. So we choose the largest number of threads that is divisible by
			 * warp_size (effictivly increasing the work of each thread to above MIN_VEC_LEN_PER_THREAD) */
			nb_warps = CEIL_DEV(vec_len, dim.x * dim.y * MIN_VEC_LEN_PER_THREAD) / gpu_info.warp_size;
		}
		else
			nb_warps = gpu_info.max_thrds_per_blk / gpu_info.warp_size;
	}

	/* Launching CUDA kernel */
	_gpu_reduce<<<dim, nb_warps * gpu_info.warp_size, 0, stream>>>(dst_vec, src, vec_len);
}

/*
 * Reduces all the vectors will dst_vec, saving the result into dst_vec.
 * Every block operates on all vectors in the given range.
 * Reduction is performed in iterations, where each thread reduce two vectors leaving only half of the vectors to the
 * next iteration.
 *
 * Complexity: O( n * log(m) ) where n is "end - start" and m is the number of vectors.
 *
 * @dst_vec [in]: CUDA memory that holds an array of numbers
 * @vectors [in]: Vectors to be reduced with the dst_vec
 * @start [in]: Index to start from the reduce process (inclusive)
 * @end [in]: Index to stop the reduce process when reached (exclusive)
 *
 * @NOTE: "blockDim.x" MUST be equal to half the number of vectors (excluding dst_vec)
 * @NOTE: all vectors are modified by this functions
 */
__device__ static void
_reduce_all_for_many_vecs_algo(void *dst_vec, void **vectors, size_t start, size_t end)
{
	size_t lvec_idx = 2 * threadIdx.x;
	size_t rvec_idx = lvec_idx + 1;
	size_t min_rvec_idx = 1;  /* The minimum rvec_idx a threadblock holds */
	prod_func gpu_apply;

	switch (operation)
	{
	case ALLREDUCE_SUM:
		gpu_apply = gpu_summation;
		break;
	case ALLREDUCE_PROD:
		gpu_apply = gpu_product;
		break;
	default:
		/* Can never happen, initialization check this value is a valid enum */
		break;
	}

	/* Sum the array - correctness of the algorithm is proven with induction */
	while (min_rvec_idx < (2 * blockDim.x + 1)) {
		if (rvec_idx < (2 * blockDim.x + 1)) {
			gpu_apply(vectors[lvec_idx], vectors[rvec_idx], start, end, 1);
			lvec_idx *= 2;
			rvec_idx *= 2;
		}
		min_rvec_idx *= 2;
		__syncthreads();
	}

	/* Add result to dst */
	start += threadIdx.x;
	if (start < end)
		gpu_apply(dst_vec, vectors[0], start, end, blockDim.x);
}

/*
 * Reduces all the vectors will dst_vec, saving the result into dst_vec.
 * Every block operates on all vectors, but only in a specific index range.
 * All threads in the same block performs reduction in iterations, where each thread reduce two vectors leaving
 * only half of the vectors to the next iteration. When the result is computed - the block continue to process
 * the next "2 * blockDim.x + 1" vectors.
 *
 * Complexity: O( m/T * log(T) * max(MIN_VEC_LEN_PER_THREAD, n/B) )
 * 	       where n is vec_len, m is nb_vecs, B is the number of blocks in the grid, T is min(m, 2 * blockDim.x + 1)
 *
 * @dst_vec [in]: CUDA memory that holds an array of numbers
 * @vectors [in]: Vectors to be reduced with the dst_vec
 * @nb_vecs [in]: The number of vectors in "vectors"
 * @vec_len [in]: The length of all the vectors, in "datatype" units
 *
 * @NOTE: Number of vectors MUST be a whole multiplication of "2 * blockDim.x + 1" (excluding dst_vec)
 * @NOTE: all vectors are modified by this functions
 */
__global__ static void
_reduce_all_for_many_vecs(void *dst_vec, void **vectors, size_t nb_vecs, size_t vec_len)
{
	size_t per_thread_seg_len = CEIL_DEV(vec_len, gridDim.x * gridDim.y);
	size_t start = blockIdx.x * gridDim.y * per_thread_seg_len + blockIdx.y * per_thread_seg_len;
	size_t end = start + per_thread_seg_len;

	if (start >= vec_len)
		return;  /* Can happen only in the 2D dim case */
	if (end > vec_len)
		end = vec_len;

	/* Each iteration sums 2*blockDim.x+1 vectors and the dst_vector. */
	void **end_vecs = vectors + nb_vecs - nb_vecs % (2 * blockDim.x + 1);
	do {
		_reduce_all_for_many_vecs_algo(dst_vec, vectors, start, end);
		vectors += (2 * blockDim.x + 1);
	} while (vectors < end_vecs);
}

/*
 * Reduces all the vectors will dst_vec, saving the result into dst_vec. Using a specific algorithm that takes
 * advantage of the parallelism of the GPU to reduce multiple vectors at the same time.
 *
 * Complexity: O( m/T * log(T) * max(MIN_VEC_LEN_PER_THREAD, n/B) )
 * 	       where n is vec_len, m is nb_vecs, B is the maximum possible number of X blocks in a grid,
 * 	       and T is min(nb_vecs, 2 * "maximum possible number of threads in a block" + 1)
 *
 * @dst_vec [in]: CUDA memory that holds an array of numbers
 * @vectors [in]: Vectors to be reduced with the dst_vec
 * @nb_vecs [in]: The number of vectors in "vectors"
 * @vec_len [in]: The length of all the vectors, in "datatype" units
 * @stream [in]: CUDA stream for async launch of the GPU kernel
 *
 * @NOTE: all vectors are modified by this functions
 */
static void
reduce_all_for_many_vecs(void *dst_vec, void **vectors, size_t nb_vecs, size_t vec_len, cudaStream_t stream)
{
	dim3 dim;
	size_t nb_leftover_vecs, opt_nb_blks;

	/* Choosing the parameters for the GPU */
	opt_nb_blks = CEIL_DEV(vec_len, MIN_VEC_LEN_PER_THREAD);
	if (opt_nb_blks <= gpu_info.max_grid_dim_x) {
		dim.x = opt_nb_blks;
	} else {
		/* Prefer division by the lower limit, to problisticly minimaize the number of extra blocks
		 * that are out of range */
		dim.y = gpu_info.max_grid_dim_y;
		opt_nb_blks = CEIL_DEV(vec_len, MIN_VEC_LEN_PER_THREAD * gpu_info.max_grid_dim_y);
		dim.x = (opt_nb_blks <= gpu_info.max_grid_dim_x) ? opt_nb_blks : gpu_info.max_grid_dim_x;
	}

	/* Launching CUDA kernel */
	/* If cannot reduce all vectors using a single kernel, launch max threads to reduce as much as can */
	if (ucs_unlikely(nb_vecs > 2 * gpu_info.max_thrds_per_blk + 1)) {
		/* Can reduce up to 2*gpu_info.max_thrds_per_blk+1 vectors using a single call to the function */
		nb_leftover_vecs = nb_vecs % (2 * gpu_info.max_thrds_per_blk + 1);
		_reduce_all_for_many_vecs<<<dim, gpu_info.max_thrds_per_blk, 0, stream>>>(
			dst_vec, vectors, nb_vecs - nb_leftover_vecs, vec_len);
	} else
		nb_leftover_vecs = nb_vecs;
	/* Reduce all remaining vectors */
	if (nb_leftover_vecs > 1) {
		_reduce_all_for_many_vecs<<<dim, nb_leftover_vecs / 2, 0, stream>>>(
			dst_vec, vectors + (nb_vecs - nb_leftover_vecs), nb_leftover_vecs, vec_len);
	} else if (nb_leftover_vecs == 1) {
		struct vectors src = {};
		src.vec = vectors[nb_vecs - 1];
		src.n = 1;
		gpu_reduce(dst_vec, src, vec_len, stream);
	}
}

/***** Exported C functions *****/

void
allreduce_reduce_all(struct allreduce_super_request *allreduce_super_request, bool is_peers)
{
	void *dst_vec;
	void **src_vecs;
	size_t nb_vecs;
	size_t vec_len = allreduce_super_request->result_vector_size;

	if (ucs_unlikely(vec_len == 0))
		return;

	if (is_peers) {
		dst_vec = allreduce_super_request->peer_result_vector;
		src_vecs = allreduce_super_request->recv_vectors;
		nb_vecs = allreduce_super_request->recv_vector_iter;
	} else {
		dst_vec = allreduce_super_request->result_vector;
		src_vecs = allreduce_super_request->clients_recv_vectors;
		nb_vecs = allreduce_config.num_clients;
		/* If the result vector was taken from a client */
		if (allreduce_super_request->result_vector_owner)
			--nb_vecs;
	}

	/* Threshold is set at the intersection point of the complexity functions, also we want at least 4 vecs. */
	const size_t x = (nb_vecs <= 2 * gpu_info.max_thrds_per_blk) ? nb_vecs : 2 * gpu_info.max_thrds_per_blk;
	const size_t y = x * ((MIN_VEC_LEN_PER_THREAD * gpu_info.max_grid_dim_x * gpu_info.max_grid_dim_y) / vec_len);

	if (nb_vecs > 4 && ucs_likely(y >= 2 * __builtin_clzl(x)))
		reduce_all_for_many_vecs(dst_vec, src_vecs, nb_vecs, vec_len, *allreduce_super_request->stream);
	else {
		struct vectors src = {};
		src.arr = src_vecs;
		src.n = nb_vecs;
		gpu_reduce(dst_vec, src, vec_len, *allreduce_super_request->stream);
	}
}

void
set_cuda_globals(void)
{
	int dev_id;
	int streams_overlap_enabled;

	/* Prevent any useless profiling */
	cudaProfilerStop();

	/* Get GPU info */
	CUDA_ASSERT(cudaGetDevice(&dev_id));
	gpu_info.dev_id = dev_id;
	CUDA_ASSERT(cudaDeviceGetAttribute(&gpu_info.max_thrds_per_blk, cudaDevAttrMaxThreadsPerBlock, dev_id));
	CUDA_ASSERT(cudaDeviceGetAttribute(&gpu_info.max_grid_dim_x, cudaDevAttrMaxGridDimX, dev_id));
	CUDA_ASSERT(cudaDeviceGetAttribute(&gpu_info.max_grid_dim_y, cudaDevAttrMaxGridDimY, dev_id));
	CUDA_ASSERT(cudaDeviceGetAttribute(&streams_overlap_enabled, cudaDevAttrGpuOverlap, dev_id));
	CUDA_ASSERT(cudaDeviceGetAttribute(&gpu_info.warp_size, cudaDevAttrWarpSize, dev_id));

	if (!streams_overlap_enabled)
		DOCA_LOG_WARN("GPU overlapping is disabled - please enable it for better performance.");

	/* Allocate constant GPU memory */
	CUDA_ASSERT(cudaMemcpyToSymbol(datatype, &allreduce_config.datatype, sizeof(allreduce_config.datatype)));
	CUDA_ASSERT(cudaMemcpyToSymbol(operation, &allreduce_config.operation, sizeof(allreduce_config.operation)));
}

void
allreduce_reduce(struct allreduce_super_request *allreduce_super_request, void *src_vec, bool is_peer)
{
	void *dst_vec = is_peer ? allreduce_super_request->peer_result_vector : allreduce_super_request->result_vector;
	size_t dst_vec_len = allreduce_super_request->result_vector_size;
	cudaStream_t stream = *allreduce_super_request->stream;
	struct vectors src = {};

	src.vec = src_vec;
	src.n = 1;

	if (ucs_unlikely(dst_vec_len == 0))
		return;

	gpu_reduce(dst_vec, src, dst_vec_len, stream);
}
