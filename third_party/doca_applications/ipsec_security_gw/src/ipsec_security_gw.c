/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <signal.h>
#include <fcntl.h>

#include <rte_ethdev.h>

#include <doca_argp.h>
#include <doca_log.h>

#include <dpdk_utils.h>
#include <pack.h>

#include "config.h"
#include "flow_common.h"
#include "flow_decrypt.h"
#include "flow_encrypt.h"
#include "ipsec_ctx.h"
#include "policy.h"

DOCA_LOG_REGISTER(IPSEC_SECURITY_GW);

#define DEFAULT_NB_CORES 4		/* Default number of running cores */
#define PACKET_BURST 32			/* The number of packets in the rx queue */
#define NB_TX_BURST_TRIES 5		/* Number of tries for sending batch of packets */
#define WINDOW_SIZE 64			/* The size of the replay window */

static bool force_quit;			/* Set when signal is received */

/*
 * Signals handler function to handle SIGINT and SIGTERM signals
 *
 * @signum [in]: signal number
 */
static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
		force_quit = true;
	}
}

/*
 * Receive the income packets from the RX queue process them, and send it to the TX queue in the second port
 *
 * @args [in]: generic pointer to core context struct
 */
static void
process_queue_packets(void *args)
{
	uint16_t port_id;
	uint16_t nb_packets_received;
	uint16_t nb_processed_packets = 0;
	uint16_t nb_packets_to_drop;
	struct rte_mbuf *packets[PACKET_BURST];
	struct rte_mbuf *processed_packets[PACKET_BURST] = {0};
	struct rte_mbuf *packets_to_drop[PACKET_BURST] = {0};
	int nb_pkts;
	int num_of_tries = NB_TX_BURST_TRIES;
	struct ipsec_security_gw_core_ctx *ctx = (struct ipsec_security_gw_core_ctx *)args;
	uint16_t nb_ports = ctx->config->dpdk_config->port_config.nb_ports;

	DOCA_LOG_DBG("Core %u is receiving packets", rte_lcore_id());
	while (!force_quit) {
		for (port_id = 0; port_id < nb_ports; port_id++) {
			nb_packets_received = rte_eth_rx_burst(port_id, ctx->queue_id, packets, PACKET_BURST);
			if (nb_packets_received) {
				DOCA_DLOG_DBG("Received %d packets from port %d on core %u", nb_packets_received, port_id, rte_lcore_id());
				if (port_id == (ctx->ports[UNSECURED_IDX])->port_id)
					handle_unsecured_packets_received(nb_packets_received, packets, ctx, &nb_processed_packets, processed_packets, packets_to_drop);
				else
					handle_secured_packets_received(nb_packets_received, packets, ctx, &nb_processed_packets, processed_packets, packets_to_drop);
				nb_pkts = 0;
				do {
					nb_pkts += rte_eth_tx_burst(port_id ^ 1, ctx->queue_id, processed_packets + nb_pkts, nb_processed_packets - nb_pkts);
					num_of_tries--;
				} while (nb_processed_packets > nb_pkts && num_of_tries > 0);
				if (nb_processed_packets > nb_pkts)
					DOCA_DLOG_WARN("%d packets were dropped during the transmission to the next port", (nb_processed_packets - nb_pkts));
				nb_packets_to_drop = nb_packets_received - nb_processed_packets;
				if (nb_packets_to_drop > 0) {
					DOCA_DLOG_WARN("%d packets were dropped during the processing", nb_packets_to_drop);
					rte_pktmbuf_free_bulk(packets_to_drop, nb_packets_to_drop);
				}
			}
		}
	}
	free(ctx);
}

