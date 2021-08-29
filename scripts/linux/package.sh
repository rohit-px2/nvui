#!/bin/bash
./scripts/linux/build-release.sh
cd build
mkdir packaged
mkdir packaged/bin
cp nvui packaged/bin
cp -r ../assets packaged
rm -rf packaged/assets/display
cp -r ../vim packaged
