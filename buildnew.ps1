rm build -Recurse -Force
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG" -GNinja
ninja