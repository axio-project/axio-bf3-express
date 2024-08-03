#!/bin/bash

#
# Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

# This script uses the dpacc tool (located in /opt/mellanox/doca/tools/dpacc) to compile DPA kernels device code.
# This script takes 4 arguments:
# arg1: The project's build path (for the DPA Device build)
# arg2: DOCA lib path
# arg3: Absolute paths of all DPA (kernel) device source code *files* (our code)
# arg4: The sample name

####################
## Configurations ##
####################

PROJECT_BUILD_DIR=$1
DOCA_LIB_DIR=$2
DPA_KERNELS_DEVICE_SRC=$3
SAMPLE_NAME=$4

# DOCA Configurations
DOCA_DIR="/opt/mellanox/doca"
DOCA_INCLUDE="${DOCA_DIR}/include"
DOCA_TOOLS="${DOCA_DIR}/tools"
DOCA_DPACC="${DOCA_TOOLS}/dpacc"
DOCA_DPA_DEV_LIB_NAME="doca_dpa_dev"

# DOCA DPA APP Configuration
# This variable name passed to DPACC with --app-name parameter and it's token must be idintical to the
# struct doca_dpa_app parameter passed to doca_dpa_create(), i.e.
# doca_error_t doca_dpa_create(..., struct doca_dpa_app *${DPA_APP_NAME}, ...);
DPA_APP_NAME="dpa_sample_app"

# DPA Configurations
DEVICE_CC_FLAGS="-D__linux__"

##################
## Script Start ##
##################

# Build directory for the DPA device (kernel) code
SAMPLE_DEVICE_BUILD_DIR="${PROJECT_BUILD_DIR}/${SAMPLE_NAME}/device/build_dpacc"

rm -rf ${SAMPLE_DEVICE_BUILD_DIR}
mkdir -p ${SAMPLE_DEVICE_BUILD_DIR}

# Compile the DPA (kernel) device source code using the DPACC
$DOCA_DPACC $DPA_KERNELS_DEVICE_SRC \
        -o ${SAMPLE_DEVICE_BUILD_DIR}/dpa_program.a \
        -hostcc=gcc \
        --devicecc-options=${DEVICE_CC_FLAGS} \
        -device-libs="-L${DOCA_LIB_DIR} -l${DOCA_DPA_DEV_LIB_NAME}" \
        --app-name="${DPA_APP_NAME}" \
        -flto \
        -I ${DOCA_INCLUDE}
