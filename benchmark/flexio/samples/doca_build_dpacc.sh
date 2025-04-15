#!/bin/bash

# SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES.
# Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The script used for build FlexIO SDK samples through meson and ninja packages
# The script works in two modes - application (for create DPA application) and
# library (for create DPA libraries )

function help_message() {
	echo "The ${SCRIPT_NAME} uses the ${DPACC_APP} tool (located in ${DOCA_TOOLS})"
	echo "Running: ./${SCRIPT_NAME} build_mode parameters"
	echo "Type of the build_modes:"
	echo " --application - to compile DPA-device code and build host stub lib"
	echo "   For this mode the parameters must be:"
	echo "     --app_name APP_NAME - A name of application"
	echo "     --srcs SRCS - A full path to device sources"
	echo "     --dpacc_build_dir DPACC_BUILD_DIR - A output directory"
	echo "     --external_cc_options EXTERNAL_CC_OPTIONS - Additional options for clang compiler"
	echo "     --hostcc_args HOSTCC_ARGS - Additional options for gcc compiler"
	echo "     --additional_include_directories ADDITIONAL_INCLUDE_DIRECTORIES - Additional include directories"
	echo "     --additional_ld_libs ADDITIONAL_LD_LIBS - Additional ld libs (only name, without lib prefix and .a suffix)"
	echo "     --additional_lib_paths ADDITIONAL_LIB_PATHS - Additional lib paths for ADDITIONAL_LD_LIBS"
	echo "     --additional_dpacc_options ADDITIONAL_DPACC_OPTIONS - Additional dpacc options"
	echo " --library - to create archive for HOST and DPA-device code"
	echo "   For this mode the parameters must be with this order:"
	echo "     --archive_name OUT_FILE - Name of archive"
	echo "     --host_archive_path HOST_ARCHIVE_PATH - The directory there is created host archive will be copied"
	echo "           if HOST_ARCHIVE_PATH is none then host archive will be deleted."
	echo "           if HOST_ARCHIVE_PATH is remain then host archive will be remained."
	echo "     --srcs SRCS - list of object file paths previously created with --compile"
	echo "     --hostcc_args HOSTCC_ARGS - Additional options for gcc compiler"
	echo "     --external_cc_options EXTERNAL_CC_OPTIONS - Additional options for compiler"
	echo "     --additional_include_directories ADDITIONAL_INCLUDE_DIR - Additional include directories for compiler"
	echo "     --additional_dpacc_options ADDITIONAL_DPACC_OPTIONS - Additional dpacc options"
	echo " --allow-experimental-api [yes|no] - support for experimental API"
	echo " --version - print version"
	echo " --help - print this help"
}

function invalid_option() {
	local INVALID_OPTION="$1"
	local BUILD_MODE="$2"

	echo "Invalid option --${INVALID_OPTION} for mode --${BUILD_MODE}"
	exit 1
}

function remove_extras() {
	local STRING=$1
	STRING=$(echo ${STRING} | sed -e "s/,,*/,/g" -e "s/^,//" -e "s/,$//" )
	echo ${STRING}
}

####################
## Configurations ##
####################

SCRIPT_NAME=$(basename ${0})

# Tools location - dpacc
DPACC_PATH="/opt/mellanox/doca/tools/dpacc"

DEV_INC_DIR=""
HOSTCC_ARGS="-fPIC,-DFLEXIO_ALLOW_EXPERIMENTAL_API"

DEV_EXPERIMENTAL_API="-DFLEXIO_DEV_ALLOW_EXPERIMENTAL_API"

# Input parameters
BUILD_MODE=${1}

if [[ "${BUILD_MODE}" == "" || "${BUILD_MODE}" == "-h" ]]; then
	help_message
	exit 1
fi

OPT_BUILD_MODE=$(echo ${BUILD_MODE} | sed -s 's/--//')

OPTION=""
DEV_INC_DIR="-I.,"
EXTERNAL_CC_OPTIONS=""
DEV_LD_LIBS=""
DEV_LIB_DIR=""
SRCS=""
OUT_FILE=""
HOST_ARCHIVE_PATH="none"
ADDITIONAL_DPACC_OPTIONS=""

