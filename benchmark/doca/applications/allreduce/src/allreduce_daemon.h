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

#ifndef ALLREDUCE_SERVER_H_
#define ALLREDUCE_SERVER_H_

#include "allreduce_core.h"

/*
 * Starts the daemon routine - listen to a port for incoming connections and progress UCX communication.
 * All requests will be handled by custom communication callbacks.
 */
void daemon_run(void);

#endif /** ALLREDUCE_SERVER_H_ */
