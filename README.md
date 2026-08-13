<div align="center">

# AxiSim

### GPU-accelerated axisymmetric CFD, from sketch to solution.

AxiSim is an interactive finite-volume solver for **axisymmetric incompressible flow**.
Sketch a geometry, mesh it, run the solver on your GPU, and explore the results — all in one app.

![release](https://img.shields.io/badge/release-v1.04--alpha-blue)
![in development](https://img.shields.io/badge/in%20development-v1.05--alpha-orange)
![platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux%20(experimental)-lightgrey)
![GPU](https://img.shields.io/badge/GPU-NVIDIA%20CUDA-76B900)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)
![license](https://img.shields.io/badge/license-MIT-green)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.20452828.svg)](https://doi.org/10.5281/zenodo.20452828)

<a href="https://github.com/Luippe/AxiSim-Software/releases/latest/download/AxiSim-win64.zip">
  <img src="https://img.shields.io/badge/Download-Windows%20x64-blue?style=for-the-badge&logo=windows" alt="Download the latest AxiSim release for Windows x64">
</a>

<sub>Requires an NVIDIA GPU (compute capability 7.5+) — see <a href="#requirements">Requirements</a></sub>

<img src="docs/images/v1.05-alpha%20screenshot/0.png" alt="AxiSim results view: axial velocity around a revolved bullet body, with the 2D inspector, live colorbar and console" width="780">

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
- Interactive geometry sketching — lines, rectangles, circles, and arcs with live dimensions, snapping,
  trimming, copy/paste, and coordinate-based movement
- **Structured**, **multi-block structured**, and **unstructured** (Gmsh triangulation) meshing
- Multi-block meshes share axial and radial grid bands, so neighbouring blocks always match along a
  shared edge — no hanging nodes. Sketches that can't be decomposed fall back to a uniform grid
- Region-of-influence controls for local refinement, with optional boundary-spacing override
- Boundary groups with per-group sizing (edge count, target spacing, bias), preserved across
  regeneration where possible
- Mesh-quality measures and colour overlays for aspect ratio, non-orthogonality, and skewness, with
  per-cell inspection

**🧮 Physics & numerics**
- Axisymmetric incompressible Navier–Stokes, finite-volume discretization
- SIMPLE pressure–velocity coupling with under-relaxation
- **Steady-state and transient** modes, with first-order (backward Euler) or second-order (BDF2) time stepping
- Convection schemes: first-order upwind, central difference, second-order upwind
- Gradient schemes: Green–Gauss and least-squares, with opt-in non-orthogonal correction
- Optional scalar transport: **energy (temperature)** and **concentration**
- Boundary conditions: wall, velocity inlet, pressure outlet, symmetry, far field
- Inlet profiles: uniform, fully-developed, and **sinusoidal pulsatile** (phase carries across a continued run)
- Surface reaction kinetics for concentration — **Michaelis–Menten** and **Hill**, with optional substrate inhibition
- Guarded Continue Solver mode for resuming compatible solver states, and long runs can be stopped
  part-way without losing the partial solution

**⚡ GPU-accelerated**
- CUDA linear solvers (Jacobi, red–black Gauss–Seidel)
- **Geometric multigrid** for the pressure equation, on every mesh type, replayed as one recorded
  CUDA graph instead of many separate launches
- Built and tuned for modern NVIDIA GPUs (Turing and newer)

**📊 Visualize & analyze**
- 3D revolved field visualization with selectable colormaps, turntable and arcball cameras,
  orthographic/perspective projection, a clickable axis gizmo, standard view buttons, and automatic framing
- 2D cross-section inspector with per-cell field values, optional mirroring across the axis, and an
  auto-sizing live colorbar
- Fields include axial and radial velocity, velocity magnitude, pressure, cell Reynolds number,
  temperature, and concentration
- Live residual plots for U, V, pressure, continuity, temperature, and concentration
- Transient animation playback with a global or per-frame colour range, plus **MP4 and PNG-sequence export**
- Per-face mass-flux and continuity-imbalance inspection
- Command console with live autocomplete for actions, objects, and supported values
- Projects save and reload their solutions, display state, and animation frames; results and images
  copy straight to the clipboard

**🔬 Export & validation**
- **NumPy export** — `.npy` arrays plus a `meta.json` describing them, carrying real cell outlines,
  solved fields, and transient frames, all in SI units
- **OpenFOAM case export** — mesh, initial fields, fluid properties, and control/scheme dictionaries,
  ready to run, for both multi-block and unstructured meshes
- Validated against analytic Hagen–Poiseuille pipe flow, equivalent OpenFOAM runs, and the
  five-laboratory **FDA benchmark-nozzle** PIV dataset at throat Reynolds number 500, in both the
  sudden-expansion and conical-diffuser directions

## Screenshots

| Geometry sketching | Structured meshing |
| :---: | :---: |
| <img src="docs/images/v1.05-alpha%20screenshot/2.png" width="380"> | <img src="docs/images/v1.05-alpha%20screenshot/3.png" width="380"> |
| **Solver setup with live residuals** | **Results: 3D revolved field + 2D inspector** |
| <img src="docs/images/v1.05-alpha%20screenshot/4.png" width="380"> | <img src="docs/images/v1.05-alpha%20screenshot/5.png" width="380"> |

## Requirements

- **Windows 10 / 11 (x64)**
- An **NVIDIA GPU** with compute capability **7.5 or newer** (Turing / GTX 16 / RTX 20 series and up) and a recent driver
- *To build from source:* CUDA Toolkit 13.0, a C++20 compiler (Visual Studio 2022), CMake ≥ 3.24, and [vcpkg](https://vcpkg.io)

An experimental **Linux x86-64** source build is also available. It requires an
NVIDIA GPU, CUDA Toolkit 13, GCC, CMake, Ninja, vcpkg, and the official Gmsh Linux
SDK. Linux MP4 export and image clipboard copy are not implemented yet; PNG
sequence export is available.

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

AxiSim uses CMake with a vcpkg manifest for GLFW, GLAD, GLM, libpng, and native
file dialogs. Dear ImGui/ImPlot are bundled. CUDA comes from the NVIDIA toolkit,
and Gmsh comes from its official platform SDK.

Set `VCPKG_ROOT` to your vcpkg checkout before using the presets.

### Windows

```sh
# configure & build (or open the folder directly in Visual Studio 2022)
cmake --preset x64-Release
cmake --build --preset x64-Release
```

The debug preset builds SASS for one GPU (`sm_86`) to keep local builds short; the
release presets build the distribution fat binary covering compute capability 7.5
and newer. Override with `-DAXISIM_CUDA_ARCHITECTURES=...` if you need something else.

### Linux (experimental)

1. Install CUDA Toolkit 13, a supported GCC toolchain, CMake, Ninja, and the
   OpenGL/X11 or Wayland development packages for your distribution.
2. Put exactly one official Linux SDK archive in `externals/gmsh/linux/`, named
   like `gmsh-4.15.2-Linux64-sdk.tgz`. CMake extracts it into the build tree
   automatically; do not commit the extracted Linux SDK.
3. Configure, build, and package:

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-release
cmake --build --preset linux-release
bash package-linux.sh v1.04-alpha
```

To use an SDK that is already extracted elsewhere, configure with
`-DAXISIM_GMSH_SDK_ROOT=/opt/gmsh-sdk`.

The archive is written under `dist/`. Extract it and run the included `axisim`
launcher; the launcher keeps runtime assets relative to the executable. See
[`docs/linux-build.md`](docs/linux-build.md) for troubleshooting and packaging
details.

## Documentation

- **Software manual:** [AxiSim Software Manual v1.04-alpha](docs/AxiSim_Software_Manual_v1.04-alpha.pdf)
  (the v1.05-alpha manual is still being written; its LaTeX source is in `docs/`)
- **Release notes:** [v1.05-alpha update log](docs/update%20logs/v1.05-alpha%20update%20log.md) — the
  current development snapshot. Earlier logs are in [`docs/update logs/`](docs/update%20logs)
- **Linux build:** [`docs/linux-build.md`](docs/linux-build.md)

> **Upgrading:** v1.05-alpha corrects several physics bugs — concentration transport applied density
> twice, the cylindrical radial-momentum term was missing, and fixed-pressure boundaries and
> fully-developed inlets were incomplete. **Re-run cases produced with earlier versions.** See the
> compatibility notes at the end of the update log.

## Project status

AxiSim is **work in progress** and updated frequently. Expect rapid changes and the occasional rough edge.

- Latest release: **v1.04-alpha**
- This source tree: **v1.05-alpha**, in development and not yet tagged

Binary save formats (`.axi`, `.axigeom`, `.aximesh`, `.axislv`) are still evolving and may change again
before a stable release.

## Citing AxiSim

If you use AxiSim in published work, please cite the archived release. Citation metadata is in
[`CITATION.cff`](CITATION.cff), and the concept DOI
[10.5281/zenodo.20452828](https://doi.org/10.5281/zenodo.20452828) always resolves to the latest version.

## License

The original AxiSim source code is released under the **[MIT License](LICENSE)**.

Third-party components bundled with or linked into AxiSim retain their own licenses. **Important:** AxiSim links the [Gmsh](https://gmsh.info) library, which is licensed under the **GNU GPL v2-or-later**. Binaries built and distributed with Gmsh are therefore subject to the GPL, in addition to the MIT terms that cover AxiSim's own source.

## Acknowledgments

AxiSim is built with [Dear ImGui](https://github.com/ocornut/imgui), [ImPlot](https://github.com/epezent/implot), [Gmsh](https://gmsh.info), [GLFW](https://www.glfw.org), [GLAD](https://github.com/Dav1dde/glad), [GLM](https://github.com/g-truc/glm), [stb_image](https://github.com/nothings/stb), and the [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit).
