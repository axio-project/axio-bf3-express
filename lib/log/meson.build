module_name = 'log'

scan_include_path = root_dir + '/scan_include.py' 
scan_src_path = root_dir + '/scan_src.py' 

# ====================== Specifed All Source Files ======================
# scan all needed source files
log_sources = []
glob = run_command('python3', files(scan_src_path), 'lib/' + module_name, check: false)
foreach source : glob.stdout().strip().split('\n')
  if source != ''
    log_sources += module_name + '/' + source
  endif
endforeach
# message(log_sources)
# =======================================================================
lib_sources += log_sources
