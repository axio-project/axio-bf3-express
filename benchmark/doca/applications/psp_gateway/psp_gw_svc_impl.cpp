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

#include <arpa/inet.h>
#include <doca_log.h>
#include <doca_flow_crypto.h>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

#include <psp_gw_svc_impl.h>
#include <psp_gw_config.h>
#include <psp_gw_flows.h>
#include <psp_gw_pkt_rss.h>
#include <psp_gw_utils.h>

DOCA_LOG_REGISTER(PSP_GW_SVC);

PSP_GatewayImpl::PSP_GatewayImpl(psp_gw_app_config *config, PSP_GatewayFlows *psp_flows)
	: config(config),
	  psp_flows(psp_flows),
	  pf(psp_flows->pf()),
	  DEBUG_KEYS(config->debug_keys)
{
}

doca_error_t PSP_GatewayImpl::handle_miss_packet(struct rte_mbuf *packet)
{
	if (config->create_tunnels_at_startup)
		return DOCA_SUCCESS; // no action; tunnels to be created by the main loop

	const auto *eth_hdr = rte_pktmbuf_mtod(packet, struct rte_ether_hdr *);
	if (eth_hdr->ether_type != RTE_BE16(RTE_ETHER_TYPE_IPV4))
		return DOCA_SUCCESS; // no action

	const auto *ipv4_hdr = rte_pktmbuf_mtod_offset(packet, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

	std::string dst_vip = ipv4_to_string(ipv4_hdr->dst_addr);

	// Create the new tunnel instance, if one does not already exist
	if (sessions.count(dst_vip) == 0) {
		// Determine the peer which owns the virtual destination
		auto *remote_host = lookup_remote_host(ipv4_hdr->dst_addr);
		if (!remote_host) {
			DOCA_LOG_WARN("Virtual Destination IP Addr not found: %s", dst_vip.c_str());
			return DOCA_ERROR_NOT_FOUND;
		}

		doca_error_t result = request_tunnel_to_host(remote_host,
							     ipv4_hdr->src_addr /* local addr */,
							     ipv4_hdr->dst_addr,
							     true,
							     false,
							     true);
		if (result != DOCA_SUCCESS) {
			return result;
		}
	}

	// A new tunnel was created; we can now resubmit the packet
	// and it will be encrypted and sent to the right port.
	if (!reinject_packet(packet, pf->port_id)) {
		std::string src_vip = ipv4_to_string(ipv4_hdr->src_addr);
		DOCA_LOG_ERR("Failed to resubmit packet from vnet addr %s to %s on port %d",
			     src_vip.c_str(),
			     dst_vip.c_str(),
			     pf->port_id);
		return DOCA_ERROR_FULL;
	}
	return DOCA_SUCCESS;
}

doca_error_t PSP_GatewayImpl::request_tunnel_to_host(struct psp_gw_host *remote_host,
						     doca_be32_t local_virt_ip,
						     doca_be32_t remote_virt_ip,
						     bool supply_reverse_params,
						     bool suppress_failure_msg,
						     bool has_remote)
{
	doca_error_t result;
	uint32_t key_len_bits = psp_version_to_key_length_bits(config->net_config.default_psp_proto_ver);
	uint32_t key_len_words = key_len_bits / 32;
	uint32_t nb_pairs = has_remote ? 1 : remote_host->vips.size();
	std::vector<uint32_t> keys(remote_host->vips.size() * key_len_words);
	std::vector<uint32_t> spis(remote_host->vips.size());

	std::string remote_host_svc_pip = ipv4_to_string(remote_host->svc_ip);
	std::string local_vip = ipv4_to_string(local_virt_ip);
	int vip_id = -1;

	auto *stub = get_stub(remote_host_svc_pip);

	::grpc::ClientContext context;
	::psp_gateway::MultiTunnelRequest request;
	request.set_request_id(++next_request_id);
	request.add_psp_versions_accepted(config->net_config.default_psp_proto_ver);

	if (supply_reverse_params) {
		result = generate_keys_spis(key_len_bits, nb_pairs, keys.data(), spis.data());
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to generate SPI/Key's for remote host %s: %s",
				     remote_host_svc_pip.c_str(),
				     doca_error_get_descr(result));
			return result;
		}
	}

	for (size_t vip_idx = 0; vip_idx < remote_host->vips.size(); vip_idx++) {
		if (has_remote) {
			if (remote_host->vips[vip_idx] != remote_virt_ip)
				continue;
			else
				vip_id = vip_idx;
		}
		std::string remote_host_vip = ipv4_to_string(remote_host->vips[vip_idx]);
		::psp_gateway::SingleTunnelRequest *single_request = request.add_tunnels();
		single_request->set_virt_src_ip(local_vip);
		single_request->set_virt_dst_ip(remote_host_vip);

		// Save a round-trip, if a local virtual IP was given.
		// Otherwise, expect the remote host to send a separate request.
		if (supply_reverse_params) {
			if (!local_virt_ip) {
				DOCA_LOG_ERR("Cannot create reverse params without a local virt ip addr");
				return DOCA_ERROR_INVALID_VALUE;
			}

			fill_tunnel_params(config->net_config.default_psp_proto_ver,
					   &(keys[vip_idx * key_len_words]),
					   spis[vip_idx],
					   single_request->mutable_reverse_params());
			debug_key("Generated",
				  single_request->reverse_params().encryption_key().c_str(),
				  key_len_bits / 8);

			if (!config->disable_ingress_acl) {
				auto &session = sessions[remote_host_vip];
				session.spi_ingress = single_request->reverse_params().spi();
				session.src_vip = remote_host->vips[vip_idx];
				session.pkt_count_ingress = UINT64_MAX;

				result = psp_flows->add_ingress_acl_entry(&session);
				if (result != DOCA_SUCCESS) {
					DOCA_LOG_ERR("Failed to open ACL from %s on SPI %d: %s",
						     remote_host_vip.c_str(),
						     session.spi_ingress,
						     doca_error_get_descr(result));
					return result;
				}

				DOCA_LOG_DBG("Opened ACL from host %s on SPI %d",
					     remote_host_vip.c_str(),
					     session.spi_ingress);
			}
		}
	}

	if (has_remote && vip_id == -1) {
		DOCA_LOG_ERR("Remote virtual IP not found");
		return DOCA_ERROR_NOT_FOUND;
	}

	::psp_gateway::MultiTunnelResponse response;
	::grpc::Status status = stub->RequestMultipleTunnelParams(&context, request, &response);

	if (!status.ok() || response.tunnels_params_size() != request.tunnels_size()) {
		if (!suppress_failure_msg) {
			DOCA_LOG_ERR("Request for new SPI/Key's to remote host %s failed: %s",
				     remote_host_svc_pip.c_str(),
				     status.error_message().c_str());
		}
		return DOCA_ERROR_IO_FAILED;
	}

	std::vector<psp_session_and_key_t> new_session_keys;
	for (int i = 0; i < response.tunnels_params_size(); i++) {
		if (supply_reverse_params) {
			if (response.tunnels_params(i).encap_type() !=
			    request.tunnels(i).reverse_params().encap_type()) {
				if (!suppress_failure_msg)
					DOCA_LOG_ERR("Encap type is different between request and response");
				return DOCA_ERROR_INVALID_VALUE;
			}
		}
		if (!has_remote)
			vip_id = i;
		result = prepare_session(remote_host_svc_pip,
					 remote_host->vips[vip_id],
					 response.tunnels_params(i),
					 new_session_keys);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR(
				"Failed to prepare session for remote host %s, request %ld: with remote vip %s: %s",
				remote_host_svc_pip.c_str(),
				request.request_id(),
				ipv4_to_string(remote_host->vips[i]).c_str(),
				doca_error_get_descr(result));
			return result;
		}
	}
	result = add_encrypt_entries(new_session_keys, remote_host_svc_pip);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add encrypt entries for remote host %s: %s",
			     remote_host_svc_pip.c_str(),
			     doca_error_get_descr(result));
		return result;
	}

	return DOCA_SUCCESS;
}

