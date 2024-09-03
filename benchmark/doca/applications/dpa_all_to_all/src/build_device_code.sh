#!/bin/bash

#
# Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

set -e

# This script uses the dpacc tool (located in /opt/mellanox/doca/tools/dpacc) to compile DPA kernels device code (for DPA samples).
# This script takes 2 arguments:
# arg1: The project's build path (for the DPA Device build)
# arg2: Absolute paths of DPA (kernel) device source code *file* (our code)

####################
## Configurations ##
####################

DOCA_BUILD_DIR=$1
DPA_KERNELS_DEVICE_SRC=$2


# DOCA Configurations
DOCA_DIR="/opt/mellanox/doca"
DOCA_TOOLS="${DOCA_DIR}/tools"
DOCA_DPACC="${DOCA_TOOLS}/dpacc"

# DOCA DPA APP Configuration
# This variable name passed to DPACC with --app-name parameter and it's token must be idintical to the
# struct doca_dpa_app parameter passed to doca_dpa_create(), i.e.
# doca_error_t doca_dpa_create(..., struct doca_dpa_app *${DPA_APP_NAME}, ...);
DPA_APP_NAME="dpa_all2all_app"

# DPA Configurations
HOST_CC_FLAGS="-Wno-deprecated-declarations -Werror"
DEVICE_CC_FLAGS="-Wno-deprecated-declarations -Werror"

##################
## Script Start ##
##################

# Build directory for the DPA device (kernel) code
APPLICATION_DEVICE_BUILD_DIR="${DOCA_BUILD_DIR}/dpa_all_to_all/src/device/build_dpacc"

rm -rf $APPLICATION_DEVICE_BUILD_DIR
mkdir -p $APPLICATION_DEVICE_BUILD_DIR

# Compile the DPA (kernel) device source code using the DPACC
$DOCA_DPACC $DPA_KERNELS_DEVICE_SRC \
	-o ${APPLICATION_DEVICE_BUILD_DIR}/dpa_all_to_all_program.a \
	-hostcc=gcc \
	-hostcc-options="${HOST_CC_FLAGS}" \
	--devicecc-options="${DEVICE_CC_FLAGS}" \
	--app-name="${DPA_APP_NAME}" \
	-flto \
	-ldpa
