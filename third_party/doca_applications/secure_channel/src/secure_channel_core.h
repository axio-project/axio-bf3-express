/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef SECURE_CHANNEL_CORE_H_
#define SECURE_CHANNEL_CORE_H_

#include <pthread.h>

#include <doca_comm_channel.h>

enum sc_mode {
	SC_MODE_HOST,						/* Run endpoint in Host */
	SC_MODE_DPU						/* Run endpoint in DPU */
};

struct sc_config {
	enum sc_mode mode;					  /* Mode of operation */
	int send_msg_size;					  /* Message size in bytes */
	int send_msg_nb;					  /* Number of messages to send */
	char cc_dev_pci_addr[DOCA_DEVINFO_PCI_ADDR_SIZE];	  /* Comm Channel DOCA device PCI address */
	char cc_dev_rep_pci_addr[DOCA_DEVINFO_REP_PCI_ADDR_SIZE]; /* Comm Channel DOCA device representor PCI address */
};

struct t_results {
	doca_error_t sendto_result;				/* Send thread result */
	doca_error_t recvfrom_result;				/* Receive thread result */
};

struct cc_ctx {
	struct sc_config *cfg;					/* Secure Channel configuration */
	struct doca_comm_channel_ep_t *ep;			/* Comm Channel endpoint ptr */
	struct doca_comm_channel_addr_t *peer;			/* Comm Channel peer address */
	int cc_send_epoll_fd;					/* Comm Channel epoll instance for send thread */
	int cc_recv_epoll_fd;					/* Comm Channel epoll instance for receive thread */
	int cc_send_fd;						/* Comm Channel send file descriptor */
	int cc_recv_fd;						/* Comm Channel receive file descriptor */
	int send_intr_fd;					/* Fd for catching interrupts on send thread*/
	int recv_intr_fd;					/* Fd for catching interrupts for recv thread*/
	pthread_t *sendto_t;					/* Send thread ptr */
	pthread_t *recvfrom_t;					/* Receive thread ptr */
	pthread_mutex_t *mutex;					/* Read/Write mutex */
	struct t_results *results;				/* Final threads result */
};

/*
 * Starts Secure Channel flow
 *
 * @cfg [in]: App configuration structure
 * @ctx [in]: Threads context structure
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t sc_start(struct sc_config *cfg, struct cc_ctx *ctx);

/*
 * Registers Secure Channel parameters
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_secure_channel_params(void);

#endif /* SECURE_CHANNEL_CORE_H_ */