doca_error_t PSP_GatewayImpl::add_encrypt_entries(std::vector<psp_session_and_key_t> &new_sessions_keys,
						  std::string remote_host_svc_ip)
{
	DOCA_LOG_DBG("Adding %d encrypt entries for remote host %s",
		     (int)new_sessions_keys.size(),
		     remote_host_svc_ip.c_str());

	uint64_t start_time = rte_get_tsc_cycles();
	for (int i = 0; i < (int)new_sessions_keys.size(); i++) {
		doca_error_t result =
			psp_flows->add_encrypt_entry(new_sessions_keys[i].first, new_sessions_keys[i].second);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add encrypt entry for %s: %s",
				     remote_host_svc_ip.c_str(),
				     doca_error_get_descr(result));
			return result;
		}
	}
	uint64_t end_time = rte_get_tsc_cycles();
	double total_time = (end_time - start_time) / (double)rte_get_tsc_hz();
	double kilo_eps = 1e-3 * new_sessions_keys.size() / total_time;
	if (config->print_perf_flags & PSP_PERF_INSERTION_PRINT) {
		DOCA_LOG_INFO("Added %d encrypt entries in %f seconds, %f Kilo-EPS",
			      (int)new_sessions_keys.size(),
			      total_time,
			      kilo_eps);
	}
	return DOCA_SUCCESS;
}

