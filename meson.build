project('NICC', ['C', 'CPP'],
	# Get version number from file.
	version: run_command(find_program('cat'),
		files('./VERSION'), check: true).stdout().strip(),
)

# get process environment
env = environment()

# find pkg-config
pkgconfig = find_program('pkg-config')

# project name
project_name = 'nic_compiler'
project_name_abbreviation = 'nicc'

# compile flags
c_args = []

# load flags
ld_args = []

# root path of the project
root_dir = meson.source_root()

envv = run_command('sh', '-c', 'echo $BUILD_TARGET')
env_build_target = envv.stdout().strip()

# ==================== Third-party Library Detection ====================

# indicator of whether necessary libraries were founded
has_dpdk = false
has_doca = false
has_flexio = false
has_cuda = false

# we must to setup pkgconfig path here
## DPDK
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	dpdk_pc_path = '/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig/'
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
	dpdk_pc_path = '/opt/mellanox/dpdk/lib/aarch64-linux-gnu/pkgconfig'
endif
message('>>> assuming pc file of DPDK is located at ' + dpdk_pc_path)

# DOCA
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	doca_pc_path = '/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig/'
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
	doca_pc_path = '/opt/mellanox/doca/lib/aarch64-linux-gnu/pkgconfig/'
endif
message('>>> assuming pc file of DOCA is located at ' + doca_pc_path)

# FlexIO
flexio_pc_path = '/opt/mellanox/flexio/lib/pkgconfig/'
message('>>> assuming pc file of FlexIO is located at ' + flexio_pc_path)

# Collectx 
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
  collectx_pc_path = '/opt/mellanox/collectx/lib/x86_64-linux-gnu/pkgconfig'
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
  collectx_pc_path = '/opt/mellanox/collectx/lib/aarch64-linux-gnu/pkgconfig'
endif

# mlx5
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	mlx5_pc_path = '/usr/lib/x86_64-linux-gnu/pkgconfig/'
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
	mlx5_pc_path = '/usr/lib/aarch64-linux-gnu/pkgconfig/'
endif
message('>>> assuming pc file of MLX5 is located at ' + mlx5_pc_path)

env.set('PKG_CONFIG_PATH', dpdk_pc_path + ':' + doca_pc_path + ':' + flexio_pc_path + ':' + collectx_pc_path + ':' + mlx5_pc_path)

# >>>>>>>>>>>>>> DPDK >>>>>>>>>>>>>>
dpdk_modules = ['libdpdk', 'libdpdk-libs']
founded_dpdk_modules = []
foreach dpdk_module : dpdk_modules
	dpdk_module_cflags = run_command(pkgconfig, '--cflags', dpdk_module, env: env, check: false)
	 # TODO: when compile with doca, remove '--static' option
	dpdk_module_ldflags = run_command(pkgconfig, '--libs', dpdk_module, env: env, check: false)
	dpdk_module_version = run_command(pkgconfig, '--modversion', dpdk_module, env: env, check: false)
	if dpdk_module_cflags.returncode() != 0 or dpdk_module_cflags.returncode() != 0 or dpdk_module_cflags.returncode() != 0
		message('>>> Failed to find ' + dpdk_module)
	else
		founded_dpdk_modules += dpdk_module
		c_args += dpdk_module_cflags.stdout().split()
		ld_args += dpdk_module_ldflags.stdout().split()
		message('>>>>>> Found ' +  dpdk_module + ', version is ' + dpdk_module_version.stdout().split()[0])
	endif
endforeach
if founded_dpdk_modules.length() == dpdk_modules.length()
	has_dpdk = true
	add_global_arguments('-DNICC_DPDK_ENABLED', language: ['c', 'cpp'])
endif


# >>>>>>>>>>>>>> DOCA >>>>>>>>>>>>>>
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
<<<<<<< HEAD
	doca_pc_path = '/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig/'
	doca_modules = ['doca-common-lib', 'doca-common']
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
	doca_pc_path = '/opt/mellanox/doca/lib/aarch64-linux-gnu/pkgconfig/'
	doca_modules = ['doca-common-lib', 'doca-common']
=======
	doca_modules = ['doca-libs']
elif host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
  # doca 2.5
  # doca_modules = ['doca-grpc-libs', 'doca-libs']

  # doca 2.8
  doca_modules = [
    'doca-aes-gcm',
    'doca-apsh',
    'doca-comch',
    'doca-comm-channel',
    'doca-common',
    'doca-compress',
    'doca-devemu',
    'doca-dma',
    'doca-dpdk-bridge',
    'doca-erasure-coding',
    'doca-eth',
    'doca-flow',
    'doca-pcc',
    'doca-rdma',
    'doca-sha',
    'doca-telemetry-exporter',
    'doca-urom'
  ]
>>>>>>> main
endif

