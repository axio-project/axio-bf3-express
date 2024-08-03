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
#include <cuda.h>
#include <cuda_runtime.h>

#include <gpu_init.h>	/* Should put it before DPDK header includes, it contains a define to disable DPDK warnings */

#include <rte_ethdev.h>
#include <rte_gpudev.h>

extern "C" {
void workload_launch_gpu_processing(struct rte_gpu_comm_list *comm_list, cudaStream_t c_stream, char **queries);
}

#define DNS_FLAGS_SIZE 12	/* DNS packet payload starts with flags and details, the size is 12B */

/*
 * Calculate IPV4 header length
 *
 * @ipv4_hdr [in]: packet IPV4 header
 * @return: packet IPV4 header length
 */
__device__ __forceinline__ uint8_t
gpu_ipv4_hdr_len(const struct rte_ipv4_hdr *ipv4_hdr)
{
	return (uint8_t)((ipv4_hdr->version_ihl & RTE_IPV4_HDR_IHL_MASK) * RTE_IPV4_IHL_MULTIPLIER);
};

/*
 * CUDA kernel function to inspect the packets burst and extract the DNS queries
 *
 * @comm_list [in]: array of communication objects, holds the bursted packets context
 * @queries [out]: array of DNS queries
 */
__global__ void
gpu_dns_workload(struct rte_gpu_comm_list *comm_list, char **queries)
{
	/* thread_ID, each thread works on packet with index=thread_ID */
	int idx = threadIdx.x;

	/* Ethernet layer header size, skip it to reach the L3 header */
	const int l2_len = RTE_ETHER_HDR_LEN;
	const struct rte_ether_hdr *eth_hdr = (const struct rte_ether_hdr *) comm_list->pkt_list[idx].addr;
	const uint8_t *l3_hdr = (const uint8_t *)eth_hdr + l2_len;
	const struct rte_ipv4_hdr *hdr = (const struct rte_ipv4_hdr *) l3_hdr;

	/* Calculate L3 header size, skip it to reach L4 headers */
	uint8_t ip_hdr_len = gpu_ipv4_hdr_len(hdr);
	const uint8_t *l4_hdr = (const uint8_t *) (l3_hdr + ip_hdr_len);

	/* Calculate DNS query offset */
	int offset = l4_hdr - (const uint8_t *) comm_list->pkt_list[idx].addr;
	offset += sizeof(struct rte_udp_hdr); /* UDP Header size = 8B */
	offset += DNS_FLAGS_SIZE; /* Skip DNS flags */

	/* Store the address of DNS query */
	queries[idx] = (char *)(comm_list->pkt_list[idx].addr + offset);

	__syncthreads(); /* Wait all threads to reach this point */
	if (idx == 0) {
		/* Notify that GPU workload is done */
		RTE_GPU_VOLATILE(*(comm_list->status_d)) = RTE_GPU_COMM_LIST_DONE;
	}
}

void
workload_launch_gpu_processing(struct rte_gpu_comm_list *comm_list, cudaStream_t c_stream, char **queries)
{
	/* Create CUDA kernel to start GPU workload */
#ifdef DOCA_LOGGING_ALLOW_DLOG
	printf("CUDA kernel launch for extracting DNS queries\n");
#endif
	gpu_dns_workload<<<1, comm_list->num_pkts, 0, c_stream>>>(comm_list, queries);
}
