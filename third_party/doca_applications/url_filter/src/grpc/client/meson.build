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

install_grpc_client_bins = []
install_grpc_client_srcs = files([DOCA_PREFIX + APP_NAME + '_gRPC_client.py'])

client_targets = [
	[APP_NAME + '_gRPC_client', install_grpc_client_srcs],
	[APP_NAME + '_py_common', gen_common_py],
	[APP_NAME + '_py_proto', gen_proto_py],
	[APP_NAME + '_py_grpc', gen_grpc_py]
]

# Hack to force the generation of non-executable generated files
foreach gen : client_targets
	py_proc = custom_target(gen[0],
				command: [ python_generator, '@INPUT@', '@OUTPUT@' ],
				input : gen[1],
				output : '@BASENAME@.py',
				install_dir : bin_install_grpc_client_dir,
				install : install_apps,
				build_by_default : true)
endforeach
