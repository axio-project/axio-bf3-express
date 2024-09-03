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

doca_dep = dependency('doca')
app_dependencies += doca_dep

# Check if mpicc exists
mpicc = find_program('mpicc', required: false)
if not mpicc.found()
	reason = 'missing mpicc'
	warning('Skipping compilation of @0@'.format(APP_NAME) + ' - ' + reason)
	subdir_done()
endif

if not flag_enable_driver_flexio
	reason = get_variable('flexio_disable_reason')
	warning('Skipping compilation of @0@'.format(APP_NAME) + ' - ' + reason)
	subdir_done()
endif

host_srcs = files([
	'host/' + APP_NAME + '.c',
	'host/' + APP_NAME + '_core.c',
])

host_hdrs = files([
	'host/' + APP_NAME + '_core.h',
])

device_srcs = files([
	'device/' + APP_NAME + '_dev.c',
])

address_santizer = get_option('b_sanitize')

debug_mode = 'none'
if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
	debug_mode = 'true'
endif

build_script = meson.current_source_dir() / 'build_dpa_all_to_all.sh'
app_builds = find_program(build_script)
commmand = [ app_builds, meson.project_build_root(), address_santizer, debug_mode]

custom_target(DOCA_PREFIX + APP_NAME,
		command: commmand,
		output : DOCA_PREFIX + APP_NAME,
		install_dir : bin_install_dir,
		install : install_apps,
		build_by_default: true)

