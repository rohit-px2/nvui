## BUILDING
<hr>
You Need:
<li> C++ Compiler that supports C++17
<li> CMake </li>

For package management, I use vcpkg @ 807a79876.  
<code>git clone https://github.com/microsoft/vcpkg.git vcpkg</code>  
<code>./vcpkg/bootstrap-vcpkg.(bat|sh) -disableMetrics</code>  
The packages used in this project are:
<li> qt5-base
<li> qt5-svg
<li> fmt
<li> boost-process
<li> msgpack
<li> Catch2 (testing only)

Install these packages. Then create a new subdirectory, say <code>build</code>, and <code>cd</code> into it.

When these packages are installed on vcpkg, set DCMAKE_TOOLCHAIN_FILE to your vcpkg cmake file. If a build type is not specified, the build type defaults to Debug.  
After running CMake, there are two targets to build, <code>nvui</code> and <code>nvui_test</code>.  
To build <code>nvui</code>, run <code>cmake --build . --target nvui</code>. To build <code>nvui_test</code> run   
<code>cmake --build . --target nvui_test</code>.  
<b>After building, you should keep the executable in this subdirectory. Otherwise, the DLLs and assets will not be found, causing the program to throw errors.</b>