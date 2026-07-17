<div align="center">

# AxiSim

### GPU-accelerated axisymmetric CFD, from sketch to solution.

AxiSim is an interactive finite-volume solver for **axisymmetric incompressible flow**.
Sketch a geometry, mesh it, run the solver on your GPU, and explore the results — all in one app.

![version](https://img.shields.io/badge/version-v1.04--alpha-blue)
![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)
![GPU](https://img.shields.io/badge/GPU-NVIDIA%20CUDA-76B900)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![license](https://img.shields.io/badge/license-MIT-green)

<a href="https://github.com/Luippe/AxiSim-Software/releases/latest/download/AxiSim-win64.zip">
  <img src="https://img.shields.io/badge/Download-Windows%20x64-blue?style=for-the-badge&logo=windows" alt="Download the latest AxiSim release for Windows x64">
</a>

<sub>Requires an NVIDIA GPU (compute capability 7.5+) — see <a href="#requirements">Requirements</a></sub>

<img src="docs/images/v1.04-alpha%20screenshots/Picture5.png" alt="AxiSim v1.04-alpha solver view with concentration boundary settings and live residual plots" width="780">

</div>

---

## What is AxiSim?

AxiSim solves the **incompressible Navier–Stokes equations in axisymmetric (r–z) coordinates** using the
finite-volume method and the **SIMPLE** pressure–velocity coupling scheme. The heavy linear algebra runs
on the GPU through CUDA, and the entire workflow — geometry, meshing, solving, and post-processing — lives
in a single real-time interface.

It's built for anyone who wants to model pipe flow, jets, nozzles, and other rotationally-symmetric
problems without stitching together separate CAD, meshing, solver, and visualization tools.

## Features

**🎨 Model & mesh**
- Interactive geometry sketching — lines, rectangles, circles, and arcs with live dimensions
- **Structured** (Cartesian) and **unstructured** (Gmsh triangulation) meshing
- Region-of-influence controls for local mesh refinement
- Boundary groups with per-group sizing (edge count, target spacing, bias)
- Structured boundary groups are preserved across mesh regeneration where possible

**🧮 Physics & numerics**
- Axisymmetric incompressible Navier–Stokes, finite-volume discretization
- SIMPLE pressure–velocity coupling with under-relaxation
- Guarded Continue Solver mode for resuming compatible solver states
- Convection schemes: first-order upwind, central difference, second-order upwind
- Gradient schemes: Green–Gauss and least-squares
- Optional scalar transport: **energy (temperature)** and **concentration**
- Steady-state and transient modes, with opt-in non-orthogonal correction
- Boundary conditions: wall, velocity inlet, pressure outlet, symmetry

**⚡ GPU-accelerated**
- CUDA linear solvers (Jacobi, red–black Gauss–Seidel, Jacobi-PCG)
- Built and tuned for modern NVIDIA GPUs (Turing and newer)

**📊 Visualize & analyze**
- 3D revolved field visualization with selectable colormaps
- 2D cross-section inspector with per-cell field values and an auto-sizing live colorbar
- Live residual plots for U, V, pressure, continuity, temperature, and concentration
- Per-face mass-flux and continuity-imbalance inspection
- Command console with live autocomplete for actions, objects, and supported values
- Save / load projects, and copy results and images straight to the clipboard

## Screenshots

| Geometry sketching | Structured meshing |
| :---: | :---: |
| <img src="docs/images/v1.04-alpha%20screenshots/Picture6.png" width="380"> | <img src="docs/images/v1.04-alpha%20screenshots/Picture1.png" width="380"> |
| **Unstructured meshing and refinement** | **Solver with live residuals** |
| <img src="docs/images/v1.04-alpha%20screenshots/Picture7.png" width="380"> | <img src="docs/images/v1.04-alpha%20screenshots/Picture5.png" width="380"> |

## Requirements

- **Windows 10 / 11 (x64)**
- An **NVIDIA GPU** with compute capability **7.5 or newer** (Turing / GTX 16 / RTX 20 series and up) and a recent driver
- *To build from source:* CUDA Toolkit 13.0, a C++20 compiler (Visual Studio 2022), CMake ≥ 3.24, and [vcpkg](https://vcpkg.io)

> **Note:** AxiSim runs its solver on the GPU and requires a compatible NVIDIA card. It will not run on machines without one.

## Download

**[⬇ Download AxiSim for Windows x64](https://github.com/Luippe/AxiSim-Software/releases/latest/download/AxiSim-win64.zip)** — always the latest release.

Unzip anywhere and run `AxiSim.exe`. Everything needed is bundled; no installer, no separate runtime to
install. Keep the folder contents together — the exe loads `assets/` and `graphics/shaders/` from
alongside itself.

> Windows SmartScreen may warn about an unknown publisher, since the build is not code-signed.
> Choose **More info → Run anyway**.

All releases, with notes, are on the [Releases page](https://github.com/Luippe/AxiSim-Software/releases).

## Building from source

AxiSim uses CMake with vcpkg for its dependencies (GLFW, GLAD, GLM, OpenGL). Gmsh and Dear ImGui/ImPlot are bundled.

```sh
# install dependencies via vcpkg
vcpkg install glfw3 glad glm

# configure & build (or open the folder directly in Visual Studio 2022)
cmake --preset x64-Release
cmake --build out/build/x64-Release
```

## Documentation

The full software manual is available here: [AxiSim Software Manual v1.04-alpha](docs/AxiSim_Software_Manual_v1.04-alpha.pdf).

Release notes are tracked in the [v1.04-alpha update log](docs/update%20logs/v1.04-alpha%20update%20log.md).

## Project status

AxiSim is **work in progress** (currently `v1.04-alpha`) and updated frequently. Expect rapid changes and the occasional rough edge.

## License

The original AxiSim source code is released under the **[MIT License](LICENSE)**.

Third-party components bundled with or linked into AxiSim retain their own licenses. **Important:** AxiSim links the [Gmsh](https://gmsh.info) library, which is licensed under the **GNU GPL v2-or-later**. Binaries built and distributed with Gmsh are therefore subject to the GPL, in addition to the MIT terms that cover AxiSim's own source.

## Acknowledgments

AxiSim is built with [Dear ImGui](https://github.com/ocornut/imgui), [ImPlot](https://github.com/epezent/implot), [Gmsh](https://gmsh.info), [GLFW](https://www.glfw.org), [GLAD](https://github.com/Dav1dde/glad), [GLM](https://github.com/g-truc/glm), [stb_image](https://github.com/nothings/stb), and the [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit).