doca_error_t PSP_GatewayImpl::prepare_session(std::string remote_host_svc_ip,
					      doca_be32_t remote_vip,
					      const psp_gateway::TunnelParameters &params,
					      std::vector<psp_session_and_key_t> &sessions_keys_prepared)
{
	std::string remote_host_vip = ipv4_to_string(remote_vip);

	if (!is_psp_ver_supported(params.psp_version())) {
		DOCA_LOG_ERR("Request for unsupported PSP version %d", params.psp_version());
		return DOCA_ERROR_UNSUPPORTED_VERSION;
	}

	uint32_t key_len_bytes = psp_version_to_key_length_bits(params.psp_version()) / 8;

	if (params.encryption_key().size() != key_len_bytes) {
		DOCA_LOG_ERR("Request for new SPI/Key to remote host %s failed: %s (%ld)",
			     remote_host_svc_ip.c_str(),
			     "Invalid encryption key length",
			     params.encryption_key().size() * 8);
		return DOCA_ERROR_IO_FAILED;
	}

	uint32_t crypto_id = next_crypto_id();
	if (crypto_id == UINT32_MAX) {
		DOCA_LOG_ERR("Exhausted available crypto_ids; cannot complete new tunnel");
		return DOCA_ERROR_NO_MEMORY;
	}

	auto &session = sessions[remote_host_vip];
	session.dst_vip = remote_vip;
	session.spi_egress = params.spi();
	session.crypto_id = crypto_id;
	session.psp_proto_ver = params.psp_version();
	session.vc = params.virt_cookie();
	void *enc_key = (void *)params.encryption_key().c_str();

	if (rte_ether_unformat_addr(params.mac_addr().c_str(), &session.dst_mac)) {
		DOCA_LOG_ERR("Failed to convert mac addr: %s", params.mac_addr().c_str());
		sessions.erase(remote_host_vip);
		return DOCA_ERROR_INVALID_VALUE;
	}

	if (params.encap_type() == 6) {
		session.dst_pip.type = DOCA_FLOW_L3_TYPE_IP6;
		if (inet_pton(AF_INET6, params.ip_addr().c_str(), session.dst_pip.ipv6_addr) != 1) {
			DOCA_LOG_ERR("Failed to parse dst_pip %s", params.ip_addr().c_str());
			sessions.erase(remote_host_vip);
			return DOCA_ERROR_INVALID_VALUE;
		}
	} else if (params.encap_type() == 4) {
		session.dst_pip.type = DOCA_FLOW_L3_TYPE_IP4;
		if (inet_pton(AF_INET, params.ip_addr().c_str(), &session.dst_pip.ipv4_addr) != 1) {
			DOCA_LOG_ERR("Failed to parse dst_pip %s", params.ip_addr().c_str());
			sessions.erase(remote_host_vip);
			return DOCA_ERROR_INVALID_VALUE;
		}
	}
	sessions_keys_prepared.push_back({&session, enc_key});

	return DOCA_SUCCESS;
}

