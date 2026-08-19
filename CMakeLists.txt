cmake_minimum_required(VERSION 3.24)

project(AxiSimSoftware LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# External libraries from vcpkg

find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(CUDAToolkit REQUIRED)

if(WIN32)
    file(GLOB CUDART_RUNTIME_FILES CONFIGURE_DEPENDS
        "${CUDAToolkit_BIN_DIR}/cudart64_*.dll"
        "${CUDAToolkit_BIN_DIR}/x64/cudart64_*.dll"
    )
elseif(UNIX)
    file(GLOB CUDART_RUNTIME_FILES CONFIGURE_DEPENDS
        "${CUDAToolkit_LIBRARY_DIR}/libcudart.so*"
    )
endif()

if(NOT CUDART_RUNTIME_FILES)
    message(FATAL_ERROR "Could not locate the CUDA runtime library for packaging")
endif()

# ------------------------------------------------------------
# Project source files
# ------------------------------------------------------------


file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu"

    "${CMAKE_CURRENT_SOURCE_DIR}/manager/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager/*.cu"

    "${CMAKE_CURRENT_SOURCE_DIR}/solver/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/solver/*.cu"

    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.cu"

    "${CMAKE_CURRENT_SOURCE_DIR}/structs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/structs/*.cu"
)

file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cuh"

    "${CMAKE_CURRENT_SOURCE_DIR}/manager/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/manager/*.cuh"

    "${CMAKE_CURRENT_SOURCE_DIR}/solver/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/solver/*.cuh"

    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/*.cuh"

    "${CMAKE_CURRENT_SOURCE_DIR}/structs/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/structs/*.cuh"
)

# ------------------------------------------------------------
# Remove graphics/imgui from the general project lists.
# We only want specific ImGui files, not every backend.
# ------------------------------------------------------------

function(filter_out_subdir list_name subdir)
    set(filtered "")

    foreach(path IN LISTS ${list_name})
        file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_SOURCE_DIR}" "${path}")

        if(NOT rel_path MATCHES "^${subdir}/")
            list(APPEND filtered "${path}")
        endif()
    endforeach()

    set(${list_name} "${filtered}" PARENT_SCOPE)
endfunction()

filter_out_subdir(PROJECT_SOURCES "graphics/imgui")
filter_out_subdir(PROJECT_HEADERS "graphics/imgui")

# ------------------------------------------------------------
# ImGui
# ------------------------------------------------------------

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui")

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"

    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# Safety check: fail immediately if the wrong ImGui backend is included
foreach(src IN LISTS PROJECT_SOURCES IMGUI_SOURCES)
    if(src MATCHES "imgui_impl_(android|allegro5|glut|sdl|vulkan|dx)")
        message(FATAL_ERROR "Wrong ImGui backend is being compiled: ${src}")
    endif()
endforeach()

# ------------------------------------------------------------
# ImPlot
# ------------------------------------------------------------

# Vendored here rather than inside graphics/imgui: that directory is the upstream
# Dear ImGui submodule, which does not ship ImPlot and cannot track files the
# parent repository adds to it. Sources kept under externals/ are version
# controlled, so a fresh clone builds.
set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/implot")

set(IMPLOT_SOURCES
    "${IMPLOT_DIR}/implot.cpp"
    "${IMPLOT_DIR}/implot_items.cpp"
    # "${IMPLOT_DIR}/implot_demo.cpp" # optional
)

# ------------------------------------------------------------
# gmsh
# ------------------------------------------------------------

set(AXISIM_GMSH_SDK_ROOT "" CACHE PATH
    "Optional path to an already-extracted Gmsh SDK (must contain include/ and lib/)")

if(AXISIM_GMSH_SDK_ROOT)
    set(GMSH_ROOT "${AXISIM_GMSH_SDK_ROOT}")
elseif(WIN32)
    set(GMSH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/externals/gmsh/windows")
elseif(UNIX AND NOT APPLE)
    set(GMSH_LINUX_ARCHIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/gmsh/linux")
    file(GLOB GMSH_LINUX_ARCHIVES CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
        "${GMSH_LINUX_ARCHIVE_DIR}/gmsh-*-Linux64-sdk.tgz"
    )
    list(LENGTH GMSH_LINUX_ARCHIVES GMSH_LINUX_ARCHIVE_COUNT)

    if(GMSH_LINUX_ARCHIVE_COUNT EQUAL 0)
        message(FATAL_ERROR
            "Could not find a Linux Gmsh SDK archive matching "
            "${GMSH_LINUX_ARCHIVE_DIR}/gmsh-*-Linux64-sdk.tgz")
    elseif(GMSH_LINUX_ARCHIVE_COUNT GREATER 1)
        message(FATAL_ERROR
            "Found multiple Linux Gmsh SDK archives in ${GMSH_LINUX_ARCHIVE_DIR}. "
            "Keep exactly one archive there or set AXISIM_GMSH_SDK_ROOT.")
    endif()

    list(GET GMSH_LINUX_ARCHIVES 0 GMSH_LINUX_ARCHIVE)
    get_filename_component(GMSH_LINUX_ARCHIVE_NAME "${GMSH_LINUX_ARCHIVE}" NAME)
    string(REGEX REPLACE "\\.tgz$" "" GMSH_LINUX_SDK_DIR_NAME
        "${GMSH_LINUX_ARCHIVE_NAME}")

    set(GMSH_EXTRACT_BASE "${CMAKE_CURRENT_BINARY_DIR}/_deps/gmsh")
    set(GMSH_ROOT "${GMSH_EXTRACT_BASE}/${GMSH_LINUX_SDK_DIR_NAME}")
    file(TIMESTAMP "${GMSH_LINUX_ARCHIVE}" GMSH_LINUX_ARCHIVE_TIMESTAMP
        "%Y%m%d%H%M%S" UTC)
    file(SIZE "${GMSH_LINUX_ARCHIVE}" GMSH_LINUX_ARCHIVE_SIZE)
    set(GMSH_EXTRACT_STAMP
        "${GMSH_EXTRACT_BASE}/.${GMSH_LINUX_SDK_DIR_NAME}-${GMSH_LINUX_ARCHIVE_TIMESTAMP}-${GMSH_LINUX_ARCHIVE_SIZE}.stamp")

    if(NOT EXISTS "${GMSH_ROOT}/include/gmsh.h_cwrap"
       OR NOT EXISTS "${GMSH_EXTRACT_STAMP}")
        message(STATUS "Extracting Linux Gmsh SDK: ${GMSH_LINUX_ARCHIVE_NAME}")
        file(REMOVE_RECURSE "${GMSH_EXTRACT_BASE}")
        file(MAKE_DIRECTORY "${GMSH_EXTRACT_BASE}")
        file(ARCHIVE_EXTRACT
            INPUT "${GMSH_LINUX_ARCHIVE}"
            DESTINATION "${GMSH_EXTRACT_BASE}"
        )
        file(WRITE "${GMSH_EXTRACT_STAMP}"
            "Extracted from ${GMSH_LINUX_ARCHIVE_NAME}\n")
    endif()
else()
    message(FATAL_ERROR
        "No bundled Gmsh SDK is configured for this platform. "
        "Set AXISIM_GMSH_SDK_ROOT to an already-extracted SDK.")
endif()

message(STATUS "Using Gmsh SDK: ${GMSH_ROOT}")

set(GMSH_INCLUDE_DIR "${GMSH_ROOT}/include")

if(NOT EXISTS "${GMSH_INCLUDE_DIR}/gmsh.h_cwrap")
    message(FATAL_ERROR "Could not find gmsh.h_cwrap at: ${GMSH_INCLUDE_DIR}/gmsh.h_cwrap")
endif()

if(WIN32)
    set(GMSH_LIBRARY "${GMSH_ROOT}/lib/gmsh.dll.lib")

    if(NOT EXISTS "${GMSH_LIBRARY}")
        message(FATAL_ERROR "Could not find the Gmsh import library at: ${GMSH_LIBRARY}")
    endif()

    set(GMSH_RUNTIME "${GMSH_ROOT}/lib/gmsh.dll")
    if(NOT EXISTS "${GMSH_RUNTIME}")
        set(GMSH_RUNTIME "${GMSH_ROOT}/bin/gmsh.dll")
    endif()
    if(NOT EXISTS "${GMSH_RUNTIME}")
        set(GMSH_RUNTIME "${GMSH_ROOT}/lib/gmsh-4.15.dll")
    endif()

    add_library(Gmsh::gmsh SHARED IMPORTED)
    set_target_properties(Gmsh::gmsh PROPERTIES
        IMPORTED_IMPLIB "${GMSH_LIBRARY}"
        IMPORTED_LOCATION "${GMSH_RUNTIME}"
    )
else()
    # The official Linux SDK provides lib/libgmsh.so plus its versioned
    # SONAME symlinks. An explicit AXISIM_GMSH_SDK_ROOT can also point to a
    # compatible extracted SDK.
    find_library(GMSH_LIBRARY
        NAMES gmsh
        HINTS "${GMSH_ROOT}"
        PATH_SUFFIXES lib
        REQUIRED
    )

    get_filename_component(GMSH_LIBRARY_DIR "${GMSH_LIBRARY}" DIRECTORY)
    file(GLOB GMSH_RUNTIME_FILES CONFIGURE_DEPENDS
        "${GMSH_LIBRARY_DIR}/libgmsh.so*"
        "${GMSH_LIBRARY_DIR}/libgmsh.dylib*"
    )

    if(NOT GMSH_RUNTIME_FILES)
        message(FATAL_ERROR "Could not locate the Gmsh shared-library runtime in ${GMSH_LIBRARY_DIR}")
    endif()

    add_library(Gmsh::gmsh SHARED IMPORTED)
    set_target_properties(Gmsh::gmsh PROPERTIES
        IMPORTED_LOCATION "${GMSH_LIBRARY}"
    )
endif()

# ------------------------------------------------------------
# Automatically add all folders that contain your headers
# ------------------------------------------------------------

set(PROJECT_INCLUDE_DIRS "")

foreach(header_file IN LISTS PROJECT_HEADERS)
    get_filename_component(header_dir "${header_file}" DIRECTORY)
    list(APPEND PROJECT_INCLUDE_DIRS "${header_dir}")
endforeach()

list(REMOVE_DUPLICATES PROJECT_INCLUDE_DIRS)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/solver/source/solver.cpp"
    PROPERTIES LANGUAGE CUDA
)

# ------------------------------------------------------------
# Sub-projects
# ------------------------------------------------------------

# Not covered by the globs above, so its sources stay out of the AxiSim target
# and the boundary is enforced by the compiler rather than by convention.
add_subdirectory(aximesh)

# ------------------------------------------------------------
# Executable
# ------------------------------------------------------------

add_executable(AxiSim
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/externals/stb_image/stb_image.cpp"
 "structs/app_struct.h" "manager/header/file_converter.h" "structs/file_struct.h")

if(WIN32 AND EXISTS "${GMSH_RUNTIME}")
    add_custom_command(TARGET AxiSim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GMSH_RUNTIME}"
        "$<TARGET_FILE_DIR:AxiSim>"
    )
elseif(WIN32)
    message(WARNING "gmsh.dll was not found. The project may compile but fail to run.")
endif()

# Bundle the demo presets next to the exe so the exe-anchored preset loader
# (executableDir()/presets/...) finds them regardless of the working directory.
add_custom_command(TARGET AxiSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/presets"
    "$<TARGET_FILE_DIR:AxiSim>/presets"
)

target_include_directories(AxiSim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/structs"

    ${PROJECT_INCLUDE_DIRS}

    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui/backends"
    "${CMAKE_SOURCE_DIR}/externals/stb_image"
    "${GMSH_INCLUDE_DIR}"

    "${IMPLOT_DIR}"

    ${CUDAToolkit_INCLUDE_DIRS}
)

target_link_libraries(AxiSim PRIVATE
    OpenGL::GL
    glad::glad
    glfw
    glm::glm
    nfd::nfd
    PNG::PNG
    CUDA::cudart
    Gmsh::gmsh
    aximesh
)

# Fallback resource root for a build-tree exe. Resources::resolve() looks next to
# the exe first (the packaged layout below), then here -- so running AxiSim.exe
# straight out of out/build/... still finds assets/ and graphics/shaders/ without
# copying them, and an edited shader is picked up on the next run. On a machine
# the package was copied to this path simply does not exist and is skipped.
#
# Scoped to the one file that reads it: as a target-wide definition it would put
# the source path on every C++ and CUDA command line and rebuild the world.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/manager/source/resource_path.cpp"
    PROPERTIES COMPILE_DEFINITIONS "AXISIM_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
)

if(WIN32)
    target_compile_definitions(AxiSim PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(AxiSim PRIVATE
        # Media Foundation, used by video_writer.cpp to encode H.264 mp4 without
        # bundling an encoder or needing ffmpeg on the user's PATH
        Mfplat
        Mfreadwrite
        Mfuuid
    )
endif()

if(UNIX)
    # The portable archive/AppImage keeps shared libraries beside AxiSim.
    # Preserve the SDK path for build-tree runs, then use the executable's
    # directory after installation.
    set_target_properties(AxiSim PROPERTIES
        BUILD_RPATH "${GMSH_LIBRARY_DIR}"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# CUDA GPU target. The default builds native SASS (-real) for this machine's GPU
# only -- sm_86 (Ampere, RTX 30 series) -- to keep local build times short.
#
# The release presets override it with the distribution list
# "75-real;86-real;89-real;120-real;75-virtual" (Turing/Ampere/Ada/Blackwell
# SASS plus compute_75 PTX as a JIT fallback for anything >= 7.5). CUDA 13.0
# dropped Maxwell/Pascal/Volta, so 7.5 is the floor. Check arches with
# `nvcc --list-gpu-arch`.
set(AXISIM_CUDA_ARCHITECTURES "86-real" CACHE STRING
    "CUDA architectures passed to CMake (release packages should use a fat binary)")

set_target_properties(AxiSim PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES "${AXISIM_CUDA_ARCHITECTURES}"
)

# Per-kernel register/local-memory report from ptxas, printed to stderr during
# compilation. Purely diagnostic -- it does not affect codegen.
#
# Scoped to these two files rather than the whole target on purpose: adding it
# to the CUDA command line invalidates every .cu, and `cuobjdump -res-usage`
# on the linked exe showed only three kernels with a nonzero stack frame --
# addTransportCoefficients here, createPPRhs and updateMassFlux in simple.cu.
#
# What -v established that cuobjdump could not: all three report 0 spill
# stores / 0 spill loads, so those frames are ABI overhead from
# SEPARABLE_COMPILATION, not register pressure. The cost is in the __device__
# helpers that survive as standalone ABI functions -- see the note at the top
# of solver_util.cuh. Compare the per-TU register counts printed here against
# `cuobjdump -res-usage` on the linked exe: the latter is higher because the
# device link step resolves those external calls into the launch footprint.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/solver/source/simple.cu"
    "${CMAKE_CURRENT_SOURCE_DIR}/solver/source/solver_util.cu"
    PROPERTIES COMPILE_OPTIONS "-Xptxas=-v"
)

# ------------------------------------------------------------
# Precompiled header
#
# Roughly half of every C++ translation unit here was re-parsing the same
# third-party headers (imgui, glm, glad) and C++20 standard headers (<format>,
# <filesystem>, ...) -- about 1.7 s of the ~2 s each file took, repeated across
# ~47 files. manager/header/pch.h collects that set; see the comment there for
# why no project headers are in it.
#
# COMPILE_LANGUAGE:CXX keeps this away from nvcc, which has no MSVC-PCH support.
# That also excludes solver/source/solver.cpp, which is built as CUDA above.
# ------------------------------------------------------------

target_precompile_headers(AxiSim PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/manager/header/pch.h>"
)

# Vendored sources are compiled as-is: they do not include the project's headers,
# they carry their own include-order requirements, and force-feeding them the PCH
# only makes them slower.
set_source_files_properties(
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/externals/stb_image/stb_image.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
)

# ------------------------------------------------------------
# Install / packaging
#
# Assembles a self-contained, redistributable folder:
#
#   <prefix>/
#     AxiSim.exe
#     glfw3.dll, gmsh*.dll, MSVC runtime DLLs
#     assets/      (fonts + icons, resolved at runtime against the exe directory)
#     graphics/shaders/  (GLSL, same)
#
# Build the package with package.ps1, or manually:
#   cmake --install <build-dir> --prefix dist/AxiSim
#
# Note: AxiSim is CUDA software -- the target machine still needs an
# NVIDIA GPU (compute capability >= 7.5) with a recent driver. That
# cannot be bundled.
# ------------------------------------------------------------

install(TARGETS AxiSim RUNTIME DESTINATION .)

# Runtime asset folders are resolved by Resources::resolve() (resource_path.h),
# which anchors names like "assets/icons/house.png" and "graphics/shaders/mesh.vert"
# to the exe directory, so they must sit alongside the exe with the same layout.
# "raw icons" holds the oversized source art that tools/icon128.py downsamples
# into icons/; it is never read at runtime, so keep it out of the package.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"           DESTINATION .
        PATTERN "raw icons" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders" DESTINATION graphics)

# Bundled presets, resolved next to the exe by loadPresetProject().
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/presets" DESTINATION .)

if(UNIX)
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/axisim" DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/AxiSim.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/image_buttons/house.png"
            DESTINATION share/icons/hicolor/128x128/apps
            RENAME axisim.png)
endif()

# Install the native Gmsh runtime. Linux SDKs include both an unversioned .so
# and versioned SONAME links, all of which are required in a relocatable bundle.
if(WIN32 AND EXISTS "${GMSH_RUNTIME}")
    install(FILES "${GMSH_RUNTIME}" DESTINATION .)
elseif(UNIX)
    install(FILES ${GMSH_RUNTIME_FILES} DESTINATION .)
    install(FILES ${CUDART_RUNTIME_FILES} DESTINATION .)
endif()

# DLLs of linked imported targets (glfw3.dll, and any others vcpkg provides).
# gmsh is linked via a raw .lib path, not an imported target, so it is not
# captured here -- hence the explicit install above.
if(WIN32)
    # vcpkg's app-local step resolves transitive runtime dependencies (including
    # libpng's zlib dependency) beside the built executable. Install that exact
    # resolved set rather than relying only on TARGET_RUNTIME_DLLS, which does
    # not see dependencies hidden behind some Find-module interface targets.
    install(CODE "
        file(GLOB AXISIM_APPLOCAL_DLLS \"${CMAKE_CURRENT_BINARY_DIR}/*.dll\")
        if(AXISIM_APPLOCAL_DLLS)
            file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\"
                 TYPE FILE FILES \${AXISIM_APPLOCAL_DLLS})
        endif()
    ")
    install(FILES ${CUDART_RUNTIME_FILES} DESTINATION .)
endif()

# Bundle the Visual C++ runtime DLLs (MSVCP140.dll, VCRUNTIME140*.dll) so
# users do not need to install the redistributable separately.
if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
    endif()
endif()
