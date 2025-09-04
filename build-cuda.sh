#!/usr/bin/env bash
set -euo pipefail

# Defaults
BUILD_TYPE="Release"
COMPILER="gcc"   # but we won't pass gcc explicitly

usage() {
  cat <<EOF
Usage: ${0##*/} [-t Release|Debug|Coverage] [-c gcc|clang]

Options:
  -t    Build type (Release|Debug|Coverage). Default: Release
  -c    Host compiler (gcc|clang). Default: gcc (omit -c to use system default)
  -h    Show this help

Examples:
  ${0##*/}                     # Release with system default compiler (gcc)
  ${0##*/} -t Debug            # Debug with system default compiler
  ${0##*/} -c clang            # Release with clang
  ${0##*/} -t Coverage         # Coverage with system default compiler
  ${0##*/} -t Coverage -c clang
EOF
}

while getopts ":t:c:h" opt; do
  case "$opt" in
    t)
      case "$OPTARG" in
        Release|Debug|Coverage) BUILD_TYPE="$OPTARG" ;;
        *) echo "Invalid build type: '$OPTARG' (use Release, Debug, or Coverage)"; usage; exit 2 ;;
      esac
      ;;
    c)
      case "$OPTARG" in
        gcc|clang) COMPILER="$OPTARG" ;;
        *) echo "Invalid compiler: '$OPTARG' (use gcc or clang)"; usage; exit 2 ;;
      esac
      ;;
    h) usage; exit 0 ;;
    \?) echo "Unknown option: -$OPTARG"; usage; exit 2 ;;
    :)  echo "Missing value for -$OPTARG"; usage; exit 2 ;;
  esac
done

# Common flags
COMMON_WARN_FLAGS="-w -fdiagnostics-color=always"

# CUDA: keep warnings quiet and print useful PTX/NVLINK verbosity
CUDA_SILENCE="--disable-warnings -Wno-deprecated-gpu-targets --compiler-options -w -Xptxas -v -Xnvlink --verbose"

# Per-config build dir (lowercased)
BUILD_DIR="build-${BUILD_TYPE,,}"  # build-release / build-debug / build-coverage

echo ">>> Config:"
echo "    Build type       : $BUILD_TYPE"
echo "    Compiler setting : $COMPILER"
echo "    Build directory  : $BUILD_DIR"
echo

# Base cmake args
CMAKE_ARGS=(
  -S . -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DCMAKE_C_FLAGS="$COMMON_WARN_FLAGS"
  -DCMAKE_CXX_FLAGS="$COMMON_WARN_FLAGS"
  -DCMAKE_CUDA_FLAGS="$CUDA_SILENCE"
)

# Add compiler options *only if clang is requested*
if [[ "$COMPILER" == "clang" ]]; then
  CMAKE_ARGS+=(
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    -DCMAKE_CUDA_HOST_COMPILER="$(command -v clang++)"
  )
fi

# Architecture selection: default to multi-arch on CI, single-arch locally
if [[ "${CI:-}" == "true" ]]; then
  echo ">>> CI detected: building with default multi-architecture targets"
  CMAKE_ARGS+=( -DBB_CUDA_USE_NATIVE=OFF )
else
  echo ">>> Local build: selecting native CUDA architecture (single SM)"
  CMAKE_ARGS+=( -DBB_CUDA_USE_NATIVE=ON )

  # Best-effort: probe and display the detected GPU SM for visibility
  if command -v nvcc >/dev/null 2>&1; then
    tmpdir=$(mktemp -d -t cudaComputeVersion-XXXXXX)
    trap 'rm -rf "$tmpdir"' EXIT
    cat > "$tmpdir/cudaComputeVersion.cu" <<'EOF'
#include <stdio.h>
#include <cuda_runtime.h>
int main(){
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop,0);
    int v = prop.major * 10 + prop.minor;
    printf("Device Name:\n%s\n\n", prop.name);
    printf("Your device architecture:\n-gencode arch=compute_%d,code=sm_%d\n", v, v);
    return 0;
}
EOF
    if nvcc "$tmpdir/cudaComputeVersion.cu" -o "$tmpdir/cudaComputeVersion" >/dev/null 2>&1; then
      "$tmpdir/cudaComputeVersion" || true
    fi
    rm -rf "$tmpdir"
    trap - EXIT
  fi
fi

# Configure fresh
rm -rf "$BUILD_DIR"
cmake "${CMAKE_ARGS[@]}"

# Build only the CUDA plotter target (fast iteration)
cmake --build "$BUILD_DIR" --target bladebit_cuda -j"$(nproc)"

# Helpful tip for Coverage builds
if [[ "$BUILD_TYPE" == "Coverage" ]]; then
  cat <<'TIP'
>>> Coverage build detected.
    After running your plotting workload, generate HTML with lcov/genhtml, e.g.:

      lcov --capture --directory build-coverage --output-file coverage.info
      lcov --remove coverage.info '/usr/*' '*/external/*' --output-file coverage.cleaned.info
      genhtml coverage.cleaned.info --output-directory coverage-html

    Or, if you added a CMake 'coverage-report' target:
      cmake --build build-coverage --target coverage-report
TIP
fi
