param(
	[string]$cc = "clang",
	[string]$cxx = "clang++",
	[string]$gen = "Ninja"
)
$generator = '"{0}"' -f $gen
$cmd = Write-Output "cmake . -B build -DQt5_DIR=$env:Qt5_DIR -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -G $generator -DBUILD_TESTS=ON"
cmd /C $cmd
cmake --build build --config Debug
