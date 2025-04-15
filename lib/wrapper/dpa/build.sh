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

# Parse the example name from first argument or use rdma_simple as default
EXAMPLE=${1:-rdma_simple}

CUR_DIR=$(pwd)
LIB_UTIL_DIR=$CUR_DIR/../../common
KERNEL_DIR=$CUR_DIR/../../../examples/$EXAMPLE

# Check if the kernel directory exists
if [ ! -d "$KERNEL_DIR" ]; then
    echo "Error: Example directory $KERNEL_DIR not found!"
    exit 1
fi

# Check if dpa_kernel.dpa.o file exists in the kernel directory
if [ ! -f "$KERNEL_DIR/dpa_kernel.dpa.o" ]; then
    echo "Error: dpa_kernel.dpa.o not found in $KERNEL_DIR!"
    echo "Please make sure the example has been built successfully first."
    exit 1
fi

# Input parameters
APP_NAME=l2_swap_wrapper
OUTPUT_NAME=libl2_swap_wrapper
SOURCE_FILE="$CUR_DIR/src/dpa_wrapper.c $KERNEL_DIR/dpa_kernel.dpa.o"
BUILD_DIR="$CUR_DIR/../../../bin"

# Tools location - DPACC, DPA compiler
DOCA_TOOLS="/opt/mellanox/doca/tools"
DPACC="${DOCA_TOOLS}/dpacc"

# CC flags
# DEV_CC_FLAGS="-Wall,-Wextra,-Wpedantic,-Werror,-O0,-g,-DE_MODE_LE,-ffreestanding,-mabi=lp64,-mno-relax,-mcmodel=medany,-nostdlib,-Wdouble-promotion"
DEV_CC_FLAGS="-Wall,-Wextra,-Wpedantic,-Werror,-Wdouble-promotion,-O2,-g,-DE_MODE_LE,-ffreestanding,-mno-relax,-mcmodel=medany"
DEV_INC_DIR="-I$CUR_DIR/include -I$LIB_UTIL_DIR"
DEVICE_OPTIONS="${DEV_CC_FLAGS},${DEV_INC_DIR}"

# Host flags
HOST_OPTIONS="-Wno-deprecated-declarations -Werror -Wall -Wextra"

# Print info about the build
echo "Building wrapper with example: $EXAMPLE"
echo "Using kernel object: $KERNEL_DIR/dpa_kernel.dpa.o"

# Compile the DPA (kernel) device source code using the DPACC
${DPACC} ${SOURCE_FILE} -o "${BUILD_DIR}/${OUTPUT_NAME}.a" \
        -hostcc=gcc \
        -mcpu=nv-dpa-bf3 \
        -hostcc-options="${HOST_OPTIONS}" \
        --devicecc-options=${DEVICE_OPTIONS} \
        --app-name=${APP_NAME} \
        -flto

# ${DPACC} ${SOURCE_FILE} -o "lib${APP_NAME}" -gen-libs \
#         -hostcc=gcc \
#         -hostcc-options="${HOST_OPTIONS}" \
#         --devicecc-options=${DEVICE_OPTIONS} \
#         --app-name=${APP_NAME} 
