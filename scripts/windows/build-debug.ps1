param(
	[string]$cc = "clang",
	[string]$cxx = "clang++",
	[string]$gen = "Ninja"
)
$generator = '"{0}"' -f $gen
$cmd = Write-Output "cmake . -B build -DCMAKE_PREFIX_PATH=""$env:CMAKE_PREFIX_PATH"" -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -G $generator -DBUILD_TESTS=ON"
cmd /C $cmd
cmake --build build --config Debug
