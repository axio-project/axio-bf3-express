/*
 * Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

#include <doca_argp.h>
#include <doca_dev.h>
#include <doca_log.h>

#include <samples/common.h>

#include <utils.h>

#include "secure_channel_core.h"

#define SERVER_NAME "secure_channel_server" /* Service name to address by the client */
#define CC_MAX_MSG_SIZE 4080		    /* Max message size */
#define CC_MAX_QUEUE_SIZE 8190		    /* Max queue size */
#define MAX_EVENTS 2			    /* Two file descriptors (comm channel, termination) */
#define SLEEP_IN_NANOS (10 * 1000)	    /* Sample the connection every 10 microseconds  */

DOCA_LOG_REGISTER(SECURE_CHANNEL::Core);

/*
 * ARGP Callback - Handle messages number parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
messages_number_callback(void *param, void *config)
{
	struct sc_config *app_cfg = (struct sc_config *)config;
	int nb_send_msg = *(int *)param;

	if (nb_send_msg < 1) {
		DOCA_LOG_ERR("Amount of messages to be sent by the client is less than 1");
		return DOCA_ERROR_INVALID_VALUE;
	}

	app_cfg->send_msg_nb = nb_send_msg;

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle message size parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
message_size_callback(void *param, void *config)
{
	struct sc_config *app_cfg = (struct sc_config *)config;
	int send_msg_size = *(int *)param;

	if (send_msg_size < 1 || send_msg_size > CC_MAX_MSG_SIZE) {
		DOCA_LOG_ERR("Received message size is not supported");
		return DOCA_ERROR_INVALID_VALUE;
	}

	app_cfg->send_msg_size = send_msg_size;
	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
dev_pci_addr_callback(void *param, void *config)
{
	struct sc_config *cfg = (struct sc_config *)config;
	const char *dev_pci_addr = (char *)param;

	if (strnlen(dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE) == DOCA_DEVINFO_PCI_ADDR_SIZE) {
		DOCA_LOG_ERR("Entered device PCI address exceeding the maximum size of %d", DOCA_DEVINFO_PCI_ADDR_SIZE - 1);
		return DOCA_ERROR_INVALID_VALUE;
	}

	strlcpy(cfg->cc_dev_pci_addr, dev_pci_addr, DOCA_DEVINFO_PCI_ADDR_SIZE);

	return DOCA_SUCCESS;
}

/*
 * ARGP Callback - Handle Comm Channel DOCA device representor PCI address parameter
 *
 * @param [in]: Input parameter
 * @config [in/out]: Program configuration context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
rep_pci_addr_callback(void *param, void *config)
{
	struct sc_config *cfg = (struct sc_config *)config;
	const char *rep_pci_addr = (char *)param;

	if (cfg->mode == SC_MODE_DPU) {
		if (strnlen(rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE) == DOCA_DEVINFO_REP_PCI_ADDR_SIZE) {
			DOCA_LOG_ERR("Entered device representor PCI address exceeding the maximum size of %d",
				     DOCA_DEVINFO_REP_PCI_ADDR_SIZE - 1);
			return DOCA_ERROR_INVALID_VALUE;
		}

		strlcpy(cfg->cc_dev_rep_pci_addr, rep_pci_addr, DOCA_DEVINFO_REP_PCI_ADDR_SIZE);
	}

	return DOCA_SUCCESS;
}

/*
 * Send user defined message size and amount into Comm Channel
 *
 * @context [in]: Thread context
 * @return: NULL (dummy return because of pthread requirement)
 */
