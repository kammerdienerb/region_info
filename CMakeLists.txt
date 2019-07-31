cmake_minimum_required(VERSION 3.6)

message(STATUS "LLVM_DIR=${LLVM_DIR}")

find_package(LLVM REQUIRED CONFIG)

message(STATUS "LLVM Version: ${LLVM_VERSION}")
message(STATUS "LLVM Prefix: ${LLVM_INSTALL_PREFIX}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_subdirectory(region_info)
add_subdirectory(runtime)
