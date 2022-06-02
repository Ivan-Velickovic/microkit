#!/usr/bin/env bash
set -euxo pipefail

export PROJ_ROOT=../../..
export BUILD_DIR=$PROJ_ROOT/tmp_build/timer
mkdir -p $BUILD_DIR
export SEL4CP_SDK=$PROJ_ROOT/release/sel4cp-sdk-1.2.6
export SEL4CP_BOARD=zcu102
export SEL4CP_CONFIG=debug
export PYTHONPATH=$PROJ_ROOT/tool
export SEL4CP_TOOL="python -m sel4coreplat"

make
mkdir -p $BUILD_DIR/build
cd $BUILD_DIR/build
cmake -G Ninja $PROJ_ROOT -DSEL4CP_BUILD_DIR="$BUILD_DIR" -DSEL4CP_CDL_FILENAME=timer.cdl -DSEL4CP_ELF_FILENAMES=timer.elf -DPLATFORM=zynqmp
ninja