static void *
sendto_channel(void *context)
{
	struct cc_ctx *ctx = (struct cc_ctx *)context;
	struct epoll_event events[MAX_EVENTS];
	struct epoll_event send_event = {
		.events = EPOLLIN,
		.data.fd = ctx->cc_send_fd
	};
	char send_buffer[ctx->cfg->send_msg_size];
	int signal, nfds, pthread_res, msg_nb = ctx->cfg->send_msg_nb;
	int idx, ev_idx, mili_timeout = 10, sent_count = 0;
	doca_error_t result;
	sigset_t signal_mask;

	if (sigemptyset(&signal_mask) != 0) {
		DOCA_LOG_ERR("Failed to create empty signal set, error=%d", errno);
		ctx->results->sendto_result = DOCA_ERROR_OPERATING_SYSTEM;
		return NULL;
	}

	if (sigaddset(&signal_mask, SIGUSR1) != 0) {
		DOCA_LOG_ERR("Failed to add SIGUSR1 to signal set, error=%d", errno);
		ctx->results->sendto_result = DOCA_ERROR_OPERATING_SYSTEM;
		return NULL;
	}

	/* Add Comm Channel send file descriptor to send epoll instance */
	if (epoll_ctl(ctx->cc_send_epoll_fd, EPOLL_CTL_ADD, ctx->cc_send_fd, &send_event) == -1) {
		DOCA_LOG_ERR("Failed to add Comm Channel file descriptor to send epoll instance, error=%d", errno);
		ctx->results->recvfrom_result = DOCA_ERROR_OPERATING_SYSTEM;
		return NULL;
	}

	for (idx = 0 ; idx < ctx->cfg->send_msg_size ; idx++)
		send_buffer[idx] = (uint8_t)(idx & 0xFF);

	while (msg_nb) {

		/* Connection has not established yet, wait for receive thread notification */
		if (ctx->peer == NULL && ctx->cfg->mode == SC_MODE_DPU) {
			if (sigwait(&signal_mask, &signal) != 0) {
				DOCA_LOG_ERR("Failed to wait for new connection, error=%d", errno);
				ctx->results->sendto_result = DOCA_ERROR_OPERATING_SYSTEM;
				return NULL;
			}
		}

		pthread_res = pthread_mutex_lock(ctx->mutex);
		if (pthread_res != 0) {
			DOCA_LOG_ERR("Failed to lock Comm Channel for writing");
			ctx->results->sendto_result = DOCA_ERROR_OPERATING_SYSTEM;
			return NULL;
		}
		result = doca_comm_channel_ep_sendto(ctx->ep, send_buffer, ctx->cfg->send_msg_size,
						     DOCA_CC_MSG_FLAG_NONE, ctx->peer);
		pthread_res = pthread_mutex_unlock(ctx->mutex);
		if (pthread_res != 0) {
			DOCA_LOG_ERR("Failed to unlock Comm Channel for writing");
			ctx->results->sendto_result = DOCA_ERROR_OPERATING_SYSTEM;
			return NULL;
		}

		if (result == DOCA_ERROR_AGAIN) {
			result = doca_comm_channel_ep_event_handle_arm_send(ctx->ep);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to arm Comm Channel send event channel, error=%d", errno);
				ctx->results->sendto_result = DOCA_ERROR_IO_FAILED;
				return NULL;
			}

			nfds = epoll_wait(ctx->cc_send_epoll_fd, events, MAX_EVENTS, mili_timeout);
			if (nfds == -1) {
				DOCA_LOG_ERR("Failed to wait on epoll instance, error=%d", errno);
				ctx->results->sendto_result = DOCA_ERROR_IO_FAILED;
				return NULL;
			}

			/* Check if interrupt was received */
			for (ev_idx = 0; ev_idx < nfds; ev_idx++) {
				if (events[ev_idx].data.fd == ctx->send_intr_fd) {
					DOCA_LOG_INFO(
						"Send thread exiting, total amount of messages sent successfully: %d",
						sent_count);
					pthread_cancel(*ctx->sendto_t);
					return NULL;
				}
			}

			continue;

		} else if (result != DOCA_SUCCESS)
			DOCA_LOG_WARN("Message number %d was not sent: %s", msg_nb, doca_get_error_string(result));
		else
			sent_count++;

		msg_nb--;
	}

	DOCA_LOG_INFO("Send thread exiting, total amount of messages sent successfully: %d", sent_count);
	return NULL;
}

/*
 * Receive messages from Comm Channel
 *
 * @context [in]: Input parameter
 * @return: NULL (dummy return because of pthread requirement)
 */