case ${OPT_BUILD_MODE} in
	application|library)
		shift
		while [[ $# -gt 0 ]]; do
			key="$1"
			shift
			case $key in
				--*)
					OPTION=$(echo $key | sed -s 's/--//')
					;;
				*)
					case $OPTION in
						app_name)
							if [[ "${OPT_BUILD_MODE}" != "application" ]];	then
								invalid_option $OPTION $OPT_BUILD_MODE
							fi
							APP_NAME="$key"
							;;
						output)
							case ${OPT_BUILD_MODE} in
								compile*)
									;;
								*)
									invalid_option $OPTION $OPT_BUILD_MODE
									;;
							esac
							OUT_FILE="$key"
							;;
						archive_name)
							if [[ "${OPT_BUILD_MODE}" != "library" ]]; then
								invalid_option $OPTION $OPT_BUILD_MODE
							fi
							OUT_FILE="$key"
							;;
						host_archive_path)
							if [[ "${OPT_BUILD_MODE}" != "library" ]]; then
								invalid_option $OPTION $OPT_BUILD_MODE
							fi
							HOST_ARCHIVE_PATH="$key"
							;;
						srcs)
							SRCS+="$key "
							;;
						dpacc_build_dir)
							DPACC_BUILD_DIR="$key"
							;;
						external_cc_options)
							EXTERNAL_CC_OPTIONS+="$key,"
							;;
						hostcc_args)
							HOSTCC_ARGS+=" $key,"
							;;
						additional_include_directories)
							DEV_INC_DIR+="-I$key,"
							;;
						additional_ld_libs)
							DEV_LD_LIBS+="-l$key,"
							;;
						additional_lib_paths)
							DEV_LIB_DIR+="-L$key,"
							;;
						additional_dpacc_options)
							ADDITIONAL_DPACC_OPTIONS+=" $key"
							;;
						allow-experimental-api)
							case "${key}" in
								yes)
									;;
								no)
									DEV_EXPERIMENTAL_API=""
									;;
								*)
									echo "Unknown $key for $OPTION"
									exit 1
							esac
							;;
						*)
							echo "Unknown option $key"
							exit 1
							;;
					esac
				;;
			esac
		done
		;;
	version)
		if [ ! -f ${DPACC_PATH} ]; then
			echo "ERROR - File ${DPACC_PATH} does not exist!"
			exit 4
		fi
		echo $(${DPACC_PATH} --version | grep version | sed -s 's/^.*version //')
		exit 0
		;;
	help)
		help_message
		exit 0
		;;
	*)
		echo "Invalid mode ${BUILD_MODE}"
		exit 1
		;;
esac

# CC flags
DEV_FULL_CC_FLAGS=$(remove_extras "${DEV_INC_DIR},${DEV_EXPERIMENTAL_API},${EXTERNAL_CC_OPTIONS}")

# Host flags
HOSTCC_ARGS=$(remove_extras "${HOSTCC_ARGS}")

case ${BUILD_MODE} in
# Application mode - the script run dpacc for create DPA application
# The dpacc run with --app-name flag.
	--application)

		DEV_FULL_LD_FLAGS=$(remove_extras "${DEV_LD_LIBS}")
		DEV_LIB_DIR=$(remove_extras "${DEV_LIB_DIR}")

		# Build directory for build
		mkdir -p ${DPACC_BUILD_DIR}

		# Compile the DPA (kernel) device source code using the dpacc
		${DPACC_PATH} ${SRCS} -o ${DPACC_BUILD_DIR}/${APP_NAME}.a \
			      -hostcc=gcc \
			      --devicecc-options="${DEV_FULL_CC_FLAGS}" \
			      --devicelink-options="${DEV_FULL_LD_FLAGS}" \
			      --device-libs="${DEV_LIB_DIR}" \
			      -hostcc-options="${HOSTCC_ARGS}" \
			      ${ADDITIONAL_DPACC_OPTIONS} \
			      --app-name=${APP_NAME}
		[ $? -ne 0 ] && exit 2
		;;
# Library mode - the script run dpacc for create DPA library
# The dpacc run with --gen-libs flag.
	--library)
		rm -f ${OUT_FILE}
		mkdir -p $(dirname ${OUT_FILE})
		OUTDIRBASE=$(dirname ${OUT_FILE})
		OUTNAME=$(basename ${OUT_FILE})
		LIBNAME=$(echo $OUTNAME | sed -En "s/lib(.*)\.a/\1/p")
		LIBNAME="lib${LIBNAME}"
		DEVLIBNAME="${LIBNAME}_device.a"
		HOSTLIBNAME="${LIBNAME}_host.a"
		rm -f ${DEVLIBNAME} ${HOSTLIBNAME}
		${DPACC_PATH} ${SRCS} --gen-libs \
			      --devicecc-options=${DEV_FULL_CC_FLAGS} \
			      -hostcc=gcc -o ${OUTDIRBASE}/${LIBNAME}\
			      ${ADDITIONAL_DPACC_OPTIONS} \
			      -hostcc-options="${HOSTCC_ARGS}"
		[ $? -ne 0 ] && exit 2
		mv ${OUTDIRBASE}/${DEVLIBNAME} ${OUT_FILE}
		if [[ "${HOST_ARCHIVE_PATH}" == "none" ]]; then
			rm ${OUTDIRBASE}/${HOSTLIBNAME}
			[ $? -ne 0 ] && exit 2
		elif [[ "${HOST_ARCHIVE_PATH}" != "remain" ]]; then
			[ -d ${HOST_ARCHIVE_PATH} ] || mkdir -p ${HOST_ARCHIVE_PATH}
			mv ${OUTDIRBASE}/${HOSTLIBNAME} ${HOST_ARCHIVE_PATH}/${HOSTLIBNAME}
			[ $? -ne 0 ] && exit 2
		fi
		;;
	*)
		;;
esac
exit 0
