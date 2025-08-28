# Base interface configuration project
add_library(bladebit_config INTERFACE)

target_include_directories(bladebit_config INTERFACE
    ${INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(bladebit_config INTERFACE
    $<${is_release}:
        _NDEBUG=1
        NDEBUG=1
    >
    $<${is_debug}:
        _DEBUG=1
        DEBUG=1
    >
)

target_compile_options(bladebit_config INTERFACE

    $<${is_c_cpp}:
        # GCC or Clang
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall
            -Wno-comment
            -Wno-unknown-pragmas
            -g

            $<${is_release}:
                -O3
            >

            $<${is_debug}:
                -O0
            >
        >

        # GCC
        $<$<CXX_COMPILER_ID:GNU>:
            -fmax-errors=5
        >

        # Clang
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:
            -ferror-limit=5
            -fdeclspec
            -Wno-empty-body
        >
    >

    $<${is_x86}:
    >

    $<${is_arm}:
    >
)

target_link_options(bladebit_config INTERFACE

    # GCC or Clang
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -g
        -rdynamic
    >
)

set_property(DIRECTORY . PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(DIRECTORY . PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

set(preinclude_pch
    $<${is_cuda}:--pre-include pch.h>
    $<${is_c_cpp}:
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:--include=pch.h>
    >
)

# See: https://gitlab.kitware.com/cmake/cmake/-/issues/18265
cmake_policy(SET CMP0105 NEW)

set(cuda_archs

    $<${is_cuda_release}:
        $<$<BOOL:${BB_CUDA_USE_NATIVE}>:
            -arch=native
        >

        $<$<NOT:$<BOOL:${BB_CUDA_USE_NATIVE}>>:

            # Maxwell
            -gencode=arch=compute_50,code=sm_50 # Tesla/Quadro M series
            -gencode=arch=compute_52,code=sm_52 # Quadro M6000 , GeForce 900, GTX-970, GTX-980, GTX Titan X
            -gencode=arch=compute_53,code=sm_53 # Tegra (Jetson) TX1 / Tegra X1, Drive CX, Drive PX, Jetson Nano

            # Pascal
            -gencode=arch=compute_60,code=sm_60 # GeForce 1000 series
            -gencode=arch=compute_61,code=sm_61 # GeForce GTX 1050Ti, GTX 1060, GTX 1070, GTX 1080
            -gencode=arch=compute_62,code=sm_62 # Drive Xavier, Jetson AGX Xavier, Jetson Xavier NX
            
            # Volta
            -gencode=arch=compute_70,code=sm_70 # GV100, Tesla V100, Titan V
            -gencode=arch=compute_72,code=sm_72 # Tesla V100
            -gencode=arch=compute_75,code=sm_75 # Turing

            # Ampere
            -gencode=arch=compute_80,code=sm_80 # NVIDIA A100, DGX-A100
            -gencode=arch=compute_86,code=sm_86 # GeForce RTX 3000 series, NVIDIA A100
            -gencode=arch=compute_87,code=sm_87 # Jetson Orin

            # Lovelace
            -gencode=arch=compute_89,code=sm_89         # NVIDIA GeForce RTX 4090, RTX 4080, RTX 6000, Tesla L40
            -gencode=arch=compute_89,code=compute_89    # Future proofing
        >
    >

    $<${is_cuda_debug}:
        -arch=native
        # -gencode=arch=compute_52,code=sm_52 # Maxwell
    >
)
