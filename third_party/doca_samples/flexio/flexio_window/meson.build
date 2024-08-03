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

project('DOCA_SAMPLE', 'C',
	# Get version number from file.
	version: run_command(find_program('cat'),
		files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
	license: 'Proprietary',
	default_options: ['buildtype=debug'],
	meson_version: '>= 0.61.2'
)

libflexio_host = dependency('libflexio')

add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

SAMPLE_NAME='flexio_window'

flexio_build_device = '/opt/mellanox/doca/tools/build_flexio_device.sh'

device_builder = find_program(flexio_build_device,
			required: true)

source_file = meson.current_source_dir() + '/device/flexio_window_device.c'

# Build device code ./build_flexio_device.sh <app name> <source file> <output dir>
dpa_code = custom_target('FLEXIO_WINDOW_DEVICE',
		command : [device_builder,
			SAMPLE_NAME + '_device',
			source_file,
			meson.current_build_dir()],
		output : SAMPLE_NAME + '_device.a')

dpa_dependency = static_library(SAMPLE_NAME + '_device', link_with : dpa_code)

subdir('host')
