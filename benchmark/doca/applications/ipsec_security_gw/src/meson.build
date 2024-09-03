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

app_dependencies += dependency('doca')
app_dependencies += dependency('libdpdk')

app_srcs += files([
	'config.c',
	'flow_common.c',
	'flow_decrypt.c',
	'flow_encrypt.c',
	'flow_encrypt.c',
	'ipsec_ctx.c',
	'policy.c',
	common_dir_path + '/dpdk_utils.c',
	common_dir_path + '/pack.c',
	common_dir_path + '/utils.c',
	common_dir_path + '/flow_parser.c',
	samples_dir_path + '/common.c',
])

vanilla_app_srcs = [
	APP_NAME + '.c',
]

executable(DOCA_PREFIX + APP_NAME,
	app_srcs + vanilla_app_srcs,
	dependencies : app_dependencies,
	include_directories : app_inc_dirs,
	install: install_apps)
