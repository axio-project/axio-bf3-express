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

#ifndef UROM_RDMO_CORE_H_
#define UROM_RDMO_CORE_H_

#include <doca_log.h>
#include <doca_error.h>
#include <limits.h>

#include <doca_urom.h>

#include "urom_common.h"

/* RDMO applications modes */
enum rdmo_mode {
	RDMO_MODE_UNKNOWN, /* RDMO unknown mode */
	RDMO_MODE_SERVER,  /* RDMO server mode */
	RDMO_MODE_CLIENT   /* RDMO client mode */
};

/* RDMO configuration structure */
struct rdmo_cfg {
	struct urom_common_cfg common;	 /* UROM common configuration file */
	enum rdmo_mode mode;		 /* Node running mode {server, client} */
	char server_name[HOST_NAME_MAX]; /* Server name */
};

/*
 * RDMO server main function
 *
 * @device_name [in]: UROM device name
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdmo_server(char *device_name);

/*
 * RDMO client main function
 *
 * @server_name [in]: RDMO server name
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t rdmo_client(char *server_name);

/*
 * Register RDMO application arguments
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t register_urom_rdmo_params(void);

#endif /* UROM_RDMO_CORE_H_ */
