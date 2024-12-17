message('============Building nicc library============')
lib_inc_dirs = ['./']
lib_sources = []

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

# compile targets
subdir('log')
subdir('wrapper/soc')
subdir('channel')

# build nicc library
message('source files: ' + ' '.join(lib_sources))
message('include directories: ' + ' '.join(lib_inc_dirs))

lib_c_args = c_args
lib_ld_args = ld_args

message('cflags: ' + ' '.join(c_args))
message('ldflags: ' + ' '.join(ld_args))
nicc_lib = static_library(
    'nicc',
    lib_sources,
    cpp_args: lib_c_args,
    link_args: lib_ld_args,
    include_directories: lib_inc_dirs,
    install: false
)
