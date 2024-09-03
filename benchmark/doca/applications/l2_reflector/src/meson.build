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

libflexio_host = dependency('libflexio')

if not flag_enable_driver_flexio
	reason = get_variable('flexio_disable_reason')
	warning('Skipping compilation of @0@'.format(APP_NAME) + ' - ' + reason)
	subdir_done()
endif

flexio_build_device = '/opt/mellanox/doca/tools/build_flexio_device.sh'

device_builder = find_program(flexio_build_device,
			required: true)

source_file = meson.current_source_dir() + '/device/l2_reflector_device.c'

# Build device code ./build_flexio_device.sh <app name> <source file> <output dir>
dpa_code = custom_target('L2_REFLECTOR_DEVICE',
		command : [device_builder,
			APP_NAME + '_device',
			source_file,
			meson.current_build_dir()],
		output : APP_NAME + '_device.a')

dpa_dependency = static_library(APP_NAME + '_device', link_with : dpa_code)

old_common_dir_path = common_dir_path
common_dir_path = '../../../common/src'
subdir('host')
common_dir_path = old_common_dir_path
