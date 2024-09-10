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

#ifndef _PSP_GW_FLOWS_H_
#define _PSP_GW_FLOWS_H_

#include <set>
#include <string>
#include <vector>
#include <map>

#include <rte_ether.h>

#include <doca_types.h>
#include <doca_flow.h>
#include <dpdk_utils.h>

// 0: PSP Header Version 0, AES-GCM-128
// 1: PSP Header Version 0, AES-GCM-256
// 2: PSP Header Version 0, AES-GMAC-128
// 3: PSP Header Version 0, AES-GMAC-256
inline const std::set<uint32_t> SUPPORTED_PSP_VERSIONS = {0, 1};
static const uint32_t DEFAULT_PSP_VERSION = 1;

// "The offset from the end of the Initialization Vector to
// the start of the encrypted portion of the payload,
// measured in 4-octet units."
// By default, leave the inner IPv4 header in cleartext.
// Add 2 if the 64-bit VC is enabled.
static constexpr uint32_t DEFAULT_CRYPT_OFFSET = 5;
static constexpr uint32_t DEFAULT_CRYPT_OFFSET_VC_ENABLED = 7;

static constexpr uint16_t PSP_PERF_KEY_GEN_PRINT = 1 << 0;
static constexpr uint16_t PSP_PERF_INSERTION_PRINT = 1 << 1;
static constexpr uint16_t PSP_PERF_ALL = PSP_PERF_KEY_GEN_PRINT | PSP_PERF_INSERTION_PRINT;

static const std::string PSP_PERF_KEY_GEN_PRINT_STR = "key-gen";
static const std::string PSP_PERF_INSERTION_PRINT_STR = "insertion";
static const std::string PSP_PERF_ALL_STR = "all";

static const std::map<std::string, uint16_t> PSP_PERF_MAP = {
	{PSP_PERF_KEY_GEN_PRINT_STR, PSP_PERF_KEY_GEN_PRINT},
	{PSP_PERF_INSERTION_PRINT_STR, PSP_PERF_INSERTION_PRINT},
	{PSP_PERF_ALL_STR, PSP_PERF_ALL},
};

static constexpr uint32_t IPV6_ADDR_LEN = 16;
typedef uint8_t ipv6_addr_t[IPV6_ADDR_LEN];

/**
 * @brief Describes a host which is capable of exchanging
 *        traffic flows over a PSP tunel.
 *
 * Currently, only one PF per host is supported, but this
 * could be extended to a list of PFs.
 */
struct psp_gw_host {
	uint32_t psp_proto_ver;	       /*!< 0 for 128-bit AES-GCM, 1 for 256-bit */
	std::vector<doca_be32_t> vips; /*!< virtual IP addresses */
	doca_be32_t svc_ip;	       /*!< control plane gRPC service address */
};

/**
 * @brief describes a network of hosts which participate
 *        in a network of PSP tunnel connections.
 */
struct psp_gw_net_config {
	std::vector<psp_gw_host> hosts; //!< The list of participating hosts and their interfaces

	bool vc_enabled;		//!< Whether Virtualization Cookies shall be included in the PSP headers
	uint32_t crypt_offset;		//!< The number of words to skip when performing encryption
	uint32_t default_psp_proto_ver; /*!< 0 for 128-bit AES-GCM, 1 for 256-bit */
};

/**
 * @brief describes the configuration of the PSP networking service on
 *        the local host.
 */
struct psp_gw_app_config {
	struct application_dpdk_config dpdk_config; //!< Configuration details of DPDK ports and queues

	std::string pf_pcie_addr;    //!< PCI domain:bus:device:function string of the host PF
	std::string pf_repr_indices; //!< Representor list string, such  as vf0 or pf[0-1]
	std::string core_mask;	     //!< EAL core mask

	std::string local_svc_addr; //!< The IPv4 addr (and optional port number) of the locally running gRPC service.
	std::string local_vf_addr;  //!< The IPv4 IP address of the VF; required for create_tunnels_at_startup

	rte_ether_addr dcap_dmac; //!< The dst mac to apply on decap

	bool nexthop_enable;	     //!< Whether to override the dmac in the tunnel request with a nexthop mac addr
	rte_ether_addr nexthop_dmac; //!< The dst mac to apply on encap, if enabled

	uint32_t max_tunnels; //!< The maximum number of outgoing tunnel connections supported on this host

	struct psp_gw_net_config net_config; //!< List of remote hosts supporting PSP connections

	/**
	 * The rate of sampling user packets is controlled by a uint16_t mask.
	 * This parameter determines how many bits of the mask should be set,
	 * with more bits indicating fewer packets to sample.
	 * (i.e. packet.meta.rand & mask == 1, where mask = (1<<N)-1)
	 *  0 -> sample no packets
	 *  1 -> sample 1 in 2
	 *  2 -> sample 1 in 4
	 * ...
	 * 16 -> sample 1 in 2^16 ~ 64K
	 * SAMPLE_RATE_DISABLED -> sampling disabled
	 */
	uint16_t log2_sample_rate;

	uint32_t ingress_sample_meta_indicator; //!< Value to assign pkt_meta when sampling incoming packets
	uint32_t egress_sample_meta_indicator;	//!< Value to assign pkt_meta when sampling outgoing packets

	bool create_tunnels_at_startup; //!< Create PSP tunnels at startup vs. on demand
	bool show_sampled_packets;	//!< Display to the console any packets marked for sampling
	bool show_rss_rx_packets;	//!< Display to the console any packets received via RSS
	bool show_rss_durations;	//!< Display performance information for RSS processing
	bool disable_ingress_acl;	//!< Allow any ingress packet that successfully decrypts
	bool debug_keys;		//!< Print the contents of PSP encryption keys to the console
	bool print_stats;		//!< Print session and pipeline statistics to the console
	bool maintain_order;		//!< maintain packet ordering when sampling enabled
	uint16_t print_perf_flags;	//!< Print performance information to the console
	enum doca_flow_l3_type outer;	//!< Indicate outer tunnel IP type
};

#endif // _PSP_GW_CONFIG_H_