static void *
recvfrom_channel(void *context)
{
	struct cc_ctx *ctx = (struct cc_ctx *)context;
	struct doca_comm_channel_addr_t *curr_peer;
	struct epoll_event events[MAX_EVENTS];
	struct epoll_event recv_event = {
		.events = EPOLLIN,
		.data.fd = ctx->cc_recv_fd
	};
	char recv_buffer[CC_MAX_MSG_SIZE];
	int timeout = -1, recv_count = 0;
	int nfds, ev_idx, pthread_res;
	size_t msg_len = CC_MAX_MSG_SIZE;
	doca_error_t result;
	bool channel_created = false;

	memset(recv_buffer, 0, sizeof(recv_buffer));

	/* Add Comm Channel receive file descriptor to receive epoll instance */
	if (epoll_ctl(ctx->cc_recv_epoll_fd, EPOLL_CTL_ADD, ctx->cc_recv_fd, &recv_event) == -1) {
		DOCA_LOG_ERR("Failed to add Comm Channel file descriptor to receive epoll instance, error=%d", errno);
		ctx->results->recvfrom_result = DOCA_ERROR_OPERATING_SYSTEM;
		return NULL;
	}

	while (1) {
		pthread_res = pthread_mutex_lock(ctx->mutex);
		if (pthread_res != 0) {
			DOCA_LOG_ERR("Failed to lock Comm Channel for reading");
			ctx->results->recvfrom_result = DOCA_ERROR_OPERATING_SYSTEM;
			return NULL;
		}
		msg_len = CC_MAX_MSG_SIZE;
		result = doca_comm_channel_ep_recvfrom(ctx->ep, recv_buffer, &msg_len, DOCA_CC_MSG_FLAG_NONE,
						       &curr_peer);
		pthread_res = pthread_mutex_unlock(ctx->mutex);
		if (pthread_res != 0) {
			DOCA_LOG_ERR("Failed to lock Comm Channel for reading");
			ctx->results->recvfrom_result = DOCA_ERROR_OPERATING_SYSTEM;
			return NULL;
		}

		if (result == DOCA_ERROR_AGAIN) {
			result = doca_comm_channel_ep_event_handle_arm_recv(ctx->ep);
			if (result != DOCA_SUCCESS) {
				DOCA_LOG_ERR("Failed to arm Comm Channel receive event channel, error=%d", errno);
				ctx->results->recvfrom_result = DOCA_ERROR_IO_FAILED;
				return NULL;
			}

			nfds = epoll_wait(ctx->cc_recv_epoll_fd, events, MAX_EVENTS, timeout);
			if (nfds == -1) {
				DOCA_LOG_ERR("Failed to wait on epoll instance, error=%d", errno);
				ctx->results->recvfrom_result = DOCA_ERROR_IO_FAILED;
				return NULL;
			}

			/* Check if interrupt was received */
			for (ev_idx = 0; ev_idx < nfds; ev_idx++) {
				if (events[ev_idx].data.fd == ctx->recv_intr_fd) {
					DOCA_LOG_INFO(
						"Receive thread exiting, total amount of messages received successfully: %d",
						recv_count);
					if (!channel_created)
						pthread_cancel(*ctx->sendto_t);

					return NULL;
				}
			}

			/* Comm channel recv_event was received, run recvfrom() again */
			continue;

		} else if (result != DOCA_SUCCESS) {
			DOCA_LOG_WARN("Failed to receive channel message: %s", doca_get_error_string(result));
			continue;
		}

		/* Accept new connection from Host */
		if (!channel_created && ctx->cfg->mode == SC_MODE_DPU) {
			channel_created = true;
			/* Set first peer address for sent thread */
			ctx->peer = curr_peer;
			/* Signal send thread to start sending messages */
			pthread_res = pthread_kill(*ctx->sendto_t, SIGUSR1);
			if (pthread_res != 0) {
				DOCA_LOG_ERR("Failed signal send thread that a connection was made, error=%d", errno);
				ctx->results->recvfrom_result = DOCA_ERROR_OPERATING_SYSTEM;
				return NULL;
			}
		}
		recv_count++;
	}
}

