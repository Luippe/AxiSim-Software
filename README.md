<div align="center">

# AxiSim

### GPU-accelerated axisymmetric CFD, from sketch to solution.

AxiSim is an interactive finite-volume solver for **axisymmetric incompressible flow**.
Sketch a geometry, mesh it, run the solver on your GPU, and explore the results — all in one app.

![version](https://img.shields.io/badge/version-v1.03--alpha-blue)
![platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)
![GPU](https://img.shields.io/badge/GPU-NVIDIA%20CUDA-76B900)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![license](https://img.shields.io/badge/license-MIT-green)

<img src="docs/images/v1.03-alpha%20screenshots/Picture3.png" alt="AxiSim results view — 3D revolved axial-velocity field with a 2D cross-section inspector" width="780">

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

**🧮 Physics & numerics**
- Axisymmetric incompressible Navier–Stokes, finite-volume discretization
- SIMPLE pressure–velocity coupling with under-relaxation
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
- 2D cross-section inspector with per-cell field values and a live colorbar
- Live residual plots for U, V, pressure, continuity, temperature, and concentration
- Per-face mass-flux and continuity-imbalance inspection
- Save / load projects, and copy results and images straight to the clipboard

## Screenshots

| Geometry sketching | Structured meshing |
| :---: | :---: |
| <img src="docs/images/v1.04-alpha%20screenshots/Picture2.png" width="380"> | <img src="docs/images/v1.03-alpha%20screenshots/Picture1.png" width="380"> |
| **Unstructured meshing** | **Solver with live residuals** |
| <img src="docs/images/v1.04-alpha%20screenshots/Picture1.png" width="380"> | <img src="docs/images/v1.03-alpha%20screenshots/Picture2.png" width="380"> |

## Requirements

- **Windows 10 / 11 (x64)**
- An **NVIDIA GPU** with compute capability **7.5 or newer** (Turing / GTX 16 / RTX 20 series and up) and a recent driver
- *To build from source:* CUDA Toolkit 13.0, a C++20 compiler (Visual Studio 2022), CMake ≥ 3.24, and [vcpkg](https://vcpkg.io)

> **Note:** AxiSim runs its solver on the GPU and requires a compatible NVIDIA card. It will not run on machines without one.

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

The full software manual is available here: [AxiSim Software Manual](docs/AxiSim_Software_Manual%20v1.01-alpha.pdf).

## Project status

AxiSim is **work in progress** (currently `v1.03-alpha`) and updated frequently. Expect rapid changes and the occasional rough edge.

## License

The original AxiSim source code is released under the **[MIT License](LICENSE)**.

Third-party components bundled with or linked into AxiSim retain their own licenses — see [license.txt](license.txt) for the full notices. **Important:** AxiSim links the [Gmsh](https://gmsh.info) library, which is licensed under the **GNU GPL v2-or-later**. Binaries built and distributed with Gmsh are therefore subject to the GPL, in addition to the MIT terms that cover AxiSim's own source.

## Acknowledgments

AxiSim is built with [Dear ImGui](https://github.com/ocornut/imgui), [ImPlot](https://github.com/epezent/implot), [Gmsh](https://gmsh.info), [GLFW](https://www.glfw.org), [GLAD](https://github.com/Dav1dde/glad), [GLM](https://github.com/g-truc/glm), [stb_image](https://github.com/nothings/stb), and the [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit).
