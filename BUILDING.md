## BUILDING
<hr>
You Need:
<ul>
  <li> C++ Compiler that supports C++17</li>
  <li> CMake </li>
  <li> On Linux, see below for the dependencies needed</li>
</ul>

For package management, I use vcpkg @ 807a79876 as a submodule.
See the vcpkg.json for the packages used in this project.

## Installling Dependencies
<hr>
### Linux
On Linux, dependencies installed using vcpkg require some packages to be
installed by the system package manger. In order for the dependencies
to build properly you must install the follwing dependencies

(apt)  
<code>sudo apt-get install -y gperf autoconf build-essential libtool
libgl1-mesa-dev libxi-dev libx11-dev libxext-dev
libxkbcommon-x11-dev libglu1-mesa-dev libx11-xcb-dev
'^libxcb.*-dev' libxrender-dev ninja-build curl
zip unzip tar autopoint python
</code>

I don't have experience with any other package manger, but these are all the required packages on Ubuntu. This is also what the Github Actions Ubuntu image does.

### Windows
On Windows you don't need to install any dependencies, vcpkg takes care of it.
<hr>

### Building

<code> cmake -B <build_dir> . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release</code>  
<code> cmake --build <build_dir> --target nvui --config Release</code>

### NOTE (Windows, MSVC):
When you use MSVC / Visual Studio the project is not built into the "build" folder,
instead it is built into "build/Release".
If you run the executable from this directory, nvui will fail to find the assets and vim files, and will close with a "file not found" error.
You should move the executable and DLLs from the "build/Release" folder into the "build" folder.
This issue does not occur with Clang.
<hr>

## Packaging Executable
nvui must find the assets folder in "../assets" and the folder for vim files at "../vim". Thus, if you are trying to package the executable the folder everything is packaged in should look like this:

- nvui
  - bin (can be whatever name, this is where the executable is stored)
  - assets
  - vim

This is what the "package" scripts do (see the "scripts" folder).
<hr>
