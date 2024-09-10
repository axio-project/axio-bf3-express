#!/bin/bash
#
# Copyright (c) 2022 NVIDIA CORPORATION AND AFFILIATES.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted
# provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright notice, this list of
#       conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice, this list of
#       conditions and the following disclaimer in the documentation and/or other materials
#       provided with the distribution.
#     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
#       to endorse or promote products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
# FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

# This script uses the dpacc tool (located in /opt/mellanox/doca/tools/dpacc) to compile DPA device
# code and build host stub lib.
# This script takes 3 arguments:
# arg1: Application name - The application's name, this name is used to create flexio_app struct
# arg2: Source file - Device source code
# arg3: Directory to install the DPA Device build, final output is <arg2>/<arg1>.a

# Input parameters
APP_NAME=$1
SOURCE_FILE=$2
BUILD_DIR=$3

# Tools location - DPACC, DPA compiler
DOCA_TOOLS="/opt/mellanox/doca/tools"
DPACC="${DOCA_TOOLS}/dpacc"

# CC flags
DEV_CC_FLAGS="-Wall,-Wextra,-Wpedantic,-Werror,-O0,-g,-DE_MODE_LE,-ffreestanding,-mabi=lp64,-mno-relax,-mcmodel=medany,-nostdlib,-Wdouble-promotion"
DEV_INC_DIR="-I/opt/mellanox/flexio/include"
DEVICE_OPTIONS="${DEV_CC_FLAGS},${DEV_INC_DIR}"

# Host flags
HOST_OPTIONS="-Wno-deprecated-declarations"

# Compile the DPA (kernel) device source code using the DPACC
${DPACC} ${SOURCE_FILE} -o "${BUILD_DIR}/${APP_NAME}.a" \
        -hostcc=gcc \
	-hostcc-options="${HOST_OPTIONS}" \
        --devicecc-options=${DEVICE_OPTIONS} \
	--app-name=${APP_NAME}
