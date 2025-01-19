#!/bin/bash

# >>>>>>>>>> common variables <<<<<<<<<<
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# target to build
targets=("runtime" "toolchain/compiler" "toolchain/loader" "lib")
targets_string=$(
  IFS='/'
  echo "${targets[*]}"
)
u_targets=()

# clean
do_clean=false

# whether the build process involve third parties
involve_third_party=false

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
  
  # apt-get install build-essential cmake gcc libudev-dev libnl-3-dev libnl-route-3-dev ninja-build pkg-config valgrind python3-dev cython3 python3-docutils pandoc
  check_single_dep ntpdate
}


# func_desp:  generic building procedure
# param_1:    name of the building target
# param_2:    path to the building target
build_nicc() {
  cd $script_dir
  log ">> building $1..."
  mkdir -p ./bin
  # prevent clock skew between host and dpu
  ntp_server="ntp.aliyun.com"
  log ">>>> sync system time with $ntp_server..."
  sudo ntpdate -u $ntp_server

  if [ ! -d "$2/build" ]; then
    log ">>>> generate building processing via meson..."
    export BUILD_TARGET=$2
    meson $script_dir/$2/build
    retval=$?
    if [ $retval -ne 0 ]; then
      error ">>>> meson build failed..."
    fi
  fi
  cd $2/build
  ninja clean
  ninja &>./$1_build.log
  retval=$?
  if [ $retval -ne 0 ]; then
    grep -n -A 5 'error' $1_build.log | awk '{print} NR%2==0 {print ""}'
    # cat $1_build.log
    error ">>>> building $1 failed, error message printed"
  else
    log "successfully built $1"
  fi
  cd $script_dir
  if [ $BUILD_TARGET = "./lib" ]; then
    mv $2/build/lib/libnicc.a ./bin
    mv $2/build/lib/libnicc.a.p ./bin
  fi
}

# func_desp:  generic cleaning procedure
# param_1:    name of the cleaning target
# param_2:    path to the cleaning target
clean_nicc() {
  cd $script_dir
  if [ $2 = "./lib" ]; then
    rm ./bin/libnicc.a
  fi
  cd $2
  log ">> cleaning $1..."
  if [ -d "./build" ]; then
    rm -rf ./build
  else
    log ">>>> no built target was founded, skipped"
  fi
}

build_third_parties() {
  cd $script_dir
  log ">> building rdma_core..."
  cd third_parties/rdma-core
  export EXTRA_CMAKE_FLAGS=-DNO_MAN_PAGES=1
  if [ -d "./build" ]; then
    rm -rf ./build
  fi
  bash build.sh
}

clean_third_parties() {
  cd $script_dir
  log ">> cleaning rdma_core..."
  cd third_parties/rdma-core
  rm -rf build
}

# =========== Rotinue Starts Here ===========

# check dependencies
check_deps

# parse command line options
args=("$@")
while getopts ":hc3t:" opt; do
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
  3)
    involve_third_party=true
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
    if [ $involve_third_party = true ]; then
        clean_third_parties
    fi
    clean_nicc "nicc_$arg" "./$arg"
  else
    if [ $involve_third_party = true ]; then
        build_third_parties
    fi
    build_nicc "nicc_$arg" "./$arg"
  fi
done
