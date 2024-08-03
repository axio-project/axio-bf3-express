#
# Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

if host_machine.cpu_family().startswith('x86')
	warning('@0@ is not supported over x86' .format(APP_NAME))
endif

app_dependencies += dependency('doca')

app_srcs += [
	'app_shield_agent_core.c',
	common_dir_path + '/utils.c',
	samples_dir_path + '/common.c',
]

vanilla_app_srcs = [
	APP_NAME + '.c',
]

executable(DOCA_PREFIX + APP_NAME,
	app_srcs + vanilla_app_srcs,
	c_args : base_c_args,
	dependencies : app_dependencies,
	include_directories : app_inc_dirs,
	install: install_apps)
