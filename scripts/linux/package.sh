#!/bin/bash
./scripts/linux/build-release.sh
cd build
mkdir packaged
mkdir packaged/bin
cp nvui packaged/bin
cp -r ../assets packaged
cp -r ../vim packaged
