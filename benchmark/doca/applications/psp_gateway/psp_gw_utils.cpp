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

#include <netinet/in.h>
#include <arpa/inet.h>
#include <rte_ether.h>

#include "psp_gw_utils.h"

std::string ipv4_to_string(rte_be32_t ipv4_addr)
{
	std::string addr_str(INET_ADDRSTRLEN, '\0');
	inet_ntop(AF_INET, &ipv4_addr, addr_str.data(), INET_ADDRSTRLEN);
	addr_str.resize(strlen(addr_str.c_str()));
	return addr_str;
}

std::string ipv6_to_string(const uint32_t ipv6_addr[])
{
	std::string addr_str(INET6_ADDRSTRLEN, '\0');
	inet_ntop(AF_INET6, ipv6_addr, addr_str.data(), INET6_ADDRSTRLEN);
	addr_str.resize(strlen(addr_str.c_str()));
	return addr_str;
}

std::string mac_to_string(const rte_ether_addr &mac_addr)
{
	std::string addr_str(RTE_ETHER_ADDR_FMT_SIZE, '\0');
	rte_ether_format_addr(addr_str.data(), RTE_ETHER_ADDR_FMT_SIZE, &mac_addr);
	addr_str.resize(strlen(addr_str.c_str()));
	return addr_str;
}

bool is_empty_mac_addr(const rte_ether_addr &addr)
{
	rte_ether_addr empty_ether_addr = {};
	return !memcmp(empty_ether_addr.addr_bytes, addr.addr_bytes, RTE_ETHER_ADDR_LEN);
}