int PSP_GatewayImpl::select_psp_version(const ::psp_gateway::MultiTunnelRequest *request) const
{
	for (int ver : request->psp_versions_accepted()) {
		if (is_psp_ver_supported(ver) > 0)
			return ver;
	}
	return -1;
}

::grpc::Status PSP_GatewayImpl::RequestMultipleTunnelParams(::grpc::ServerContext *context,
							    const ::psp_gateway::MultiTunnelRequest *request,
							    ::psp_gateway::MultiTunnelResponse *response)
{
	doca_error_t result;
	std::string peer = context ? context->peer() // note: NOT authenticated
				     :
				     "[TESTING]";

	int psp_ver = select_psp_version(request);
	if (psp_ver < 0) {
		std::string supported_psp_versions = "[ ";
		for (auto psp_ver : SUPPORTED_PSP_VERSIONS) {
			supported_psp_versions += std::to_string(psp_ver) + " ";
		}
		supported_psp_versions += "]";
		std::string error_str = "Rejecting tunnel request from peer " + peer + ", PSP version must be one of " +
					supported_psp_versions;
		DOCA_LOG_ERR("%s", error_str.c_str());
		return ::grpc::Status(::grpc::INVALID_ARGUMENT, error_str);
	}
	uint32_t key_len_bits = psp_version_to_key_length_bits(psp_ver);
	uint32_t key_len_words = key_len_bits / 32;
	std::vector<uint32_t> keys(request->tunnels_size() * key_len_words);
	std::vector<uint32_t> spis(request->tunnels_size());

	result = generate_keys_spis(key_len_bits, request->tunnels_size(), keys.data(), spis.data());
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to generate SPI/Key's for remote host %s: %s",
			     peer.c_str(),
			     doca_error_get_descr(result));
		return ::grpc::Status(::grpc::RESOURCE_EXHAUSTED, "Failed to generate SPI/Key");
	}

	response->set_request_id(request->request_id());

	std::vector<psp_session_and_key_t> reversed_sessions_keys;

	for (int tun_idx = 0; tun_idx < request->tunnels_size(); tun_idx++) {
		const ::psp_gateway::SingleTunnelRequest &single_request = request->tunnels(tun_idx);

		::psp_gateway::TunnelParameters *params = response->add_tunnels_params();
		std::string src_vip = single_request.virt_src_ip();

		fill_tunnel_params(psp_ver, &(keys[tun_idx * key_len_words]), spis[tun_idx], params);
		DOCA_LOG_DBG("#%d: SPI %d generated for addr %s on peer %s",
			     tun_idx,
			     params->spi(),
			     src_vip.c_str(),
			     peer.c_str());
		debug_key("Generated", params->encryption_key().c_str(), key_len_bits / 8);

		if (!config->disable_ingress_acl) {
			auto &session = sessions[src_vip];
			session.spi_ingress = params->spi();
			if (inet_pton(AF_INET, src_vip.c_str(), &session.src_vip) != 1) {
				return ::grpc::Status(grpc::INVALID_ARGUMENT,
						      "Failed to parse virt_src_ip: " + src_vip);
			}
			session.pkt_count_ingress = UINT64_MAX;

			result = psp_flows->add_ingress_acl_entry(&session);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to open ACL from %s on SPI %d: %s",
					     src_vip.c_str(),
					     session.spi_ingress,
					     doca_error_get_descr(result));
				return ::grpc::Status(grpc::INTERNAL, "Failed to create ingress ACL session flow");
			}

			DOCA_LOG_DBG("Opened ACL from host %s on SPI %d", src_vip.c_str(), session.spi_ingress);
		}

		if (single_request.has_reverse_params()) {
			if ((single_request.reverse_params().encap_type() == 4 &&
			     config->outer == DOCA_FLOW_L3_TYPE_IP6) ||
			    (single_request.reverse_params().encap_type() == 6 &&
			     config->outer == DOCA_FLOW_L3_TYPE_IP4)) {
				DOCA_LOG_ERR("Invalid encap type");
				return ::grpc::Status(::grpc::INVALID_ARGUMENT, "Received invalid encap type");
			}
			doca_be32_t remote_host_vip = {};

			if (inet_pton(AF_INET, src_vip.c_str(), &remote_host_vip) != 1) {
				return ::grpc::Status(grpc::INVALID_ARGUMENT,
						      "Failed to parse virt_src_ip: " + src_vip);
			}

			result = prepare_session(peer,
						 remote_host_vip,
						 single_request.reverse_params(),
						 reversed_sessions_keys);
			if (result != DOCA_SUCCESS) {
				return ::grpc::Status(::grpc::UNKNOWN,
						      "Failed to prepare session for remote host " +
							      std::to_string(request->request_id()));
			}
			DOCA_LOG_DBG("Created return flow on SPI %d to peer %s",
				     single_request.reverse_params().spi(),
				     peer.c_str());
		}
	}

	if (reversed_sessions_keys.size() > 0) {
		result = add_encrypt_entries(reversed_sessions_keys, peer);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add encrypt entries for remote host %s: %s",
				     peer.c_str(),
				     doca_error_get_descr(result));
			return ::grpc::Status(grpc::INTERNAL, "Failed to create ingress ACL session flow");
		}
	}

	return ::grpc::Status::OK;
}

