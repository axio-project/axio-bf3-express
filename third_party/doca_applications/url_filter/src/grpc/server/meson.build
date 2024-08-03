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

grpc_static_srcs = files([
	APP_NAME + '_gRPC_server.cpp',
])

grpc_headers = files([
	'server.hpp',
	'orchestration.hpp',
])

grpc_common_static_srcs = files([
	common_grpc_dir_path + '/log_forwarder.cpp',
])

grpc_inc_dirs  = app_inc_dirs
grpc_inc_dirs += include_directories('../..')

executable(DOCA_PREFIX + APP_NAME + '_grpc', app_srcs, grpc_static_srcs, grpc_common_static_srcs,
			gen_proto_cc, gen_grpc_cc, gen_common_cc, gen_orchestration_cc,
			c_args : grpc_c_args,
			cpp_args : grpc_cpp_args,
			dependencies : app_dependencies + grpc_dependencies,
			link_with : app_libs,
			include_directories: grpc_inc_dirs,
			install_dir : bin_install_grpc_server_dir,
			install: install_apps)