/*
 * Run on each lcore process_queue_packets() to receive and send packets in a loop
 *
 * @config [in]: application configuration struct
 * @ports [in]: application ports
 * @antireplay_states [in]: antireplay states array
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
ipsec_security_gw_process_packets(struct ipsec_security_gw_config *config, struct ipsec_security_gw_ports_map *ports[],
				  struct antireplay_state *antireplay_states)
{
	uint16_t lcore_index = 0;
	int current_lcore = 0;
	struct ipsec_security_gw_core_ctx *ctx;
	int nb_queues = config->dpdk_config->port_config.nb_queues;

	while ((current_lcore < RTE_MAX_LCORE) && (lcore_index < nb_queues)) {
		current_lcore = rte_get_next_lcore(current_lcore, true, false);
		ctx = (struct ipsec_security_gw_core_ctx *)malloc(sizeof(struct ipsec_security_gw_core_ctx));
		if (ctx == NULL) {
			DOCA_LOG_ERR("malloc() failed");
			force_quit = true;
			return DOCA_ERROR_NO_MEMORY;
		}
		ctx->queue_id = lcore_index;
		ctx->config = config;
		ctx->encrypt_rules = config->app_rules.encrypt_rules;
		ctx->decrypt_rules = config->app_rules.decrypt_rules;
		ctx->nb_encrypt_rules = &config->app_rules.nb_encrypted_rules;
		ctx->ports = ports;
		ctx->antireplay_states = antireplay_states;

		/* Launch the worker to start process packets */
		if (rte_eal_remote_launch((void *)process_queue_packets, (void *)ctx, current_lcore) != 0) {
			DOCA_LOG_ERR("Remote launch failed");
			free(ctx);
			force_quit = true;
			return DOCA_ERROR_DRIVER;
		}
		lcore_index++;
	}
	return DOCA_SUCCESS;
}

/*
 * Initialize the antireplay states array
 *
 * @nb_entries [in]: number of entries in the array
 * @initial_sn [in]: initial sequence number
 * @states [in]: antireplay states array
 */
static void
init_anti_replay_states(int nb_entries, uint32_t initial_sn, struct antireplay_state *states)
{
	int i;
	uint32_t end_win_sn = initial_sn + WINDOW_SIZE - 1;

	for (i = 0; i < nb_entries; i++) {
		states[i].window_size = WINDOW_SIZE;
		states[i].end_win_sn = end_win_sn;
		states[i].bitmap = 0;
		states[i].initial_sn = initial_sn;
	}
}

