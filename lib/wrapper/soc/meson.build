module_name = 'wrapper/soc'

scan_include_path = root_dir + '/scan_include.py' 
scan_src_path = root_dir + '/scan_src.py' 

# ====================== Specifed All Source Files ======================
# scan all needed source files
wrapper_soc_sources = []
glob = run_command('python3', files(scan_src_path), 'lib/' + module_name, check: false)
foreach source : glob.stdout().strip().split('\n')
  if source != ''
    wrapper_soc_sources += module_name + '/' + source
  endif
endforeach
message(wrapper_soc_sources)
# =======================================================================
lib_inc_dirs += [module_name]
lib_sources += wrapper_soc_sources
