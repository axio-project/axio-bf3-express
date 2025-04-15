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

# The meson file for build packet processor device side sample application

sample_dev_srcs =  ['flexio_rpc_dev.c']
app_out_name = dev_app_name + '.a'

# Device side DPA application build by meson command custom_target
# through doca_build_dpacc.sh script
sample_dev_target = custom_target('apps_' + dev_app_name,
				  command: [build_dpacc, '--application',
					    '--app_name', dev_app_name,
					    '--srcs', '@INPUT@',
					    '--dpacc_build_dir', '@OUTDIR@',
					    '--additional_dpacc_options', dpacc_options,
					    '--external_cc_options',
					    ','.join(dpacc_devicecc_array)],
				  output: app_out_name,
				  input: sample_dev_srcs)

# Create dependency from DPA application for link with host side sample
sample_device_dep = declare_dependency(link_args :
                                       join_paths(meson.current_build_dir(), app_out_name))
