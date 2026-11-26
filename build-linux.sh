#!/usr/bin/env bash
# The native Linux build of ShaderCompile, see ShaderCompile/linux/peloader.h for how it runs the Windows DLL.
# Needs cmake, ninja, gcc 15 or newer for #embed, and g++.
# The committed DLL in d3dcompiler/ is compiled into the binary, see CMakeLists.txt

set -euo pipefail
cd "$(dirname "$0")"
rm -rf build-linux
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