/*
 * Unpack external buffer for new policy
 *
 * @buf [in]: buffer to unpack
 * @nb_bytes [in]: buffer size
 * @policy [out]: policy pointer to store the unpacked values
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
unpack_policy_buffer(uint8_t *buf, uint32_t nb_bytes, struct ipsec_security_gw_ipsec_policy *policy)
{
	uint8_t *ptr = buf;

	policy->src_port = unpack_uint16(&ptr);
	policy->dst_port = unpack_uint16(&ptr);
	policy->l3_protocol = unpack_uint8(&ptr);
	policy->l4_protocol = unpack_uint8(&ptr);
	policy->outer_l3_protocol = unpack_uint8(&ptr);
	policy->policy_direction = unpack_uint8(&ptr);
	policy->policy_mode = unpack_uint8(&ptr);
	policy->esn = unpack_uint8(&ptr);
	policy->icv_length = unpack_uint8(&ptr);
	policy->key_type = unpack_uint8(&ptr);
	policy->spi = unpack_uint32(&ptr);
	policy->salt = unpack_uint32(&ptr);
	unpack_blob(&ptr, MAX_IP_ADDR_LEN + 1, (uint8_t *)policy->src_ip_addr);
	policy->src_ip_addr[MAX_IP_ADDR_LEN] = '\0';
	unpack_blob(&ptr, MAX_IP_ADDR_LEN + 1, (uint8_t *)policy->dst_ip_addr);
	policy->dst_ip_addr[MAX_IP_ADDR_LEN] = '\0';
	unpack_blob(&ptr, MAX_IP_ADDR_LEN + 1, (uint8_t *)policy->outer_src_ip);
	policy->outer_src_ip[MAX_IP_ADDR_LEN] = '\0';
	unpack_blob(&ptr, MAX_IP_ADDR_LEN + 1, (uint8_t *)policy->outer_dst_ip);
	policy->outer_dst_ip[MAX_IP_ADDR_LEN] = '\0';
	if (nb_bytes == POLICY_RECORD_MAX_SIZE)
		unpack_blob(&ptr, 32, (uint8_t *)policy->enc_key_data);
	else
		unpack_blob(&ptr, 16, (uint8_t *)policy->enc_key_data);
	return DOCA_SUCCESS;
}

/*
 * Read bytes_to_read from given socket
 *
 * @fd [in]: socket file descriptor
 * @bytes_to_read [in]: number of bytes to read
 * @buf [out]: store data from socket
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
fill_buffer_from_socket(int fd, size_t bytes_to_read, uint8_t *buf)
{
	ssize_t ret;
	size_t bytes_received = 0;

	do {
		ret = recv(fd, buf + bytes_received, bytes_to_read - bytes_received, 0);
		if (ret == -1) {
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return DOCA_ERROR_AGAIN;
			else {
				DOCA_LOG_ERR("Failed to read from socket buffer [%s]", strerror(errno));
				return DOCA_ERROR_IO_FAILED;
			}
		}
		if (ret == 0)
			return DOCA_ERROR_AGAIN;
		bytes_received += ret;
	} while (bytes_received < bytes_to_read);

	return DOCA_SUCCESS;
}

/*
 * Read first 4 bytes from the socket to know policy length
 *
 * @app_cfg [in]: application configuration struct
 * @length [out]: policy length
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
read_message_length(struct ipsec_security_gw_config *app_cfg, uint32_t *length)
{
	uint8_t buf[8] = {0};
	uint8_t *ptr = &buf[0];
	uint32_t policy_length;
	doca_error_t result;

	result = fill_buffer_from_socket(app_cfg->socket_ctx.connfd, sizeof(uint32_t), buf);
	if (result != DOCA_SUCCESS)
		return result;

	policy_length = unpack_uint32(&ptr);
	if (policy_length != POLICY_RECORD_MIN_SIZE && policy_length != POLICY_RECORD_MAX_SIZE) {
		DOCA_LOG_ERR("Wrong policy length [%u], should be [%u] or [%u]", policy_length,
				POLICY_RECORD_MIN_SIZE, POLICY_RECORD_MAX_SIZE);
		return DOCA_ERROR_IO_FAILED;
	}

	*length = policy_length;
	return DOCA_SUCCESS;
}

/*
 * check if new policy where added to the socket and read it.
 *
 * @app_cfg [in]: application configuration struct
 * @policy [out]: policy structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
read_message_from_socket(struct ipsec_security_gw_config *app_cfg, struct ipsec_security_gw_ipsec_policy *policy)
{
	uint8_t buffer[1024] = {0};
	uint32_t policy_length;
	doca_error_t result;

	result = read_message_length(app_cfg, &policy_length);
	if (result != DOCA_SUCCESS)
		return result;

	result = fill_buffer_from_socket(app_cfg->socket_ctx.connfd, policy_length, buffer);
	if (result != DOCA_SUCCESS)
		return result;

	return unpack_policy_buffer(buffer, policy_length, policy);
}

/*
 * Wait in a loop and process packets until receive signal
 *
 * @app_cfg [in]: application configuration struct
 * @ports [in]: application ports
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
ipsec_security_gw_wait_for_traffic(struct ipsec_security_gw_config *app_cfg, struct ipsec_security_gw_ports_map *ports[])
{
	doca_error_t result = DOCA_SUCCESS;
	struct ipsec_security_gw_ipsec_policy policy = {0};
	struct doca_flow_port *secured_port;
	struct encrypt_rule *enc_rule;
	struct decrypt_rule *dec_rule;
	int entry_idx;
	struct antireplay_state *antireplay_states = NULL;

	force_quit = false;
	DOCA_LOG_INFO("Waiting for traffic, press Ctrl+C for termination");
	if (app_cfg->offload != IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH) {
		if (app_cfg->sw_sn_inc_enable) {
			for (entry_idx = 0; entry_idx < MAX_NB_RULES; entry_idx++)
				app_cfg->app_rules.encrypt_rules[entry_idx].current_sn = (uint32_t)(app_cfg->sn_initial);
		}
		if (app_cfg->sw_antireplay) {
			/* Create and allocate an anti-replay state for each entry */
			antireplay_states = (struct antireplay_state *)calloc(MAX_NB_RULES, sizeof(struct antireplay_state));
			if (antireplay_states == NULL) {
				DOCA_LOG_ERR("Failed to allocate anti-replay state");
				return DOCA_ERROR_NO_MEMORY;
			}
			init_anti_replay_states(MAX_NB_RULES, (uint32_t)(app_cfg->sn_initial), antireplay_states);
		}
		result = ipsec_security_gw_process_packets(app_cfg, ports, antireplay_states);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to process packets on all lcores");
			goto exit_failure;
		}
	}

	if (app_cfg->flow_mode == IPSEC_SECURITY_GW_VNF)
		secured_port = ports[SECURED_IDX]->port;
	else
		secured_port = doca_flow_port_switch_get(NULL);

	while (!force_quit) {
		if (!app_cfg->socket_ctx.socket_conf) {
			sleep(1);
			continue;
		}

		memset(&policy, 0, sizeof(policy));
		result = read_message_from_socket(app_cfg,  &policy);
		if (result != DOCA_SUCCESS) {
			if (result == DOCA_ERROR_AGAIN) {
				DOCA_LOG_DBG("No new IPsec policy, try again");
				sleep(1);
				continue;
			} else {
				DOCA_LOG_ERR("Failed to read new IPSEC policy [%s]", doca_get_error_string(result));
				goto exit_failure;
			}
		}

		print_policy_attrs(&policy);

		if (policy.policy_direction == POLICY_DIR_OUT) {
			if (app_cfg->app_rules.nb_encrypted_rules >= MAX_NB_RULES) {
				DOCA_LOG_ERR("Can't receive more encryption policies the array is full, maximum size is [%d]", MAX_NB_RULES);
				result = DOCA_ERROR_BAD_STATE;
				goto exit_failure;
			}
			/* Get the next empty encryption rule for egress traffic */
			enc_rule = &app_cfg->app_rules.encrypt_rules[app_cfg->app_rules.nb_encrypted_rules];
			result = ipsec_security_gw_handle_encrypt_policy(app_cfg, ports, &policy, enc_rule);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to handle new encryption policy");
				goto exit_failure;
			}
		} else if (policy.policy_direction == POLICY_DIR_IN) {
			if (app_cfg->app_rules.nb_decrypted_rules >= MAX_NB_RULES) {
				DOCA_LOG_ERR("Can't receive more decryption policies the array is full, maximum size is [%d]", MAX_NB_RULES);
				result = DOCA_ERROR_BAD_STATE;
				goto exit_failure;
			}
			/* Get the next empty decryption rule for ingress traffic */
			dec_rule =  &app_cfg->app_rules.decrypt_rules[app_cfg->app_rules.nb_decrypted_rules];
			result = ipsec_security_gw_handle_decrypt_policy(app_cfg, secured_port, &policy, dec_rule);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to handle new decryption policy");
				goto exit_failure;
			}
		}
	}

