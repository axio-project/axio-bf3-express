#
# Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
libflexio_host = dependency('libflexio')
app_dependencies += libflexio_host

host_srcs = files([
	'host/' + APP_NAME + '.c',
	'host/' + APP_NAME + '_core.c',
])

device_code_build_script_name = 'build_device_code.sh'
device_code_build_script = meson.current_source_dir() / device_code_build_script_name

pcc_program_lib_name = 'pcc_main_app'
dpacc_build_dir_path = meson.project_build_root() + '/' + APP_NAME + '/device/build_dpacc'

# Command to compile the application
app_builds = find_program(device_code_build_script)

doca_lib_dir = doca_dep.get_variable(pkgconfig : 'libdir')

pcc_app_dev_src_dir = meson.project_source_root() + '/' + APP_NAME + '/src/device/'

# Run DPACC to build app device program
run_command(app_builds, meson.project_source_root(),pcc_app_dev_src_dir,
	dpacc_build_dir_path, pcc_program_lib_name, doca_lib_dir, check: true)

pcc_program_lib = meson.get_compiler('c').find_library(pcc_program_lib_name, dirs : dpacc_build_dir_path, static: true)

# Build executable
doca_pcc_test = executable(DOCA_PREFIX + APP_NAME,
	host_srcs,
	c_args : base_c_args,
	include_directories: [app_inc_dirs, './host'],
	link_with: app_libs,
	install_dir : bin_install_dir,
	dependencies : [app_dependencies, pcc_program_lib],
	install: install_apps
)
