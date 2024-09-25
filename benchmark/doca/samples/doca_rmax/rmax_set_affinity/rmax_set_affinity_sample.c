/*
 * Copyright (c) 2022-2024 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

#include "rmax_common.h"

DOCA_LOG_REGISTER(RMAX_SET_AFFINITY);

/*
 * Sets the CPU affinity, through DOCA Rivermax API
 *
 * @core [in]: CPU core number.
 * @return: DOCA_SUCCESS on success and DOCA error otherwise
 */
doca_error_t set_affinity(unsigned core)
{
	doca_error_t result, cleanup_result;
	struct doca_rmax_cpu_affinity *mask;

	/* create CPU affinity structure */
	result = doca_rmax_cpu_affinity_create(&mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create CPU affinity: %s", doca_error_get_descr(result));
		return result;
	}
	/* set affinity mask to selected CPU */
	result = doca_rmax_cpu_affinity_set(mask, core);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU in affinity mask: %s", doca_error_get_descr(result));
		goto cleanup;
	}
	/* apply CPU affinity mask */
	result = doca_rmax_set_cpu_affinity_mask(mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set CPU affinity: %s", doca_error_get_descr(result));
		goto cleanup;
	}

cleanup:
	/* destroy CPU affinity structure */
	cleanup_result = doca_rmax_cpu_affinity_destroy(mask);
	if (cleanup_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy CPU affinity: %s", doca_error_get_descr(cleanup_result));
	}
	return result;
}

/*
 * Sets the CPU affinity, through DOCA Rivermax API (main sample logic)
 *
 * @core [in]: CPU core number.
 * @return: DOCA_SUCCESS on success and DOCA error otherwise
 */
doca_error_t set_affinity_sample(unsigned core)
{
	doca_error_t result;

	DOCA_LOG_INFO("Setting internal thread CPU affinity to CPU %u", core);
	result = set_affinity(core);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Error setting CPU affinity: %s", doca_error_get_descr(result));
		return result;
	}

	/* library initialization */
	result = doca_rmax_init();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize DOCA Rivermax: %s", doca_error_get_descr(result));
		return result;
	}

	/* application code here */

	/* deinitialization */
	result = doca_rmax_release();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to deinitialize DOCA Rivermax: %s", doca_error_get_descr(result));
		return result;
	}

	return result;
}