exit_failure:
	force_quit = true;
	/* If SW offload is enabled, wait till threads finish */
	if (app_cfg->offload != IPSEC_SECURITY_GW_ESP_OFFLOAD_BOTH)
		rte_eal_mp_wait_lcore();

	if (app_cfg->socket_ctx.socket_conf) {
		/* Close the connection */
		close(app_cfg->socket_ctx.connfd);
		close(app_cfg->socket_ctx.fd);

		/* Remove the socket file */
		unlink(app_cfg->socket_ctx.socket_path);
	}

	if (antireplay_states != NULL)
		free(antireplay_states);

	return result;
}

/*
 * Create socket connection, including opening new fd for socket, binding shared file, and listening for new connection
 *
 * @app_cfg [in/out]: application configuration structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_policy_socket(struct ipsec_security_gw_config *app_cfg)
{
	struct sockaddr_un addr;
	int fd, connfd, flags;
	doca_error_t result;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	strlcpy(addr.sun_path, app_cfg->socket_ctx.socket_path, MAX_SOCKET_PATH_NAME);

	/* Create a Unix domain socket */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		DOCA_LOG_ERR("Failed to create new socket [%s]",  strerror(errno));
		return DOCA_ERROR_IO_FAILED;
	}

	/* Set socket as non blocking */
	flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		DOCA_LOG_ERR("Failed to set socket as non blocking [%s]", strerror(errno));
		close(fd);
		return DOCA_ERROR_IO_FAILED;
	}

	/* Bind the socket to a file path */
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		DOCA_LOG_ERR("Failed to bind the socket with the file path [%s]", strerror(errno));
		result = DOCA_ERROR_IO_FAILED;
		goto exit_failure;
	}

	/* Listen for incoming connections */
	if (listen(fd, 5) == -1) {
		DOCA_LOG_ERR("Failed to listen for incoming connection [%s]", strerror(errno));
		result = DOCA_ERROR_IO_FAILED;
		goto exit_failure;
	}

	DOCA_LOG_DBG("Waiting for establishing new connection");

	/* Accept an incoming connection */
	while (!force_quit) {
		connfd = accept(fd, NULL, NULL);
		if (connfd == -1) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				sleep(1);	/* No pending connections at the moment
						 * Wait for a short period and retry
						 */
				continue;
			}
			DOCA_LOG_ERR("Failed to accept incoming connection [%s]", strerror(errno));
			result = DOCA_ERROR_IO_FAILED;
			goto exit_failure;
		} else
			break;
	}

	/* Set socket as non blocking */
	flags = fcntl(connfd, F_GETFL, 0);
	if (fcntl(connfd, F_SETFL, flags | O_NONBLOCK) == -1) {
		DOCA_LOG_ERR("Failed to set connection socket as non blocking [%s]", strerror(errno));
		result = DOCA_ERROR_IO_FAILED;
		close(connfd);
		goto exit_failure;
	}
	app_cfg->socket_ctx.connfd = connfd;
	app_cfg->socket_ctx.fd = fd;
	return DOCA_SUCCESS;

