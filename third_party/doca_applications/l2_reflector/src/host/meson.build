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

app_dependencies += [libflexio_host]
app_dependencies += dependency('libibverbs', required : true)
app_dependencies += dependency('libmlx5', required : true)
app_dependencies += dependency('doca')

srcs = files([
	APP_NAME + '.c',
	APP_NAME + '_core.c',
	common_dir_path + '/utils.c',
])

executable(APP_NAME,
	srcs,
	c_args : base_c_args,
	include_directories: app_inc_dirs,
	dependencies: app_dependencies,
	link_with : app_libs + dpa_dependency,
	install_dir : bin_install_dir,
	install: install_apps
)
