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

# The script used for check compatibility to build FlexIO SDK samples on
# the current configuration

function create_c_file {
	cat <<EOT >> $1
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <libflexio/flexio.h>

int main(void)
{
	ibv_get_device_index(NULL);

	mlx5dv_sched_leaf_destroy(NULL);

	flexio_event_handler_destroy(NULL);

	return 0;
}
EOT
}
function create_meson_file {
	cat <<EOT >> $1
project('flexio', 'c',
	license : 'NVIDIA Proprietary',
	version : '1.0'
)

c = meson.get_compiler('c')

ibverbs_dep = dependency('libibverbs', required: true)
mlx5_dep = dependency('libmlx5', required: true)
thread_dep = dependency('threads', required: true)
flexio_dep = dependency('libflexio', required: true)

test_host_src='test_host.c'
output_host_file='test_host'

executable(output_host_file, [test_host_src],
	native: true,
	dependencies: [ibverbs_dep, mlx5_dep, flexio_dep],
)
EOT
}

function check_exist {
	local PROG=$1
	local OUT=$(command -v $1)

	if [ "$OUT" == "" ]; then
		echo "Cannot find $PROG. Please install and re-run"
		exit 1
	fi
}

function version {
	echo "$@" | awk -F'[.-]' '{ printf("%04d%04d%04d%04d\n", $1,$2,$3,$4); }'
}

function check_version {
	local METHOD=$1
	local PROG=$2
	local MINIMAL_VERSION=$3
	local OUT=

	if [ $METHOD -eq 0 ]; then
		OUT=$($PROG --version)
	elif [ $METHOD -eq 1 ]; then
		OUT=$($PROG --version | grep version | sed -s 's/^.*version //')
	elif [ $METHOD -eq 2 ]; then
		OUT=$($PROG --version | grep gcc | sed -s 's/^.*)\s//')
	elif [ $METHOD -eq 3 ]; then
		OUT=$(pkg-config --modversion lib${PROG})
	elif [ $METHOD -eq 4 ]; then
		OUT=$($PROG --version | grep clang | sed -e 's/^.*version //; s/-.*$//')
	else
		echo "Unknown method $METHOD"
		exit 2
	fi

	if [ "$OUT" == "" ]; then
		echo "Can not read version from $PROG with method $METHOD"
		exit 1
	fi

	if [[ $(version $OUT) < $(version $MINIMAL_VERSION) ]]; then
		echo "Version $OUT of $PROG less than minimal $MINIMAL_VERSION"
		echo "Please install new version and re-run"
		exit 1
	fi
}

function check_package {
	local PACKAGE=$1

	OUT=$(pkg-config --libs lib${PACKAGE} 2>/dev/null)
	if [ "$OUT" == "" ]; then
		echo "Package $PACKAGE does not installed. Please install and re-run"
		exit 1
	fi
}

check_exist meson
check_version 0 meson "0.60.0"
check_exist ninja
if [ "${CC}" == "" ]; then
	CC=gcc
fi
if [ "${CC##*clang}" == "" ]; then
	check_exist ${CC}
	check_version 4 ${CC} "7.0"
else
	if [ "${CC##*gcc}" != "" ]; then
		echo "Illegal compiler ${CC}"
		exit 1
	fi
	check_exist ${CC}
	check_version 2 ${CC} "7.0"
fi
check_exist python3
check_exist pkg-config
check_exist /opt/mellanox/doca/tools/dpacc
check_version 1 /opt/mellanox/doca/tools/dpacc "1.8.0"
check_exist /opt/mellanox/doca/tools/dpa-clang
check_version 1 /opt/mellanox/doca/tools/dpa-clang "1.8.0"
check_package ibverbs
check_package mlx5
check_version 3 mlx5
check_package flexio

TMPDIR=/tmp/check_comp
rm -rf ${TMPDIR}
mkdir ${TMPDIR}
create_c_file ${TMPDIR}/test_host.c
create_meson_file ${TMPDIR}/meson.build
cd ${TMPDIR}
meson setup build
if [ $? -ne 0 ]; then
	exit 1
fi
ninja -C build
if [ $? -ne 0 ]; then
	exit 1
fi
rm -rf ${TMPDIR}

echo
echo "***************************"
echo "* Compatible check passed *"
echo "***************************"
exit 0