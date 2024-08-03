module_name = 'runtime'

scan_include_path = root_dir + '/scan_include.py' 
scan_src_path = root_dir + '/scan_src.py' 

# ==================== Specified All Dependent Headers ==================
# scan all needed header files
glob = run_command('python3', files(scan_include_path), module_name, check: false)
runtime_inc_dirs = include_directories(glob.stdout().strip().split('\n'))
# message(glob.stdout().strip().split('\n'))
# =======================================================================


# ====================== Specifed All Source Files ======================
# scan all needed source files
glob = run_command('python3', files(scan_src_path), module_name, check: false)
runtime_sources = glob.stdout().strip().split('\n')
# message(sources)
# =======================================================================

binary_name = project_name + '/' + module_name
runtime_c_args = c_args
runtime_ld_args = ld_args

log_conf = configuration_data()
log_conf.set('nicc_print_error', enable_print_error)
log_conf.set('nicc_print_warn', enable_print_warn)
log_conf.set('nicc_print_log', enable_print_log)
log_conf.set('nicc_print_debug', enable_print_debug)
log_conf.set('nicc_print_with_color', enable_print_with_color)
configure_file(input : 'log.h.in', output : 'log.h', configuration : log_conf)

# build executable
message('cflags: ' + ' '.join(c_args))
message('ldflags: ' + ' '.join(ld_args))
executable(
    binary_name,
    runtime_sources,
	dependencies : dep,
	cpp_args: c_args,
	link_args: ld_args,
	include_directories: runtime_inc_dirs,
	install: false
)
