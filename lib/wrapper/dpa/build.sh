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
LIB_DIR=$CUR_DIR/../../dpa/include
KERNEL_DIR=$CUR_DIR/../../../examples/l2_swap

# Input parameters
APP_NAME=l2_swap_wrapper
OUTPUT_NAME=libl2_swap_wrapper
SOURCE_FILE="$CUR_DIR/src/dpa_wrapper.c $KERNEL_DIR/dpa_kernel.dpa.o"
BUILD_DIR="$CUR_DIR/../../../bin"

# Tools location - DPACC, DPA compiler
DOCA_TOOLS="/opt/mellanox/doca/tools"
DPACC="${DOCA_TOOLS}/dpacc"

# CC flags
DEV_CC_FLAGS="-Wall,-Wextra,-Wpedantic,-Werror,-O0,-g,-DE_MODE_LE,-ffreestanding,-mabi=lp64,-mno-relax,-mcmodel=medany,-nostdlib,-Wdouble-promotion"
# DEV_INC_DIR="-I$CUR_DIR/include -I/$LIB_DIR"
DEV_INC_DIR="-I$CUR_DIR/include"
DEVICE_OPTIONS="${DEV_CC_FLAGS},${DEV_INC_DIR}"

# Host flags
HOST_OPTIONS="-Wno-deprecated-declarations"

# Compile the DPA (kernel) device source code using the DPACC
${DPACC} ${SOURCE_FILE} -o "${BUILD_DIR}/${OUTPUT_NAME}.a" \
        -hostcc=gcc \
        -hostcc-options="${HOST_OPTIONS}" \
        --devicecc-options=${DEVICE_OPTIONS} \
        --app-name=${APP_NAME} 

# ${DPACC} ${SOURCE_FILE} -o "lib${APP_NAME}" -gen-libs \
#         -hostcc=gcc \
#         -hostcc-options="${HOST_OPTIONS}" \
#         --devicecc-options=${DEVICE_OPTIONS} \
#         --app-name=${APP_NAME} 
