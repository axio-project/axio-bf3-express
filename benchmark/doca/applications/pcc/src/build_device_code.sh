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
# This script takes 4 arguments:
# arg1: The DOCA directory path
# arg2: The project's build path (for the DPA Device build)
# arg3: Absolute paths of DPA (kernel) device source code directory (our code)
# arg4: Absolute paths of directory of compiled DPA program
# arg5: Name of compiled DPA program

####################
## Configurations ##
####################

DOCA_DIR=$1
PCC_APP_DEVICE_SRC_DIR=$2
APPLICATION_DEVICE_BUILD_DIR=$3
PCC_APP_NAME=$4
DOCA_LIB_DIR=$5

# Tools location - DPACC, DPA compiler
DOCA_INSTALL_DIR="/opt/mellanox/doca"
DOCA_TOOLS="${DOCA_INSTALL_DIR}/tools"
DPACC="${DOCA_TOOLS}/dpacc"

# DOCA Configurations
DOCA_PCC_DEV_LIB_NAME="doca_pcc_dev"
PCC_APP_DEVICE_SRCS=`ls ${PCC_APP_DEVICE_SRC_DIR}/*.c`
DOCA_PCC_DEVICE_ALGO_SRCS=`ls ${PCC_APP_DEVICE_SRC_DIR}/algo/*.c`
PCC_DEVICE_SRC_FILES="${PCC_APP_DEVICE_SRCS} ${DOCA_PCC_DEVICE_ALGO_SRCS}"
DOCA_APP_DEVICE_COMMON_DIR="${DOCA_INSTALL_DIR}/applications/common/src/device/"

# DOCA include list
DOCA_INC_LIST="-I${DOCA_INSTALL_DIR}/include/ -I${DOCA_APP_DEVICE_COMMON_DIR}"

APP_INC_LIST="-I${PCC_APP_DEVICE_SRC_DIR} -I${PCC_APP_DEVICE_SRC_DIR}/algo"

# DPA Configurations
HOST_CC_FLAGS="-Wno-deprecated-declarations -Werror"
DEV_CC_EXTRA_FLAGS="-DSIMX_BUILD,-ffreestanding,-mcmodel=medany,-ggdb,-O2,-DE_MODE_LE,-Wdouble-promotion"
DEVICE_CC_FLAGS="-Wno-deprecated-declarations -Werror -Wall -Wextra -W ${DEV_CC_EXTRA_FLAGS} "

##################
## Script Start ##
##################

rm -rf $APPLICATION_DEVICE_BUILD_DIR
mkdir -p $APPLICATION_DEVICE_BUILD_DIR

# Compile the DPA (kernel) device source code using the DPACC
$DPACC \
-flto \
$PCC_DEVICE_SRC_FILES \
-o ${APPLICATION_DEVICE_BUILD_DIR}/${PCC_APP_NAME}.a \
-hostcc=gcc \
-hostcc-options="${HOST_CC_FLAGS}" \
--devicecc-options="${DEVICE_CC_FLAGS},${DOCA_INC_LIST}" \
-disable-asm-checks \
-device-libs="-L${DOCA_LIB_DIR} -l${DOCA_PCC_DEV_LIB_NAME}" \
--app-name="${PCC_APP_NAME}"
