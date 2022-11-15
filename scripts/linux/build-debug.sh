#!/bin/bash
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
./vcpkg/vcpkg install fmt msgpack boost-process boost-container qt5-base qt5-svg catch2
cmake -B build . \
	-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
	-DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
