module_name = 'runtime'

scan_include_path = root_dir + '/scan_include.py' 
scan_src_path = root_dir + '/scan_src.py' 

# ==================== Specified All Dependent Headers ==================
runtime_inc_dirs = include_directories(['include', '../lib'])
# =======================================================================


# ====================== Specifed All Source Files ======================
# scan all needed source files
glob = run_command('python3', files(scan_src_path), module_name, check: false)
runtime_sources = glob.stdout().strip().split('\n')
# message(sources)
# =======================================================================

binary_name = project_name_abbreviation + '_' + module_name
runtime_c_args = c_args
runtime_ld_args = ld_args + ['-L'+root_dir+'/bin', '-ldpa_wrapper', '-lnicc']

log_conf = configuration_data()
log_conf.set('nicc_print_error', enable_print_error)
log_conf.set('nicc_print_warn', enable_print_warn)
log_conf.set('nicc_print_log', enable_print_log)
log_conf.set('nicc_print_debug', enable_print_debug)
log_conf.set('nicc_print_with_color', enable_print_with_color)
configure_file(input : root_dir + '/lib/log/log.h.in', output : 'log.h', configuration : log_conf)

debug_conf = configuration_data()
debug_conf.set('nicc_runtime_debug_check', enable_runtime_debug_check)
configure_file(input : root_dir + '/lib/log/debug.h.in', output : 'debug.h', configuration : debug_conf)

# build executable
message('cflags: ' + ' '.join(c_args))
message('ldflags: ' + ' '.join(ld_args))
executable(
  binary_name,
  runtime_sources,
	dependencies : dep,
	cpp_args: runtime_c_args,
	link_args: runtime_ld_args,
	include_directories: runtime_inc_dirs,
	install: false
)
