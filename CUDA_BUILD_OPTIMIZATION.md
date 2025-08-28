# Bladebit CUDA-Only Build Optimization

## Overview
This repository has been optimized to remove all code not relevant to building bladebit-cuda. The build system now focuses exclusively on CUDA GPU plotting functionality, significantly reducing complexity and build dependencies.

## Changes Made

### Removed Components
- **CPU-only bladebit executable**: The original `bladebit` target that supported RAM plotting and disk plotting
- **Harvester functionality**: `bladebit_harvester` target and all harvester-related code
- **Test infrastructure**: All test targets and test-related CMake modules
- **Command implementations**: Removed `src/commands/` directory with CPU-specific command handlers
- **Unused tools**: Removed IOTester, PlotValidator, PlotComparer, and other CPU-focused utilities
- **Build scripts**: Removed `build.sh` and `build-harvester.sh` (only `build-cuda.sh` remains)
- **Sandbox code**: Removed development sandbox and test utilities

### Simplified Components
- **Main executable**: Created `src/main_cuda.cpp` with only CUDA-relevant commands
- **Core library**: `bladebit_core` now contains only essential sources needed by CUDA plotting
- **PlotWriter**: Disabled PlotChecker functionality to reduce dependencies
- **CMake configuration**: Streamlined to focus on CUDA builds only

### Remaining Functionality
The optimized build retains:
- **CUDA plotting**: Full `cudaplot` command with all CUDA plotting modes
- **Memory testing**: `memtest` command for system memory validation
- **Essential utilities**: Core plotting infrastructure, threading, I/O, and cryptographic functions
- **Multi-platform support**: Linux, Windows, and macOS compatibility maintained
- **All compression levels**: Support for compressed plots (C1-C7)
- **Hybrid modes**: 128GB and 64GB disk-hybrid plotting modes

### Build Targets
- `bladebit_cuda`: CUDA plotting executable (when CUDA Toolkit is available)
- `bladebit_core`: Minimal core library with only CUDA-essential sources

### File Structure
```
├── build-cuda.sh              # CUDA-only build script
├── CMakeLists.txt             # Simplified to CUDA-only targets
├── Config.cmake               # Base configuration (CUDA-focused)
├── Bladebit.cmake            # Minimal core library definition
├── BladebitCUDA.cmake        # CUDA executable definition
├── src/main_cuda.cpp         # CUDA-only main with reduced commands
└── src/                      # Source tree with only essential files
    ├── plotting/             # Core plotting infrastructure
    ├── threading/            # Threading utilities
    ├── util/                 # Essential utilities
    ├── b3/, fse/, pos/       # Cryptographic functions
    ├── harvesting/           # Green Reaper (for CUDA thresher)
    └── tools/MemTester.cpp   # Memory testing tool
```

### Dependencies Retained
- **BLS signatures**: For plot key generation
- **NUMA support**: For memory-aware allocation (Linux)
- **Blake3**: Cryptographic hashing
- **FSE compression**: Plot compression functionality
- **Threading libraries**: Multi-threaded processing support

### Build Size Reduction
The optimization significantly reduces:
- Source files: Removed hundreds of unused source files
- Build time: Faster compilation with fewer dependencies
- Binary size: Smaller executable focused on CUDA functionality
- Complexity: Simplified build configuration and maintenance

## Usage
```bash
# Build CUDA version (requires CUDA Toolkit)
./build-cuda.sh

# CUDA plotting
./build-release/bladebit_cuda -f <farmer_key> -c <pool_contract> cudaplot <output_dir>

# Memory testing
./build-release/bladebit_cuda memtest -s 1GB

# Help
./build-release/bladebit_cuda --help
./build-release/bladebit_cuda help cudaplot
```

## Requirements
- CUDA Toolkit 11.0+ for GPU plotting
- 256+ GB system RAM for full CUDA plotting
- 8+ GB VRAM (recommended)
- C++20 compatible compiler
- CMake 3.19+

This optimization makes the repository ideal for CUDA-focused plotting applications while maintaining all essential functionality for GPU-accelerated Chia plot generation.