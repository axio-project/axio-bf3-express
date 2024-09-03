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

#ifndef ALLREDUCE_UTILS_H_
#define ALLREDUCE_UTILS_H_

#include <stdlib.h>

#include <doca_log.h>

/*
 * Signal from "exit" will be caught by a signal handler,
 * then a graceful termination will be performed
 */
#define ALLREDUCE_EXIT(format, ...)				\
	do {							\
		DOCA_LOG_ERR(format "\n", ##__VA_ARGS__);	\
		exit(1);					\
	} while (0)

#endif /* ALLREDUCE_UTILS_H_ */
