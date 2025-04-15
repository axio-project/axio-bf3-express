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

# The meson file for build device side DPA library for samples

common_dev_sources = [
	'com_dev.c'
]

common_dev_name = 'commondev'
common_dev_full_name = 'lib' + common_dev_name + '.a'
dpacc_mode = '--library'
common_dev_include_dir = meson.current_source_dir()
common_dev_lib_path = meson.current_build_dir()

common_dev_includes = [join_paths(common_dev_include_dir, 'com_dev.h')]

# Device side DPA library build by meson command custom_target
# through doca_build_dpacc.sh script
common_dev_target = custom_target('archive_' + common_dev_name,
				  command: [build_dpacc, '--library',
					    '--archive_name', '@OUTPUT@',
					    '--external_cc_options', ','.join(dpacc_devicecc_array),
					    '--additional_dpacc_options', dpacc_options,
					    '--srcs', '@INPUT@'],
				  output: common_dev_full_name,
				  input: common_dev_sources,
				  depend_files: common_dev_includes)