founded_doca_modules = []
foreach doca_module : doca_modules
	doca_module_cflags = run_command(pkgconfig, '--cflags', doca_module, env: env, check: false)
	doca_module_ldflags = run_command(pkgconfig, '--libs', '--static', doca_module, env: env, check: false) # add '--static' option if static link is needed
	doca_module_version = run_command(pkgconfig, '--modversion', doca_module, env: env, check: false)
	if doca_module_cflags.returncode() != 0 or doca_module_ldflags.returncode() != 0 or doca_module_version.returncode() != 0
    message('>>>>>> Failed to find ' + doca_module + ', error message: ' + doca_module_cflags.stderr().strip())
	else
		founded_doca_modules += doca_module
		c_args += doca_module_cflags.stdout().split()
		ld_args += doca_module_ldflags.stdout().split()
		message('>>>>>> Found ' + doca_module + ', version is ' + doca_module_version.stdout().split()[0])
	endif
endforeach
if founded_doca_modules.length() == doca_modules.length()
	has_doca = true
	add_global_arguments('-DNICC_DOCA_ENABLED', language: ['c', 'cpp'])
endif

# >>>>>>>>>>>>>> FlexIO >>>>>>>>>>>>>>
# flexio_modules = ['libflexio-dev', 'libflexio']
flexio_modules = ['libflexio']

founded_flexio_modules = []
foreach flexio_module : flexio_modules
	flexio_module_cflags = run_command(pkgconfig, '--cflags', flexio_module, env: env, check: false)
	flexio_module_ldflags = run_command(pkgconfig, '--libs', '--static', flexio_module, env: env, check: false) # add '--static' option if static link is needed
	flexio_module_version = run_command(pkgconfig, '--modversion', flexio_module, env: env, check: false)
	if flexio_module_cflags.returncode() != 0 or flexio_module_ldflags.returncode() != 0 or flexio_module_version.returncode() != 0
		message('>>>>>> Failed to find ' + flexio_module)
	else
		founded_flexio_modules += flexio_module
		c_args += flexio_module_cflags.stdout().split()
		ld_args += flexio_module_ldflags.stdout().split()
		message('>>>>>> Found ' + flexio_module + ', version is ' + flexio_module_version.stdout().split()[0])
	endif
endforeach
if founded_flexio_modules.length() == flexio_modules.length()
	has_flexio = true
endif

# >>>>>>>>>>>>>> mlx5 >>>>>>>>>>>>>>
mlx5_modules = ['libmlx5', 'libibverbs']
founded_mlx5_modules = []
foreach mlx5_module : mlx5_modules
	mlx5_module_cflags = run_command(pkgconfig, '--cflags', mlx5_module, env: env, check: false)
	mlx5_module_ldflags = run_command(pkgconfig, '--libs', '--static', mlx5_module, env: env, check: false) # add '--static' option if static link is needed
	mlx5_module_version = run_command(pkgconfig, '--modversion', mlx5_module, env: env, check: false)
	if mlx5_module_cflags.returncode() != 0 or mlx5_module_ldflags.returncode() != 0 or mlx5_module_version.returncode() != 0
		message('>>>>>> Failed to find ' + mlx5_module)
	else
		founded_mlx5_modules += mlx5_module
		c_args += mlx5_module_cflags.stdout().split()
		ld_args += mlx5_module_ldflags.stdout().split()
		message('>>>>>> Found ' + mlx5_module + ', version is ' + mlx5_module_version.stdout().split()[0])
	endif
endforeach
if founded_mlx5_modules.length() == mlx5_modules.length()
	has_mlx5 = true
endif

# >>>>>>>>>>>>>> Other Dependencies Found through Compiler >>>>>>>>>>>>>>
dep = []
dep += meson.get_compiler('cpp').find_library('pthread')
# dep += meson.get_compiler('cpp').find_library('gmp')

# >>>>>>>>>>>>>> Other Dependencies Found through cflags >>>>>>>>>>>>>>
if host_machine.cpu_family() == 'arm' or host_machine.cpu_family() == 'aarch64'
	# ref: https://github.com/noloader/SHA-Intrinsics#arm-sha
	c_args += ['-march=armv8-a+crc+crypto']
elif host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	# ref: https://github.com/noloader/SHA-Intrinsics#intel-sha
	c_args += ['-msse4', '-msha']
endif

## Note: we detect different DPDK, DOCA and CUDA version inside meson and source files,
##		so we block all deprecated-declarations warnings here to make log output of 
##		the build process cleaner
c_args += ['-D_GNU_SOURCE', '-Wno-deprecated-declarations', '-g']
c_args += ['--std=c++17', '-Wno-missing-field-initializers']

# necessary dependencies check
assert(has_dpdk, 'couldn\'t find libdpdk')
assert(has_doca, 'couldn\'t find libdoca')
assert(has_flexio, 'couldn\'t find libflexio')
assert(has_mlx5, 'couldn\'t find libmlx5')
# =======================================================================

# ==================== Static Configurations ====================
# switch:   whether to print all fatal messages
enable_print_error = 1

# switch:   whether to print all warning messages
enable_print_warn = 1

# switch:   whether to print all log messages
enable_print_log = 1

# switch:   whether to print all debug messages
enable_print_debug = 1

# switch:   whether to print fatal/warn/debug/log with colors
#           should be disabled when try to dump all outputs to a file
enable_print_with_color = 1

# switch:   whether conduct runtime bug checking
enable_runtime_debug_check = 1
# ===============================================================

message('subdir: ' + env_build_target)
subdir(env_build_target)
