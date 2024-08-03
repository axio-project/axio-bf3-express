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

app_dependencies += dependency('doca')
app_dependencies += dependency('libdpdk')
app_dependencies += declare_dependency(link_args : '-lresolv')

app_srcs += files([
	'dns_filter_core.c',
	common_dir_path + '/dpdk_utils.c',
	common_dir_path + '/flow_skeleton.c',
	common_dir_path + '/offload_rules.c',
	common_dir_path + '/utils.c',
	samples_dir_path + '/common.c',
])

vanilla_app_srcs = [
	APP_NAME + '.c',
]

# Only relevant if GPU is supported
if get_option('enable_gpu_support')

endif

executable(DOCA_PREFIX + APP_NAME,
	app_srcs + vanilla_app_srcs,
	c_args : base_c_args,
	dependencies : app_dependencies,
	include_directories : app_inc_dirs,
	install: install_apps)

if get_option('enable_gpu_support')
	gpu_app_srcs = files([
		'dns_filter_kernel.cu',
		common_dir_path + '/gpu_init.c',
	])

	executable(DOCA_PREFIX + APP_NAME + GPU_SUFFIX,
		app_srcs + vanilla_app_srcs + gpu_app_srcs,
		c_args : gpu_c_args,
		cuda_args : gpu_cuda_args,
		dependencies : app_dependencies + gpu_dependencies,
		include_directories: app_inc_dirs,
		install: install_apps)
endif

# Only relevant if gRPC is supported
if get_option('enable_grpc_support')
	subdir('grpc')
endif
