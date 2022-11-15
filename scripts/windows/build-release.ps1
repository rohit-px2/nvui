param(
	[string]$cc = "clang",
	[string]$cxx = "clang++",
	[string]$gen = "Ninja"
)
$generator = '"{0}"' -f $gen
$cmd = Write-Output "cmake . -B build -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -G $generator"
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\vcpkg\vcpkg install fmt msgpack qt5-base qt5-svg boost-process boost-container
cmd /C $cmd
cmake --build build --config Release
