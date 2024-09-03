#
# Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

project('DOCA_APPLICATIONS', 'C', 'CPP',
	# Get version number from file.
	version: run_command(find_program('cat'), files('VERSION'), check: true).stdout().strip(),
	license: 'Proprietary',
	default_options: [
		'buildtype=debug',
		'cpp_std=c++11'
	],
	meson_version: '>= 0.61.2'
)

DOCA_PREFIX = 'doca_'
GPU_SUFFIX = '_gpu'

# With enabling GPU support, languages variable will contain CUDA language
languages = ['c', 'cpp']
if get_option('enable_gpu_support')
	# Enables ifdef for GPU code
	gpu_compile_flags = ['-DGPU_SUPPORT']

	add_languages('CUDA')
	languages += ['cuda']
	cuda = import('unstable-cuda')
	nvcc = meson.get_compiler('cuda')

	nvcc_flags = []
	nvcc_flags += ['-gencode', 'arch=compute_80,code=sm_80']
	nvcc_flags += ['-gencode', 'arch=compute_90,code=sm_90']
	add_project_arguments(nvcc_flags, language: 'cuda')

	# Refer to https://mesonbuild.com/Cuda-module.html
	add_project_arguments('-forward-unknown-to-host-compiler', language: 'cuda')
	if get_option('enable_cross_compilation_to_dpu')
		add_project_link_arguments(['-forward-unknown-to-host-linker', '--allow-shlib-undefined'], language : 'cuda')
	endif
	add_project_arguments('-rdc=true', language: 'cuda')

	gpu_dependencies = []
	gpu_dependencies += dependency('cuda', version: '>= 11-8', modules: ['cudart', 'cuda'])

	nvcc_flags_link = []
	nvcc_flags_link += ['-gencode=arch=compute_80,code=sm_80']
	nvcc_flags_link += ['-gencode=arch=compute_90,code=sm_90']
	add_project_link_arguments(nvcc_flags_link, language: 'cuda')
endif

install_apps = false
bin_install_dir = ''

base_c_args = [
	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
]

base_cpp_args = [
	'-Wno-missing-field-initializers',
]

grpc_c_args = base_c_args + [
	'-Wno-null-dereference',
]

grpc_cpp_args = base_cpp_args + [
	'-Wno-null-dereference',
]

if get_option('enable_gpu_support')
	gpu_c_args = base_c_args + gpu_compile_flags
	gpu_cpp_args = base_cpp_args + gpu_compile_flags
	gpu_cuda_args = gpu_compile_flags
endif

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: languages)

if get_option('enable_deprecated_features')
	# Comment this line to restore warnings of deprecated DOCA features
	add_project_arguments('-D DOCA_ALLOW_DEPRECATED_API', language: languages)
endif

if get_option('enable_trace_log')
	add_project_arguments('-D DOCA_LOGGING_ALLOW_TRACE', language: languages)
endif

flag_enable_driver_flexio = get_option('enable_driver_flexio')

if not flag_enable_driver_flexio
	set_variable('flexio_disable_reason', 'Disabled by user')
endif

if get_option('enable_cross_compilation_to_dpu')
	# Please update this folder if the base cross-compilation folder is located elsewhere
	add_project_arguments('-I/root/doca-cross/usr/include', language: languages)
	# DPA Compiler doesn't fully support cross compilation yet
	if flag_enable_driver_flexio
		flag_enable_driver_flexio = false
		set_variable('flexio_disable_reason', 'Cross compilation is not supported for FlexIO applications')
	endif
endif

# Resolve irrelevant compiler warnings
add_project_arguments('-Wno-format-zero-length', language: languages)

base_app_dependencies = []
base_app_dependencies += dependency('threads')
base_app_dependencies += dependency('json-c')
dependency_libbsd = dependency('libbsd', required: false)
if dependency_libbsd.found()
	base_app_dependencies += dependency_libbsd
	add_project_arguments('-D DOCA_USE_LIBBSD', language: languages)
	# Ensure mlnx-dpdk will manage to find our libbsd if exists
	add_project_arguments('-D RTE_USE_LIBBSD', language: languages)
endif

common_path = 'common/src'
common_dir_path = '../../' + common_path
base_app_inc_dirs = [
	include_directories(common_path),
	include_directories('..'),
]
samples_dir_path = '../../../samples'

# Add the relevant GPU stuff to have a successful compilation with GPU support.
apps_gpu_common_src = files([
	common_path + '/gpu_init.c',
])

source_path = 'src'
bin_path = 'bin'

app_list = [
	    'allreduce',
	    'app_shield_agent',
	    'dma_copy',
	    'dpa_all_to_all',
	    'file_compression',
	    'file_integrity',
	    'firewall',
	    'gpu_packet_processing',
	    'ipsec_security_gw',
	    'l2_reflector',
	    'nat',
	    'pcc',
	    'secure_channel',
	    'simple_fwd_vnf',
	    'switch',
	    'yara_inspection',
]

fs = import('fs')
foreach APP_NAME : app_list
	# Some apps are only present on a subset of installations
	if not fs.is_dir(APP_NAME)
		continue
	endif
	if (not get_option('enable_' + APP_NAME) and not get_option('enable_all_applications'))
		warning('Skipping compilation of DOCA Application - @0@.'.format(APP_NAME))
		continue
	endif

	# Basic app definitions
	app_dependencies = base_app_dependencies
	app_inc_dirs = base_app_inc_dirs
	app_libs = []
	app_srcs = []

	# Enter the application's directory
	subdir(APP_NAME + '/' + source_path)

endforeach
