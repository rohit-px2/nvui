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

#### Arch Linux

- Build without vcpkg

1. Dependencies

```bash
sudo pacman -Sy
sudo pacman -S clang make boost fmt hicolor-icon-theme msgpack-cxx qt5-base qt5-svg catch2 cmake ninja git curl wget --needed
```

2. Clone

```bash
git clone https://github.com/rohit-px2/nvui.git --recurse-submodules
cd nvui
```

3. Build

```bash
cmake -B build . -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nvui --config Release
```

4. Install

Nvui need to read the asserts and vim directories at parent directory.
So I make a script to call it, not put it directly in the /bin.

Also the path `~/.local` can be replace with `/usr/local` to use nvui
system wide.

```bash
mkdir -p ~/.local/share/nvui/bin
mkdir -p ~/.local/bin
cp ./build/nvui ~/.local/share/nvui/bin
cp -r ./vim ~/.local/share/nvui/vim
cp -r ./assets ~/.local/share/nvui/assets
echo -e '#!/bin/bash\n\n$HOME/.local/share/nvui/bin/nvui --detached -- "$@"' > ~/.local/bin/nvui
chmod +x ~/.local/bin/nvui
```

### Windows
On Windows you don't need to install any dependencies, vcpkg takes care of it.
<hr>

### Building

<code> cmake -B <build_dir> . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release</code>  
<code> cmake --build <build_dir> --target nvui --config Release</code>

### NOTE (Windows, MSVC):
When you use MSVC / Visual Studio the project is not built into the "build" folder,
instead it is built into "build/Release".
If you run the executable from this directory, nvui will fail to find the assets and vim files, and, if you have commands in your vimrc, they won't be recognized (since nvui.vim is not loaded). Also, you won't see any popup menu icons (if you use the external popup menu).
You should move the executable and DLLs from the "build/Release" folder into the "build" folder.
This issue does not occur with Clang, since the executable is placed right into
the "build" directory.
<hr>

## Packaging Executable
nvui must find the assets folder in "../assets" and the folder for vim files at "../vim". Thus, if you are trying to package the executable the folder everything is packaged in should look like this:

- nvui (the main folder)
  - bin (can be whatever name, this is where the executable is stored)
    - nvui executable
    - (On Windows) the "plugins" directory, DLLs
  - assets
  - vim

This is what the "package" scripts do (see the "scripts" folder).
<hr>
