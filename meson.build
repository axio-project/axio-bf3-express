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

# ==================== Third-party Library Detection ====================

# indicator of whether necessary libraries were founded
has_dpdk = false
has_doca = false
has_flexio = false
has_cuda = false

# >>>>>>>>>>>>>> DPDK >>>>>>>>>>>>>>
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	dpdk_pc_path = '/home/ubuntu/git_repos/dpdk/dpdk-stable-22.11.5/build/install/lib/x86_64-linux-gnu/pkgconfig'
elif host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	dpdk_pc_path = '/opt/mellanox/dpdk/lib/aarch64-linux-gnu/pkgconfig'
endif
# dpdk_pc_path = '/usr/lib/dpdk/lib/x86_64-linux-gnu/pkgconfig/'
# dpdk_pc_path = meson.current_source_dir() + '/' + 'third_party/dpdk-21.11/install/lib/x86_64-linux-gnu/pkgconfig/'
# dpdk_pc_path = '/opt/mellanox/dpdk/lib/aarch64-linux-gnu/pkgconfig'
dpdk_modules = ['libdpdk', 'libdpdk-libs']
founded_dpdk_modules = []
env.set('PKG_CONFIG_PATH', dpdk_pc_path)
message('>>> Detecting DPDK, assume pc file of DPDK is located at ' + dpdk_pc_path)
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
endif


# >>>>>>>>>>>>>> DOCA >>>>>>>>>>>>>>
if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	doca_pc_path = '/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig/'
elif host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
	doca_pc_path = '/opt/mellanox/doca/lib/aarch64-linux-gnu/pkgconfig/'
endif
doca_modules = ['doca-grpc-libs', 'doca-libs']
founded_doca_modules = []
message('>>> Detecting DOCA, assume pc file of DOCA is located at ' + doca_pc_path)
env.set('PKG_CONFIG_PATH', doca_pc_path)
foreach doca_module : doca_modules
	doca_module_cflags = run_command(pkgconfig, '--cflags', doca_module, env: env, check: false)
	doca_module_ldflags = run_command(pkgconfig, '--libs', '--static', doca_module, env: env, check: false) # add '--static' option if static link is needed
	doca_module_version = run_command(pkgconfig, '--modversion', doca_module, env: env, check: false)
	if doca_module_cflags.returncode() != 0 or doca_module_ldflags.returncode() != 0 or doca_module_version.returncode() != 0
		message('>>>>>> Failed to find ' + doca_module)
	else
		founded_doca_modules += doca_module
		c_args += doca_module_cflags.stdout().split()
		ld_args += doca_module_ldflags.stdout().split()
		message('>>>>>> Found ' + doca_module + ', version is ' + doca_module_version.stdout().split()[0])
	endif
endforeach
if founded_doca_modules.length() == doca_modules.length()
	has_doca = true
	add_global_arguments('-DSC_HAS_DOCA', language: ['c', 'cpp'])
endif

# >>>>>>>>>>>>>> FlexIO >>>>>>>>>>>>>>
flexio_pc_path = '/opt/mellanox/flexio/lib/pkgconfig/'
flexio_modules = ['libflexio-dev']
founded_flexio_modules = []
message('>>> Detecting FlexIO, assume pc file of FlexIO is located at ' + flexio_pc_path)
env.set('PKG_CONFIG_PATH', doca_pc_path)
foreach flexio_module : flexio_modules
	flexio_module_cflags = run_command(pkgconfig, '--cflags', flexio_module, env: env, check: false)
	flexio_module = run_command(pkgconfig, '--libs', '--static', flexio_module, env: env, check: false) # add '--static' option if static link is needed
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

# >>>>>>>>>>>>>> CUDA >>>>>>>>>>>>>>
cudax_desired_version = '12.1'
cudax_pc_path = '/usr/lib/pkgconfig'
cudax_modules = ['cublas', 'cuda', 'cudart', 'cufft', 'cufftw', 'cufile', 'cuinj64', 'curand', 'cusolver', 'cusparse']
founded_cudax_modules = []
message('>>> Detecting CUDA Toolkit, assume pc file of CUDA Toolkit is located at ' + cudax_pc_path)
env.set('PKG_CONFIG_PATH', cudax_pc_path)
foreach cudax_module : cudax_modules
	cudax_module_cflags = run_command(pkgconfig, '--cflags', cudax_module+'-'+cudax_desired_version, env: env, check: false)
	cudax_module_ldflags = run_command(pkgconfig, '--libs', '--static', cudax_module+'-'+cudax_desired_version, env: env, check: false) # add '--static' option if static link is needed
	cudax_module_version = run_command(pkgconfig, '--modversion', cudax_module+'-'+cudax_desired_version, env: env, check: false)
	if cudax_module_cflags.returncode() != 0 or cudax_module_ldflags.returncode() != 0 or cudax_module_version.returncode() != 0
		message('>>>>>> Failed to find ' + cudax_module+'-'+cudax_desired_version)
	else
		founded_cudax_modules += cudax_module
		c_args += cudax_module_cflags.stdout().split()
		ld_args += cudax_module_ldflags.stdout().split()
		message('>>>>>> Found ' + cudax_module + ', version is ' + cudax_module_version.stdout().split()[0])
	endif
endforeach
if founded_cudax_modules.length() == cudax_modules.length()
	has_cuda = true
endif

# >>>>>>>>>>>>>> Other Dependencies Found through Compiler >>>>>>>>>>>>>>
dep = []
dep += meson.get_compiler('cpp').find_library('pthread')
dep += meson.get_compiler('cpp').find_library('gmp')


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
c_args += ['--std=c++11']

# necessary dependencies check
assert(has_dpdk, 'couldn\'t find libdpdk')
# =======================================================================

subdir('runtime')
subdir('toolchain/compiler')
subdir('toolchain/loader')
