/*
 * Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
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

DOCA_LOG_REGISTER(RMAX_SET_CLOCK);

/*
 * Sets PTP clock device to be used internally in DOCA RMAX
 *
 * @pcie_addr [in]: PCIe address, to set the PTP clock capability for
 * @state [in]: a place holder for DOCA core related objects
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t set_clock(const char *pcie_addr, struct program_core_objects *state)
{
	doca_error_t result;

	/* open DOCA device with the given PCI address */
	result = open_doca_device_with_pci(pcie_addr, NULL, &state->dev);
	if (result != DOCA_SUCCESS)
		return result;

	/* DOCA RMAX library Initialization */
	result = doca_rmax_init();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to initialize DOCA RMAX library: %s", doca_error_get_descr(result));
		destroy_core_objects(state);
		return result;
	}

	/* Set the device to use for obtaining PTP time */
	result = doca_rmax_set_clock(state->dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to clock for the device to use for obtaining PTP time.: %s",
			     doca_error_get_descr(result));
		doca_rmax_release();
		return result;
	}

	result = doca_rmax_release();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to clock for the device to use for obtaining PTP time.: %s",
			     doca_error_get_descr(result));
		return result;
	}

	return result;
}
