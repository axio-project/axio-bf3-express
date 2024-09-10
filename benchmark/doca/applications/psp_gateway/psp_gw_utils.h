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

#ifndef _PSP_GW_UTILS_H_
#define _PSP_GW_UTILS_H_

#include <inttypes.h>
#include <string>

#include <rte_ether.h>
#include <rte_byteorder.h>

/**
 * @brief Converts an IPv4 address to a C++ string
 */
std::string ipv4_to_string(rte_be32_t ipv4_addr);

/**
 * @brief Converts an IPv6 address to a C++ string
 */
std::string ipv6_to_string(const uint32_t ipv6_addr[]);

/**
 * @brief Converts a MAC/ethernet address to a C++ string
 */
std::string mac_to_string(const rte_ether_addr &mac_addr);

/**
 * @brief Tests whether a MAC address has been set (is non-zero)
 *
 * @addr [in]: the mac addr to test
 * @return: true if all bits are zero; false otherwise
 */
bool is_empty_mac_addr(const rte_ether_addr &addr);

#endif /* _PSP_GW_UTILS_H_ */
