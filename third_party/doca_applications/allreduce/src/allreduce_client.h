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

#ifndef ALLREDUCE_CLIENT_H_
#define ALLREDUCE_CLIENT_H_

#include "allreduce_core.h"

/*
 * Starts the client routine - performs Allreduce operations and some calculations at the same time, in order to display
 * performance metrics
 */
void client_run(void);

#endif /* ALLREDUCE_CLIENT_H_ */