exit_failure:
	close(fd);
	/* Remove the socket file */
	unlink(app_cfg->socket_ctx.socket_path);
	return result;
}

/*
 * IPsec Security Gateway application main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
	doca_error_t result;
	int ret, nb_ports = 2;
	int exit_status = EXIT_SUCCESS;
	struct ipsec_security_gw_ports_map *ports[nb_ports];
	struct ipsec_security_gw_config app_cfg = {0};
	struct application_dpdk_config dpdk_config = {
		.port_config.nb_ports = nb_ports,
		.port_config.nb_queues = 2,
		.port_config.nb_hairpin_q = 2,
		.port_config.enable_mbuf_metadata = true,
		.port_config.isolated_mode = true,
		.reserve_main_thread = true,
	};
	char cores_str[10];
	char *eal_param[5] = {"", "-a", "00:00.0", "-l", ""};
	struct doca_flow_pipe *encrypt_root, *encrypt_pipe, *decrypt_root;

	app_cfg.dpdk_config = &dpdk_config;
	app_cfg.nb_cores = DEFAULT_NB_CORES;

	/* Register a logger backend */
	result = doca_log_create_standard_backend();
	if (result != DOCA_SUCCESS)
		return EXIT_FAILURE;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	force_quit = false;

	/* Init ARGP interface and start parsing cmdline/json arguments */
	result = doca_argp_init("doca_ipsec_security_gw", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_get_error_string(result));
		return EXIT_FAILURE;
	}
	result = register_ipsec_security_gw_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register application params: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application input: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	snprintf(cores_str, sizeof(cores_str), "0-%d", app_cfg.nb_cores - 1);
	eal_param[4] = cores_str;
	ret = rte_eal_init(5, eal_param);
	if (ret < 0) {
		DOCA_LOG_ERR("EAL initialization failed");
		exit_status = EXIT_FAILURE;
		goto argp_destroy;
	}

	result = ipsec_security_gw_parse_config(&app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse application json file: %s", doca_get_error_string(result));
		doca_argp_destroy();
		return EXIT_FAILURE;
	}

	if (app_cfg.flow_mode == IPSEC_SECURITY_GW_SWITCH)
		dpdk_config.port_config.self_hairpin = true;

	result = ipsec_security_gw_init_devices(&app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open DOCA devices: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto argp_destroy;
	}

	/* Update queues and ports */
	result = dpdk_queues_and_ports_init(&dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update application ports and queues: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_destroy;
	}

	result = ipsec_security_gw_ipsec_ctx_create(&app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create encrypt sa object: %s", doca_get_error_string(result));
		exit_status = EXIT_FAILURE;
		goto dpdk_cleanup;
	}

	result = ipsec_security_gw_init_doca_flow(&app_cfg, dpdk_config.port_config.nb_queues, ports);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow");
		exit_status = EXIT_FAILURE;
		goto ipsec_ctx_cleanup;
	}

	result = ipsec_security_gw_insert_encrypt_rules(ports, &app_cfg, dpdk_config.port_config.nb_queues,
							&encrypt_root, &encrypt_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create encrypt rules");
		exit_status = EXIT_FAILURE;
		goto doca_flow_cleanup;
	}

	result = ipsec_security_gw_insert_decrypt_rules(ports[SECURED_IDX], &app_cfg, &decrypt_root);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create decrypt rules");
		exit_status = EXIT_FAILURE;
		goto doca_flow_cleanup;
	}

	if (app_cfg.flow_mode == IPSEC_SECURITY_GW_SWITCH) {
		result = create_switch_root_pipes(ports, encrypt_root, decrypt_root, encrypt_pipe);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create switch root pipe");
			exit_status = EXIT_FAILURE;
			goto doca_flow_cleanup;
		}
	}

	if (app_cfg.socket_ctx.socket_conf) {
		result = create_policy_socket(&app_cfg);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create policy socket");
			exit_status = EXIT_FAILURE;
			goto doca_flow_cleanup;
		}
	}

	result = ipsec_security_gw_wait_for_traffic(&app_cfg, ports);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error happened during waiting for new traffic");
		exit_status = EXIT_FAILURE;
		goto doca_flow_cleanup;
	}

doca_flow_cleanup:
	/* Flow cleanup */
	doca_flow_cleanup(nb_ports, ports);

	/* Destroy rules SAs */
	ipsec_security_gw_destroy_sas(&app_cfg);
ipsec_ctx_cleanup:
	ipsec_security_gw_ipsec_ctx_destroy(&app_cfg);
dpdk_cleanup:
	/* DPDK cleanup */
	dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_destroy:
	dpdk_fini();
argp_destroy:
	if (app_cfg.app_rules.encrypt_rules)
		free(app_cfg.app_rules.encrypt_rules);
	if (app_cfg.app_rules.decrypt_rules)
		free(app_cfg.app_rules.decrypt_rules);

	/* ARGP cleanup */
	doca_argp_destroy();

	return exit_status;
}
