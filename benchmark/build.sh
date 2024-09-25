#!/bin/bash

# >>>>>>>>>> common variables <<<<<<<<<<
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# target to build
targets=("doca/applications")
targets_string=$(
  IFS='/'
  echo "${targets[*]}"
)
u_targets=()

# clean
do_clean=false

log() {
  echo -e "\033[37m [NICC Build Log] $1 \033[0m"
}

warn() {
  echo -e "\033[33m [NICC Build Wrn] $1 \033[0m"
}

error() {
  echo -e "\033[31m [NICC Build Err] $1 \033[0m"
  exit 1
}

# print helping
print_usage() {
  echo ">>>>>>>>>> NICC build routine <<<<<<<<<<"
  echo "usage: $0 [-c] [-t <target_1>,<target_2>] [-h]"
  echo "  -t  target to be built or cleaned, supported targets: $targets_string"
  echo "  -c  clean previously built assets"
  echo "  -h  help message"
}


# func_desp: check dependencies
check_deps() {
  check_single_dep() {
    if ! command -v $1 &> /dev/null; then
      error "$1 required, please \"sudo apt-get install $1\""
    fi
  }

  check_single_dep ntpdate
}


# func_desp:  generic building procedure
# param_1:    name of the building target
# param_2:    path to the building target
build_nicc() {
  cd $script_dir
  log ">> building $1..."

  # prevent clock skew between host and dpu
  ntp_server="ntp.aliyun.com"
  log ">>>> sync system time with $ntp_server..."
  sudo ntpdate -u $ntp_server
  cd $2
  if [ ! -d "./build" ]; then
    log ">>>> generate building processing via meson..."
    meson setup build
    retval=$?
    if [ $retval -ne 0 ]; then
      error ">>>> meson build failed..."
    fi
  fi
  cd ./build
  ninja clean
  ninja &>./build.log
  retval=$?
  if [ $retval -ne 0 ]; then
    grep -n -A 5 'error' build.log | awk '{print} NR%2==0 {print ""}'
    # cat $1_build.log
    error ">>>> building $1 failed, error message printed"
  else
    log "successfully built $1"
  fi
}

# func_desp:  generic cleaning procedure
# param_1:    name of the cleaning target
# param_2:    path to the cleaning target
clean_nicc() {
  cd $script_dir
  cd $2
  log ">> cleaning $1..."
  if [ -d "./build" ]; then
    rm -rf ./build
  else
    log ">>>> no built target was founded, skipped"
  fi
}

# =========== Rotinue Starts Here ===========

# check dependencies
check_deps

# parse command line options
args=("$@")
while getopts ":hct:" opt; do
  case $opt in
  h)
    print_usage
    exit 0
    ;;
  c)
    do_clean=true
    ;;
  t)
    u_targets=(${OPTARG//,/ })
    ;;
  \?)
    echo "invalid target: -$OPTARG" >&2
    exit 1
    ;;
  :)
    echo "option -$OPTARG require extra parameter" >&2
    exit 1
    ;;
  esac
done

# check target list
if [ ${#u_targets[@]} -eq 0 ]; then
  warn "no target specified, aims at all target: $targets_string"
  u_targets=("${targets[@]}")
else
  for arg in "${u_targets[@]}"; do
    if ! printf '%s\n' "${targets[@]}" | grep -q -P "^$arg$"; then
      error "unknown target $arg, supported target are: $targets_string"
    fi
  done
  u_targets_string=$(
    IFS=', '
    echo "${u_targets[*]}"
  )
  log "aims at target: $u_targets_string"
fi

# building procedure
for arg in "${u_targets[@]}"; do
  if [ $do_clean = true ]; then
    clean_nicc "benchmark/$arg" "./$arg"
  else
    build_nicc "benchmark/$arg" "./$arg"
  fi
done