void PSP_GatewayImpl::fill_tunnel_params(int psp_ver, uint32_t *key, uint32_t spi, psp_gateway::TunnelParameters *params)
{
	uint32_t key_len_bits = psp_version_to_key_length_bits(psp_ver);
	uint32_t key_len_bytes = key_len_bits / 8;

	params->set_mac_addr(pf->src_mac_str);
	params->set_ip_addr(pf->src_pip_str);
	params->set_psp_version(psp_ver);
	params->set_spi(spi);
	params->set_encryption_key(key, key_len_bytes);
	params->set_virt_cookie(0x778899aabbccddee);
	if (config->outer == DOCA_FLOW_L3_TYPE_IP4)
		params->set_encap_type(4);
	else
		params->set_encap_type(6);
}

doca_error_t PSP_GatewayImpl::generate_keys_spis(uint32_t key_len_bits,
						 uint32_t nr_keys_spis,
						 uint32_t *keys,
						 uint32_t *spis)
{
	doca_error_t result;
	struct doca_flow_crypto_psp_spi_key_bulk *bulk_key_gen = nullptr;

	auto key_type = key_len_bits == 128 ? DOCA_FLOW_CRYPTO_KEY_128 : DOCA_FLOW_CRYPTO_KEY_256;
	auto key_array_size = key_len_bits / 32; // 32-bit words

	DOCA_LOG_DBG("Generating %d SPI/Key pairs", nr_keys_spis);

	result = doca_flow_crypto_psp_spi_key_bulk_alloc(pf->port_obj, key_type, nr_keys_spis, &bulk_key_gen);
	if (result != DOCA_SUCCESS || !bulk_key_gen) {
		DOCA_LOG_ERR("Failed to allocate bulk-key-gen object: %s", doca_error_get_descr(result));
		return DOCA_ERROR_NO_MEMORY;
	}

	uint64_t start_time = rte_get_tsc_cycles();
	result = doca_flow_crypto_psp_spi_key_bulk_generate(bulk_key_gen);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to generate keys and SPIs: %s", doca_error_get_descr(result));
		doca_flow_crypto_psp_spi_key_bulk_free(bulk_key_gen);
		return DOCA_ERROR_IO_FAILED;
	}

	for (uint32_t i = 0; i < nr_keys_spis; i++) {
		uint32_t *cur_key = keys + (i * key_array_size);
		result = doca_flow_crypto_psp_spi_key_bulk_get(bulk_key_gen, i, &spis[i], cur_key);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to retrieve SPI/Key: %s", doca_error_get_descr(result));
			doca_flow_crypto_psp_spi_key_bulk_free(bulk_key_gen);
			return DOCA_ERROR_IO_FAILED;
		}
	}

	uint64_t end_time = rte_get_tsc_cycles();
	double total_time = (end_time - start_time) / (double)rte_get_tsc_hz();
	double kilo_kps = 1e-3 * nr_keys_spis / total_time;
	if (config->print_perf_flags & PSP_PERF_KEY_GEN_PRINT) {
		DOCA_LOG_INFO("Generated %d SPI/Key pairs in %f seconds, %f KILO-KPS",
			      nr_keys_spis,
			      total_time,
			      kilo_kps);
	}

	doca_flow_crypto_psp_spi_key_bulk_free(bulk_key_gen);

	return DOCA_SUCCESS;
}

