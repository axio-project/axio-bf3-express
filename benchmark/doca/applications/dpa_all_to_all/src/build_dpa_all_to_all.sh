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

# This script uses the mpicc (MPI C compiler) to compile the dpa_all_to_all application
# This script takes 2 arguments:
# arg1: The project's build path
# arg2: Address sanitizer option
# arg3: The installed DOCA lib dir
# arg4: Buildtype

####################
## Configurations ##
####################

APP_NAME="dpa_all_to_all"
MPI_COMPILER="mpicc"

# DOCA Configurations
DOCA_DIR="/opt/mellanox/doca"
DOCA_BUILD_DIR=$1
ADDRESS_SANITIZER_OPTION=$2
DOCA_LIB_DIR=$3
BUILD_TYPE=$4
DOCA_INCLUDE="${DOCA_DIR}/include"
ALL_TO_ALL_DIR="${DOCA_DIR}/applications/$APP_NAME/src"
ALL_TO_ALL_HOST_DIR="${ALL_TO_ALL_DIR}/host"
ALL_TO_ALL_HOST_SRC_FILES="${ALL_TO_ALL_HOST_DIR}/${APP_NAME}.c ${ALL_TO_ALL_HOST_DIR}/${APP_NAME}_core.c"
ALL_TO_ALL_DEVICE_SRC_DIR="${ALL_TO_ALL_DIR}/device"
ALL_TO_ALL_DEVICE_SRC_FILES="${ALL_TO_ALL_DEVICE_SRC_DIR}/${APP_NAME}_dev.c"
ALL_TO_ALL_APP_EXE="${DOCA_BUILD_DIR}/${APP_NAME}/src/doca_${APP_NAME}"
DEVICE_CODE_BUILD_SCRIPT="${ALL_TO_ALL_DIR}/build_device_code.sh"
DEVICE_CODE_LIB="${DOCA_BUILD_DIR}/${APP_NAME}/src/device/build_dpacc/${APP_NAME}_program.a "

# Finalize flags
LINK_FLAGS="-pthread -lm -lflexio -lstdc++ -libverbs -lmlx5"

# If address sanitizer option is not none then add it to the link flags
if [ "$ADDRESS_SANITIZER_OPTION" != "none" ]; then
	LINK_FLAGS="${LINK_FLAGS} -fsanitize=${ADDRESS_SANITIZER_OPTION}"
fi

# If compile in debug mode add -g flag
if [ "$BUILD_TYPE" != "none" ]; then
	LINK_FLAGS="${LINK_FLAGS} -g"
fi

DOCA_FLAGS="-DDOCA_ALLOW_EXPERIMENTAL_API"
DOCA_LINK_FLAGS=`pkg-config --libs doca`

# FlexIO Configurations
MLNX_INSTALL_PATH="/opt/mellanox/"
FLEXIO_LIBS_DIR="${MLNX_INSTALL_PATH}/flexio/lib/"

##################
## Script Start ##
##################

# Compile device code
/bin/bash $DEVICE_CODE_BUILD_SCRIPT $DOCA_BUILD_DIR $ALL_TO_ALL_DEVICE_SRC_FILES $DOCA_LIB_DIR

# Compile application using MPI compiler
$MPI_COMPILER $ALL_TO_ALL_HOST_SRC_FILES -o $ALL_TO_ALL_APP_EXE $DEVICE_CODE_LIB -I$ALL_TO_ALL_HOST_DIR \
	-I$DOCA_INCLUDE -L$FLEXIO_LIBS_DIR $DOCA_FLAGS $DOCA_LINK_FLAGS $LINK_FLAGS
