/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef COMMON_DEVICE_UTILS_H_
#define COMMON_DEVICE_UTILS_H_

#ifndef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))	/* Return the minimum value between X and Y */
#endif

#ifndef MAX
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))	/* Return the maximum value between X and Y */
#endif

#ifndef likely
#define likely(x)       __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x)     __builtin_expect((x), 0)
#endif

#endif /* COMMON_DEVICE_UTILS_H_ */
