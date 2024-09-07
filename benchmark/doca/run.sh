#!/bin/bash

# get the benchmark name
if [ $# -lt 2 ]; then
    echo "Usage: $0 <benchmark_name> <mlx5_device>"
    exit 1
fi
BENCHMARK=$1
# check the benchmark name
targets=("l2_reflector" "l2_reflector_optimized")
if [[ ! " ${targets[@]} " =~ " ${BENCHMARK} " ]]; then
    echo "Error: unsupported benchmark name: $BENCHMARK"
    exit 1
fi
# run the benchmark
CUR_DIR=$(cd `dirname $0`; pwd)
cd $CUR_DIR

sudo ./applications/build/$BENCHMARK/src/host/$BENCHMARK -d $2