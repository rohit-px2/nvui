#!/bin/bash
./vcpkg/booststrap-vcpkg.sh -disableMetrics
./vcpkg/vcpkg install fmt msgpack boost-process boost-container qt5-base qt5-svg
cmake -B build . \
	-DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
	-DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
