#!/usr/bin/env bash
set -e
_dir=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
cd $_dir

build_dir=build-release
mkdir -p ${build_dir}
cd ${build_dir}

echo "Configuring build..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Check if CUDA target exists, otherwise build core library
if cmake --build . --target help | grep -q "bladebit_cuda"; then
    echo "Building bladebit_cuda (CUDA toolkit found)..."
    cmake --build . --target bladebit_cuda --config Release --clean-first -j24
else
    echo "CUDA toolkit not found. Building core library for testing..."
    cmake --build . --target bladebit_core --config Release --clean-first -j24
    echo ""
    echo "Note: Install CUDA toolkit to build the full bladebit_cuda target."
    echo "This build contains the core library but cannot perform GPU plotting."
fi
