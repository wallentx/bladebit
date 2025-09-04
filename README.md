# Bladebit CUDA Plotter

A high-performance CUDA-only Chia (XCH) plotter optimized for GPU plotting.

## Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt install -y build-essential cmake libgmp-dev libnuma-dev
```

**CentOS/RHEL:**
```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y cmake gmp-devel numactl-devel
```

**macOS:**
```bash
brew install cmake gmp
```

**For CUDA support:** Install [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) (11.2 or later)

## Building

```bash
# Build (automatically detects CUDA)
./build-cuda.sh

# The executable will be in build-release/bladebit_cuda (or just core library if no CUDA)
```

## Usage

**CUDA Plotting (requires CUDA toolkit):**
```bash
./build-release/bladebit_cuda -f <farmer_key> -c <contract> cudaplot <output_dir>
```

**Memory Test:**
```bash
./build-release/bladebit_cuda memtest -s 1GB
```

## System Requirements

- **CUDA plotting**: 256+ GiB system RAM + 8+ GiB VRAM
- **Memory test**: 4+ GiB RAM

## Supported Platforms

- Linux (x86_64, ARM64) 
- macOS (x86_64, ARM64)

This repository is optimized specifically for CUDA plotting and contains only the minimal code necessary for GPU-based plot generation.