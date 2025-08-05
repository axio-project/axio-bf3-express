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
subdir('common')
subdir('wrapper/soc')

# 检查是否有指定的example
example_name = run_command('sh', '-c', 'echo $NICC_EXAMPLE_NAME').stdout().strip()
if example_name != ''
  message('>>> Including example ' + example_name + ' in libnicc')
  
  # 检查example的src目录
  example_src_dir = '../examples/' + example_name + '/src'
  if run_command('sh', '-c', 'test -d ' + example_src_dir + ' && echo true || echo false').stdout().strip() == 'true'
    # 查找src目录下的所有.cc和.cpp文件
    src_files = run_command('sh', '-c', 'find ' + example_src_dir + ' -name "*.cc" -o -name "*.cpp"').stdout().strip().split('\n')
    if src_files[0] != ''
      foreach src_file : src_files
        message('>>> Adding source file: ' + src_file)
        lib_sources += [src_file]
      endforeach
    endif
    lib_inc_dirs += [example_src_dir]
  endif
  
  # 检查example的include目录
  example_inc_dir = '../examples/' + example_name + '/include'
  if run_command('sh', '-c', 'test -d ' + example_inc_dir + ' && echo true || echo false').stdout().strip() == 'true'
    message('>>> Adding include directory: ' + example_inc_dir)
    lib_inc_dirs += [example_inc_dir]
  endif
endif

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
    install: false,
    pic: true
)
