# Linux build and packaging

The initial Linux target is x86-64 with an NVIDIA GPU of compute capability 7.5
or newer. It uses the same CUDA solver and OpenGL/GLFW interface as Windows.

## Prerequisites

- A Linux distribution supported by CUDA Toolkit 13
- NVIDIA driver 580 or newer for CUDA 13 binaries
- CUDA Toolkit 13, including `nvcc`
- GCC with C++20 support
- CMake 3.24 or newer and Ninja
- vcpkg, with `VCPKG_ROOT` pointing at its checkout
- OpenGL and desktop development packages required by GLFW
- The official Gmsh Linux SDK

On Ubuntu, the non-CUDA build prerequisites typically start with:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libgl1-mesa-dev libglu1-mesa-dev xorg-dev libwayland-dev \
    wayland-protocols libdbus-1-dev
```

Install CUDA using NVIDIA's instructions for the exact Ubuntu release rather
than relying on Ubuntu's older `nvidia-cuda-toolkit` package.

## Gmsh SDK layout

Download the Linux SDK matching the architecture from <https://gmsh.info/> and
keep the compressed archive in the platform folder. Exactly one matching
archive should be present:

```text
externals/gmsh/
├── linux/gmsh-4.15.2-Linux64-sdk.tgz
└── windows/...
```

During configuration, CMake extracts the Linux SDK into
`out/build/linux-release/_deps/gmsh/`. It reuses that copy until the archive's
timestamp or size changes, so the extracted SDK does not need to be committed.

Alternatively, point CMake at an already-extracted SDK whose root contains
`include/gmsh.h_cwrap` and `lib/libgmsh.so`:

```sh
cmake --preset linux-release -DAXISIM_GMSH_SDK_ROOT=/opt/gmsh-sdk
```

## Configure and build

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-release
cmake --build --preset linux-release
```

The Linux release preset produces native code for Turing, Ampere, Ada, and
Blackwell GPUs and embeds compute 7.5 PTX as a fallback. This takes longer than
the local Windows preset, which remains limited to `sm_86` for fast iteration.

## Package

```sh
bash package-linux.sh v1.04-alpha
```

This runs `cmake --install`, verifies the important runtime files, and creates
`dist/AxiSim-v1.04-alpha-linux-x86_64.tar.gz`. Users run the `axisim` launcher
inside the extracted directory. The launcher changes to the application
directory before starting AxiSim because fonts, icons, and GLSL shaders are
currently resolved relative to the working directory.

The archive bundles the CUDA runtime and Gmsh library but cannot bundle the
NVIDIA kernel/display driver. An AppImage can be produced after this tarball has
been tested across the oldest and newest supported Linux distributions.

## Current Linux limitations

- MP4 animation export is Windows-only. Export a PNG sequence on Linux.
- Copying rendered images to the desktop clipboard is Windows-only. Text
  clipboard operations work through GLFW on Linux.
- The Linux Gmsh SDK is extracted during CMake configuration, so a first
  configure needs roughly 100 MB of additional build-directory space.
- Release testing requires a physical or self-hosted Linux machine with an
  NVIDIA GPU. A compile-only CI job does not validate CUDA kernels or graphics.
