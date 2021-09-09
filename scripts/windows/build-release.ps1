param(
	[string]$cc = "clang",
	[string]$cxx = "clang++",
	[string]$gen = "Ninja",
	[string]$cmake_args = ""
)
$generator = '"{0}"' -f $gen
$cmd = Write-Output "cmake . -B build -DBUILD_TEST=OFF -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -G $generator $cmake_args"
cmd /C $cmd
cmake --build build --config Release
