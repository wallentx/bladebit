# Bladebit Chia Plotter

Bladebit is a high-performance k32-only Chia (XCH) plotter written in C++20 supporting multiple plotting modes: In-RAM (CPU), GPU (CUDA), and disk-based plotting. This project uses CMake build system and supports Linux, Windows, and macOS on both x86_64 and ARM64 architectures.

Always reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.

## Working Effectively

### Prerequisites Installation
Install the required packages for your platform:
```bash
# Ubuntu or Debian-based systems
sudo apt install -y build-essential cmake libgmp-dev libnuma-dev

# CentOS or RHEL-based systems 
sudo yum group install -y "Development Tools"
sudo yum install -y cmake gmp-devel numactl-devel

# macOS (requires Xcode or Xcode build tools)
brew install cmake
brew install gmp  # optional
```

### Building and Testing
**NEVER CANCEL builds or long-running commands. Builds may take 3+ minutes.**

Build the main bladebit executable:
```bash
# Using build script (recommended):
./build.sh  # Takes ~2m30s. Set timeout to 5+ minutes. NEVER CANCEL.

# Or manually:
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target bladebit --config Release -j32  # Takes ~2m30s. Set timeout to 5+ minutes. NEVER CANCEL.
```

Build the harvester component:
```bash
./build-harvester.sh  # Takes ~15s. Set timeout to 1+ minutes. NEVER CANCEL.
```

Build CUDA version (requires CUDA toolkit):
```bash
./build-cuda.sh  # Takes ~2m30s. Set timeout to 5+ minutes. NEVER CANCEL.
```

### Test Basic Functionality
```bash
# Test binary works
./build-release/bladebit --version  # Should output version like "3.1.1" or "0.0.0-dev"
./build-release/bladebit --help     # Verify help system works
./build-release/bladebit --memory   # Display memory requirements
./build-release/bladebit --memory-json  # JSON format memory info

# Test utilities
./build-release/bladebit memtest -s 100MB  # Quick memory test
./build-release/bladebit help diskplot     # Command-specific help
./build-release/bladebit help validate     # Validation command help
```

### Manual Validation Scenarios

**CRITICAL:** After making changes, always run these validation scenarios:

1. **Build Validation**: 
   - Clean build from scratch: `rm -rf build-release && ./build.sh`
   - Verify binary works: `./build-release/bladebit --help`

2. **Memory and System Tests**:
   - Memory test: `./build-release/bladebit memtest -s 100MB`
   - System info: `./build-release/bladebit --memory`

3. **Command Structure Tests**:
   - Test help system: `./build-release/bladebit help diskplot`
   - Test global options: `./build-release/bladebit --version`

**Note about tests:** The repository contains sandbox test files in `tests/` directory, but they are not proper unit tests and have compilation errors. The CMake option `BB_ENABLE_TESTS=ON` will enable them, but they should not be relied upon for validation.

## Key Information

### Build Timing Expectations
- **Standard build**: ~2m30s (NEVER CANCEL - Set timeout to 5+ minutes)
- **Harvester build**: ~15s (Set timeout to 1+ minutes)
- **CUDA build**: ~2m30s (requires CUDA toolkit, Set timeout to 5+ minutes)
- **Configuration step**: ~3-6s

### Memory Requirements
- **RAM plotting**: 416+ GiB of system RAM
- **CUDA plotting**: 256+ GiB system RAM + 8+ GiB VRAM
- **Disk plotting**: 4+ GiB RAM (up to 12 GiB for low bucket counts)

### Build Output Locations
```
build-release/bladebit           # Main CPU plotter
build-release/bladebit_cuda      # CUDA plotter (if built)
build-harvester/libbladebit_harvester.so  # Harvester library
```

### Command Structure
All bladebit commands follow the pattern:
```bash
bladebit <GLOBAL_OPTIONS> <command> <COMMAND_OPTIONS>
```

**Main commands:**
- `ramplot` - In-memory plotting (requires 416+ GiB RAM)
- `diskplot` - Disk-based plotting (requires temp directories)
- `cudaplot` - CUDA GPU plotting (requires CUDA toolkit)
- `validate` - Validate plot files
- `simulate` - Simulation tool for compressed plots
- `iotest` - Disk I/O testing
- `memtest` - Memory testing
- `check` - Random proof validation

### Frequently Referenced Directories

**Source code structure:**
```
src/
├── main.cpp                     # Main entry point
├── commands/                    # Command implementations
│   ├── CmdSimulator.cpp        # Simulation command
│   └── CmdPlotCheck.cpp        # Plot checking
├── plotting/                   # Core plotting algorithms
│   ├── PlotWriter.cpp/.h       # Plot file writing
│   ├── GlobalPlotConfig.h      # Configuration structures
│   └── PlotTools.cpp/.h        # Plot utilities
├── tools/                      # Utility tools
│   ├── PlotValidator.cpp       # Plot validation
│   ├── IOTester.cpp           # I/O testing
│   └── MemTester.cpp          # Memory testing
├── plotdisk/                   # Disk plotting implementation
├── plotmem/                    # Memory plotting implementation
└── cuda/                       # CUDA implementations
```

**Build scripts:**
```
build.sh                        # Standard build script
build-cuda.sh                   # CUDA build script  
build-harvester.sh              # Harvester build script
embed-version.sh                # Version embedding
```

### CI/CD Information
- GitHub Actions workflows in `.github/workflows/`
- Build configurations support multiple platforms: Linux (x86_64, ARM64), Windows, macOS
- CUDA builds require special setup with CUDA toolkit
- Build artifacts are created as compressed archives

### Development Notes
- Written in C++20 with CMake build system
- Uses FetchContent for dependency management (BLS signatures, libsodium, etc.)
- Supports both Release and Debug configurations
- Direct I/O support for performance on supported filesystems
- NUMA-aware memory allocation on supported systems

### Validation Requirements
When making changes:
1. **Always build from scratch** to ensure clean compilation
2. **Test basic functionality** with `--help`, `--version`, `--memory`
3. **Run memory tests** to verify system integration
4. **Test relevant commands** for the area you changed
5. **Never skip validation** due to time constraints - builds are relatively fast at ~2m30s

### Common Issues
- **Buffer overflow in iotest**: Known issue with iotest utility - use memtest instead for memory validation
- **CUDA not found**: Expected if CUDA toolkit not installed, will build CPU-only version
- **Tests compilation errors**: Sandbox tests are not maintained, compilation errors are expected
- **Memory requirements**: RAM plotting requires 416+ GiB - check with `--memory` command
- **Direct I/O**: May need `--no-direct-io` flag on some filesystems

Remember: Always validate your changes work correctly before considering them complete. The build times are reasonable (~2m30s) so there's no excuse for skipping proper validation.