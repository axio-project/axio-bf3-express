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

# This script uses the dpacc tool (located in /opt/mellanox/doca/tools/dpacc) to compile DPA device
# code and build host stub lib.
# This script takes 3 arguments:
# arg1: Application name - The application's name, this name is used to create flexio_app struct
# arg2: Source file - Device source code
# arg3: Directory to install the DPA Device build, final output is <arg2>/<arg1>.a

CUR_DIR=$(pwd)
LIB_DIR=$CUR_DIR/../../lib/common

# Input parameters
SOURCE_FILE="$CUR_DIR/src/dpa_kernel.c"
BUILD_DIR=$CUR_DIR

# Tools location - DPACC, DPA compiler
DOCA_TOOLS="/opt/mellanox/doca/tools"
DPACC="${DOCA_TOOLS}/dpacc"

# Device CC flags
DEV_CC_FLAGS="-DE_MODE_LE,-DFLEXIO_DEV_ALLOW_EXPERIMENTAL_API,-Wall,-Wextra,-Wpedantic,-Wdouble-promotion,-Wno-empty-translation-unit,-Wmissing-prototypes,-Wno-unused-function,-Wstrict-prototypes,-ffreestanding,-mcmodel=medany,-g,-O2,-gdwarf-4,-Werror" # error on warnings

DEV_INC_DIR="-I$CUR_DIR/include -I$LIB_DIR"
DEVICE_OPTIONS="${DEV_CC_FLAGS},${DEV_INC_DIR}"

# Host flags
HOST_OPTIONS="-fPIC,-DFLEXIO_ALLOW_EXPERIMENTAL_API,-Wno-deprecated-declarations,-Werror,-Wall,-Wextra"

# Compile the DPA (kernel) device source code using the DPACC
${DPACC} ${SOURCE_FILE} -c \
        -hostcc=gcc \
        -mcpu=nv-dpa-bf3 \
        -hostcc-options="${HOST_OPTIONS}" \
        --devicecc-options=${DEVICE_OPTIONS} \
        -flto
