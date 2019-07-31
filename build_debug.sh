#!/usr/bin/env bash

./clean.sh

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=True -DLLVM_DIR=$(readlink -f ../../../llvm/lib/cmake/llvm)
make -j255

cd ..
mkdir -p lib
cp build/region_info/libregion_info.so build/region_info/libregion_info_catch_dlclose.so build/runtime/libregion_info_rt.so lib
cp build/compile_commands.json .