::grpc::Status PSP_GatewayImpl::RequestKeyRotation(::grpc::ServerContext *context,
						   const ::psp_gateway::KeyRotationRequest *request,
						   ::psp_gateway::KeyRotationResponse *response)
{
	(void)context;
	DOCA_LOG_DBG("Received PSP Master Key Rotation Request");

	response->set_request_id(request->request_id());

	if (request->issue_new_keys()) {
		return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Re-key not implemented");
	}

	doca_error_t result = doca_flow_crypto_psp_master_key_rotate(pf->port_obj);
	if (result != DOCA_SUCCESS) {
		return ::grpc::Status(::grpc::StatusCode::UNKNOWN, "Key Rotation Failed");
	}

	return ::grpc::Status::OK;
}

size_t PSP_GatewayImpl::try_connect(std::vector<psp_gw_host> &hosts, rte_be32_t local_vf_addr)
{
	size_t num_connected = 0;
	for (auto host_iter = hosts.begin(); host_iter != hosts.end(); /* increment below */) {
		doca_error_t result = request_tunnel_to_host(&*host_iter, local_vf_addr, 0, false, true, false);
		if (result == DOCA_SUCCESS) {
			++num_connected;
			host_iter = hosts.erase(host_iter);
		} else {
			++host_iter;
		}
	}
	return num_connected;
}

psp_gw_host *PSP_GatewayImpl::lookup_remote_host(rte_be32_t dst_vip)
{
	for (auto &host : config->net_config.hosts) {
		for (auto vip : host.vips) {
			if (vip == dst_vip) {
				return &host;
			}
		}
	}
	return nullptr;
}

doca_error_t PSP_GatewayImpl::show_flow_counts(void)
{
	for (auto &session : sessions) {
		psp_flows->show_session_flow_count(session.first, session.second);
	}
	return DOCA_SUCCESS;
}

uint32_t PSP_GatewayImpl::next_crypto_id(void)
{
	if (next_crypto_id_ > config->max_tunnels) {
		return UINT32_MAX;
	}
	return next_crypto_id_++;
}

::psp_gateway::PSP_Gateway::Stub *PSP_GatewayImpl::get_stub(const std::string &remote_host_ip)
{
	auto stubs_iter = stubs.find(remote_host_ip);
	if (stubs_iter != stubs.end()) {
		return stubs_iter->second.get();
	}

	std::string remote_host_addr = remote_host_ip;
	if (remote_host_addr.find(":") == std::string::npos) {
		remote_host_addr += ":" + std::to_string(DEFAULT_HTTP_PORT_NUM);
	}
	auto channel = grpc::CreateChannel(remote_host_addr, grpc::InsecureChannelCredentials());
	stubs_iter = stubs.emplace(remote_host_ip, psp_gateway::PSP_Gateway::NewStub(channel)).first;

	DOCA_LOG_INFO("Created gRPC stub for remote host %s", remote_host_addr.c_str());

	return stubs_iter->second.get();
}

void PSP_GatewayImpl::debug_key(const char *msg_prefix, const void *key, size_t key_size_bytes) const
{
	if (!DEBUG_KEYS) {
		return;
	}

	char key_str[key_size_bytes * 3];
	const uint8_t *key_bytes = (const uint8_t *)key;
	for (size_t i = 0, j = 0; i < key_size_bytes; i++) {
		j += sprintf(key_str + j, "%02X", key_bytes[i]);
		if ((i % 4) == 3) {
			j += sprintf(key_str + j, " ");
		}
	}
	DOCA_LOG_INFO("%s encryption key: %s", msg_prefix, key_str);
}