/*
 * Initiate Comm Channel mutex
 *
 * @mutex [in]: mutex which will sync the channel between send and receive threads
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_thread_sync(pthread_mutex_t *mutex)
{
	if (pthread_mutex_init(mutex, NULL) != 0) {
		DOCA_LOG_ERR("Failed to initiate Comm Channel lock, error=%d", errno);
		return DOCA_ERROR_BAD_STATE;
	}

	return DOCA_SUCCESS;
}

/*
 * Set Comm Channel properties
 *
 * @mode [in]: Mode of operation
 * @ctx [in]: Thread context
 * @dev [in]: Comm Channel DOCA device
 * @dev_rep [in]: Comm Channel DOCA device representor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
set_cc_properties(enum sc_mode mode, struct cc_ctx *ctx, struct doca_dev *dev, struct doca_dev_rep *dev_rep)
{
	doca_error_t result;

	result = doca_comm_channel_ep_set_device(ctx->ep, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set DOCA device property");
		return result;
	}

	result = doca_comm_channel_ep_set_max_msg_size(ctx->ep, CC_MAX_MSG_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set max_msg_size property");
		return result;
	}

	result = doca_comm_channel_ep_set_send_queue_size(ctx->ep, CC_MAX_QUEUE_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set snd_queue_size property");
		return result;
	}

	result = doca_comm_channel_ep_set_recv_queue_size(ctx->ep, CC_MAX_QUEUE_SIZE);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set rcv_queue_size property");
		return result;
	}

	if (mode == SC_MODE_DPU) {
		result = doca_comm_channel_ep_set_device_rep(ctx->ep, dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set DOCA device representor property");
			return result;
		}
	}

	return result;
}

/*
 * Initiate Comm Channel
 *
 * @cfg [in]: Configuration structure
 * @ctx [in]: Thread context
 * @dev [in]: Comm Channel DOCA device
 * @dev_rep [in]: Comm Channel DOCA device representor
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_cc(struct sc_config *cfg, struct cc_ctx *ctx, struct doca_dev **dev, struct doca_dev_rep **dev_rep)
{
	doca_error_t result;
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = SLEEP_IN_NANOS,
	};

	/* Create Secure Channel endpoint */
	result = doca_comm_channel_ep_create(&ctx->ep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create Comm Channel endpoint");
		return result;
	}

	/* Open DOCA device */
	result = open_doca_device_with_pci(cfg->cc_dev_pci_addr, NULL, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open Comm Channel DOCA device based on PCI address");
		doca_comm_channel_ep_destroy(ctx->ep);
		return result;
	}

	/* Open DOCA device representor on DPU side */
	if (cfg->mode == SC_MODE_DPU) {
		result = open_doca_device_rep_with_pci(*dev, DOCA_DEV_REP_FILTER_NET, cfg->cc_dev_rep_pci_addr, dev_rep);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to open Comm Channel DOCA device representor based on PCI address");
			doca_comm_channel_ep_destroy(ctx->ep);
			doca_dev_close(*dev);
			return result;
		}
	}

	result = set_cc_properties(cfg->mode, ctx, *dev, *dev_rep);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set Comm Channel properties");
		doca_comm_channel_ep_destroy(ctx->ep);
		if (cfg->mode == SC_MODE_DPU)
			doca_dev_rep_close(*dev_rep);
		doca_dev_close(*dev);
		return result;
	}

	if (cfg->mode == SC_MODE_HOST) {
		result = doca_comm_channel_ep_connect(ctx->ep, SERVER_NAME, &ctx->peer);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Couldn't establish a connection with DPU node: %s",
				     doca_get_error_string(result));
			doca_comm_channel_ep_destroy(ctx->ep);
			doca_dev_close(*dev);
			return result;
		}

		while ((result = doca_comm_channel_peer_addr_update_info(ctx->peer)) == DOCA_ERROR_CONNECTION_INPROGRESS)
			nanosleep(&ts, &ts);

		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to validate the connection with the DPU: %s", doca_get_error_string(result));
			return result;
		}

		DOCA_LOG_INFO("Connection to DPU was established successfully");
	} else {
		result = doca_comm_channel_ep_listen(ctx->ep, SERVER_NAME);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Secure Channel server couldn't start listening: %s",
				     doca_get_error_string(result));
			doca_comm_channel_ep_destroy(ctx->ep);
			doca_dev_rep_close(*dev_rep);
			doca_dev_close(*dev);
			return result;
		}
		DOCA_LOG_INFO("Started Listening, waiting for new connection");
	}

	return DOCA_SUCCESS;
}

