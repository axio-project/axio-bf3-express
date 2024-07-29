module_name = 'toolchain/compiler'

scan_include_path = root_dir + '/scan_include.py' 
scan_src_path = root_dir + '/scan_src.py' 

# ==================== Specified All Dependent Headers ==================
# scan all needed header files
glob = run_command('python3', files(scan_include_path), module_name, check: false)
toolchain_compiler_inc_dirs = include_directories(glob.stdout().strip().split('\n'))
# message(glob.stdout().strip().split('\n'))
# =======================================================================


# ====================== Specifed All Source Files ======================
# scan all needed source files
glob = run_command('python3', files(scan_src_path), module_name, check: false)
toolchain_compiler_sources = glob.stdout().strip().split('\n')
# message(sources)
# =======================================================================

binary_name = project_name + '/' + module_name
toolchain_compiler_c_args = c_args
toolchain_compiler_ld_args = ld_args

# build executable
message('cflags: ' + ' '.join(c_args))
message('ldflags: ' + ' '.join(ld_args))
executable(
    binary_name,
    toolchain_compiler_sources,
	cpp_args: toolchain_compiler_c_args,
	link_args: toolchain_compiler_ld_args,
	include_directories: toolchain_compiler_inc_dirs,
	install: false
)
