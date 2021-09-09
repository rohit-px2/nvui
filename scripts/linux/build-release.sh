#!/bin/bash
cmake -B build . \
	-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_TEST=OFF
cmake --build build --config Release