/*
 * Destroy Comm Channel
 *
 * @ctx [in]: Thread context
 * @dev [in]: Comm Channel DOCA device
 * @dev_rep [in]: Comm Channel DOCA device representor
 */
static void
destroy_cc(struct cc_ctx *ctx, struct doca_dev *dev, struct doca_dev_rep *dev_rep)
{
	doca_error_t result;

	if (ctx->peer != NULL) {
		result = doca_comm_channel_ep_disconnect(ctx->ep, ctx->peer);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to disconnect from Comm Channel peer address: %s",
				     doca_get_error_string(result));
	}

	result = doca_comm_channel_ep_destroy(ctx->ep);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to destroy Comm Channel endpoint: %s", doca_get_error_string(result));

	if (dev_rep != NULL) {
		result = doca_dev_rep_close(dev_rep);
		if (result != DOCA_SUCCESS)
			DOCA_LOG_ERR("Failed to close Comm Channel DOCA device representor: %s",
				     doca_get_error_string(result));
	}

	result = doca_dev_close(dev);
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Failed to close Comm Channel DOCA device: %s", doca_get_error_string(result));
}

/*
 * Initiate all relevant signal and epoll file descriptors
 *
 * @cc_send_epoll_fd [out]: Comm Channel send epoll file descriptor
 * @cc_recv_epoll_fd [out]: Comm Channel receive epoll file descriptor
 * @send_interrupt_fd [out]: File descriptor to catch interrupts on send thread
 * @recv_interrupt_fd [out]: File descriptor to catch interrupts on receive thread
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
init_signaling_polling(int *cc_send_epoll_fd, int *cc_recv_epoll_fd, int *send_interrupt_fd, int *recv_interrupt_fd)
{
	struct epoll_event intr_fd;
	sigset_t signal_mask;
	int fd;

	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGINT);
	sigaddset(&signal_mask, SIGUSR1);

	/* Block all threads on SIGINT and SIGTERM signals */
	if (pthread_sigmask(SIG_BLOCK, &signal_mask, NULL) != 0) {
		DOCA_LOG_ERR("Failed to create blocked signal mask, error=%d", errno);
		return DOCA_ERROR_BAD_STATE;
	}

	fd = epoll_create1(0);
	if (fd == -1) {
		DOCA_LOG_ERR("Failed to create Comm Channel send epoll instance, error=%d", errno);
		return DOCA_ERROR_IO_FAILED;
	}
	*cc_send_epoll_fd = fd;

	fd = epoll_create1(0);
	if (fd == -1) {
		DOCA_LOG_ERR("Failed to create Comm Channel receive epoll instance, error=%d", errno);
		close(*cc_send_epoll_fd);
		return DOCA_ERROR_IO_FAILED;
	}
	*cc_recv_epoll_fd = fd;

	fd = signalfd(-1, &signal_mask, 0);
	if (fd == -1) {
		DOCA_LOG_ERR("Failed to create send termination file descriptor, error=%d", errno);
		close(*cc_recv_epoll_fd);
		close(*cc_send_epoll_fd);
		return DOCA_ERROR_IO_FAILED;
	}
	*send_interrupt_fd = fd;
	intr_fd.events = EPOLLIN;
	intr_fd.data.fd = fd;
	if (epoll_ctl(*cc_send_epoll_fd, EPOLL_CTL_ADD, *send_interrupt_fd, &intr_fd) == -1) {
		DOCA_LOG_ERR("Failed to add termination file descriptor to epoll instance, error=%d", errno);
		close(*send_interrupt_fd);
		close(*cc_recv_epoll_fd);
		close(*cc_send_epoll_fd);
		return DOCA_ERROR_IO_FAILED;
	}

	fd = signalfd(-1, &signal_mask, 0);
	if (fd == -1) {
		DOCA_LOG_ERR("Failed to create receive termination file descriptor, error=%d", errno);
		close(*send_interrupt_fd);
		close(*cc_recv_epoll_fd);
		close(*cc_send_epoll_fd);
		return DOCA_ERROR_IO_FAILED;
	}
	*recv_interrupt_fd = fd;
	intr_fd.data.fd = fd;
	if (epoll_ctl(*cc_recv_epoll_fd, EPOLL_CTL_ADD, *recv_interrupt_fd, &intr_fd) == -1) {
		DOCA_LOG_ERR("Failed to add termination file descriptor to epoll instance, error=%d", errno);
		close(*recv_interrupt_fd);
		close(*send_interrupt_fd);
		close(*cc_recv_epoll_fd);
		close(*cc_send_epoll_fd);
		return DOCA_ERROR_IO_FAILED;
	}

	return DOCA_SUCCESS;
}

