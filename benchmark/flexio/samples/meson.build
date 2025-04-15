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

# The root meson file for samples project.

project('flexio-sdk samples', 'c',
	license : 'NVIDIA Proprietary',
	version : '0.1.0'
)

# Dependencies for host side samples
ibverbs_dep = dependency('libibverbs', required: true)
mlx5_dep = dependency('libmlx5', required: true)
thread_dep = dependency('threads', required: true)
flexio_dep = dependency('libflexio', required: true)

# Use native C compiler for build host side
c = meson.get_compiler('c', native: true)
# Add warnings to host compile
add_project_arguments('-Wmissing-prototypes', language:'c', native: true)
add_project_arguments('-Wstrict-prototypes', language:'c', native: true)

# Get program for build device side library and applications
build_dpacc = find_program('doca_build_dpacc.sh', required: true)

# List of arguments that should be pass to build_dpacc
dpacc_devicecc_array = ['-DE_MODE_LE',
			'-Wall',
			'-Wextra',
			'-Wpedantic',
			'-Wdouble-promotion',
			'-Wno-empty-translation-unit',
			'-Wmissing-prototypes',
			'-Wstrict-prototypes',
			'-ffreestanding',
			'-mcmodel=medany',]

# Set optimization type for device side libraries and applications
# according of optimization type of host side
meson_build_type = get_option('buildtype')


# Set HW model types for executables build
dpa_cpus = get_option('cpu')
dpa_cpus_str = '-mcpu=nv-dpa-' + ',nv-dpa-'.join(dpa_cpus)
dpacc_options = [dpa_cpus_str]

if (meson_build_type == 'debug')
	dpacc_devicecc_array += ['-g', '-O0', '-gdwarf-4']
elif (meson_build_type == 'debugoptimized' or meson_build_type == '')
	dpacc_devicecc_array += ['-g', '-O2', '-gdwarf-4']
	dpacc_options += ['-flto']
elif (meson_build_type == 'release')
	dpacc_devicecc_array += ['-O3']
	dpacc_options += ['-flto']
else
	error('Unsupported build mode ', meson_build_type)
endif

# Set or unset warnings as error for device side libraries and applications
# according of optimization type of host side
if (get_option('werror'))
	dpacc_devicecc_array += ['-Werror']
else
	dpacc_devicecc_array += ['-Wno-error']
endif

# List of the samples
samples = [
	'flexio_rpc',
	'packet_processor',
]

# Build common host and device side libraries
subdir('common')

# Build the samples
foreach sample: samples
	subdir(sample)
endforeach
