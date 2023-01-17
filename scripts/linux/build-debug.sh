#!/bin/bash
cmake -B build . \
	-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
	-DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