/*
 * Close all opened file descriptors
 *
 * @cc_send_epoll_fd [out]: Comm Channel send epoll file descriptor
 * @cc_recv_epoll_fd [out]: Comm Channel receive epoll file descriptor
 * @send_interrupt_fd [out]: File descriptor to catch interrupts on send thread
 * @recv_interrupt_fd [out]: File descriptor to catch interrupts on receive thread
 */
static void
close_fd(int cc_send_epoll_fd, int cc_recv_epoll_fd, int send_interrupt_fd, int recv_interrupt_fd)
{
	close(recv_interrupt_fd);
	close(send_interrupt_fd);
	close(cc_recv_epoll_fd);
	close(cc_send_epoll_fd);
}

/*
 * Start threads and wait for them to finish
 *
 * @ctx [in]: Thread context
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
start_threads(struct cc_ctx *ctx)
{
	if (pthread_create(ctx->sendto_t, NULL, sendto_channel, (void *)ctx) != 0) {
		DOCA_LOG_ERR("Failed to start sendto thread");
		return DOCA_ERROR_BAD_STATE;
	}

	if (pthread_create(ctx->recvfrom_t, NULL, recvfrom_channel, (void *)ctx) != 0) {
		DOCA_LOG_ERR("Failed to start recvfrom thread");
		return DOCA_ERROR_BAD_STATE;
	}

	pthread_join(*ctx->sendto_t, NULL);
	pthread_join(*ctx->recvfrom_t, NULL);

	return DOCA_SUCCESS;
}

doca_error_t
sc_start(struct sc_config *cfg, struct cc_ctx *ctx)
{
	struct t_results t_results = {0};
	struct doca_dev *dev = NULL;
	struct doca_dev_rep *dev_rep = NULL;
	doca_error_t result;
	int cc_send_epoll_fd, cc_recv_epoll_fd, send_intr_fd, recv_intr_fd;
	pthread_t sendto_thread, recvfrom_thread;
	pthread_mutex_t cc_mutex;

	result = init_thread_sync(&cc_mutex);
	if (result != DOCA_SUCCESS)
		return result;

	result = init_cc(cfg, ctx, &dev, &dev_rep);
	if (result != DOCA_SUCCESS) {
		pthread_mutex_destroy(&cc_mutex);
		return result;
	}

	result = init_signaling_polling(&cc_send_epoll_fd, &cc_recv_epoll_fd, &send_intr_fd, &recv_intr_fd);
	if (result != DOCA_SUCCESS) {
		destroy_cc(ctx, dev, dev_rep);
		return result;
	}

	ctx->cfg = cfg;
	ctx->cc_send_epoll_fd = cc_send_epoll_fd;
	ctx->cc_recv_epoll_fd = cc_recv_epoll_fd;
	ctx->send_intr_fd = send_intr_fd;
	ctx->recv_intr_fd = recv_intr_fd;
	ctx->sendto_t = &sendto_thread;
	ctx->recvfrom_t = &recvfrom_thread;
	ctx->mutex = &cc_mutex;
	ctx->results = &t_results;
	doca_comm_channel_ep_get_event_channel(ctx->ep, &ctx->cc_send_fd, &ctx->cc_recv_fd);

	result = start_threads(ctx);
	if (result != DOCA_SUCCESS) {
		close_fd(cc_send_epoll_fd, cc_recv_epoll_fd, send_intr_fd, recv_intr_fd);
		destroy_cc(ctx, dev, dev_rep);
		pthread_mutex_destroy(&cc_mutex);
		return result;
	}

	result = ctx->results->sendto_result;
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Send thread finished unsuccessfully");

	result = ctx->results->recvfrom_result;
	if (result != DOCA_SUCCESS)
		DOCA_LOG_ERR("Receive thread finished unsuccessfully");

	close_fd(cc_send_epoll_fd, cc_recv_epoll_fd, send_intr_fd, recv_intr_fd);
	destroy_cc(ctx, dev, dev_rep);
	pthread_mutex_destroy(&cc_mutex);

	return result;
}

doca_error_t
register_secure_channel_params(void)
{
	doca_error_t result;

	struct doca_argp_param *message_size_param, *messages_number_param, *pci_addr_param, *rep_pci_addr_param;

	/* Create and register message to send param */
	result = doca_argp_param_create(&message_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(message_size_param, "s");
	doca_argp_param_set_long_name(message_size_param, "msg-size");
	doca_argp_param_set_description(message_size_param, "Message size to be sent");
	doca_argp_param_set_callback(message_size_param, message_size_callback);
	doca_argp_param_set_type(message_size_param, DOCA_ARGP_TYPE_INT);
	doca_argp_param_set_mandatory(message_size_param);
	result = doca_argp_register_param(message_size_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register number of message param */
	result = doca_argp_param_create(&messages_number_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(messages_number_param, "n");
	doca_argp_param_set_long_name(messages_number_param, "num-msgs");
	doca_argp_param_set_description(messages_number_param, "Number of messages to be sent");
	doca_argp_param_set_callback(messages_number_param, messages_number_callback);
	doca_argp_param_set_type(messages_number_param, DOCA_ARGP_TYPE_INT);
	doca_argp_param_set_mandatory(messages_number_param);
	result = doca_argp_register_param(messages_number_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register Comm Channel DOCA device PCI address */
	result = doca_argp_param_create(&pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(pci_addr_param, "p");
	doca_argp_param_set_long_name(pci_addr_param, "pci-addr");
	doca_argp_param_set_description(pci_addr_param,
					"DOCA Comm Channel device PCI address");
	doca_argp_param_set_callback(pci_addr_param, dev_pci_addr_callback);
	doca_argp_param_set_type(pci_addr_param, DOCA_ARGP_TYPE_STRING);
	doca_argp_param_set_mandatory(pci_addr_param);
	result = doca_argp_register_param(pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Create and register Comm Channel DOCA device representor PCI address */
	result = doca_argp_param_create(&rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create ARGP param: %s", doca_get_error_string(result));
		return result;
	}
	doca_argp_param_set_short_name(rep_pci_addr_param, "r");
	doca_argp_param_set_long_name(rep_pci_addr_param, "rep-pci");
	doca_argp_param_set_description(rep_pci_addr_param,
					"DOCA Comm Channel device representor PCI address (needed only on DPU)");
	doca_argp_param_set_callback(rep_pci_addr_param, rep_pci_addr_callback);
	doca_argp_param_set_type(rep_pci_addr_param, DOCA_ARGP_TYPE_STRING);
	result = doca_argp_register_param(rep_pci_addr_param);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register program param: %s", doca_get_error_string(result));
		return result;
	}

	/* Register version callback for DOCA SDK & RUNTIME */
	result = doca_argp_register_version_callback(sdk_version_callback);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register version callback: %s", doca_get_error_string(result));
		return result;
	}

	return DOCA_SUCCESS;
}
