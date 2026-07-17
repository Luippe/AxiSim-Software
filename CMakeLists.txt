cmake_minimum_required(VERSION 3.24)

project(AxiSimSoftware LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# External libraries from vcpkg

find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(CUDAToolkit REQUIRED)

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

set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui")

set(IMPLOT_SOURCES
    "${IMPLOT_DIR}/implot.cpp"
    "${IMPLOT_DIR}/implot_items.cpp"
    # "${IMPLOT_DIR}/implot_demo.cpp" # optional
)

# ------------------------------------------------------------
# gmsh
# ------------------------------------------------------------

set(GMSH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/externals/gmsh")

set(GMSH_INCLUDE_DIR "${GMSH_ROOT}/include")
set(GMSH_LIB "${GMSH_ROOT}/lib/gmsh.dll.lib")

if(NOT EXISTS "${GMSH_INCLUDE_DIR}/gmsh.h_cwrap")
    message(FATAL_ERROR "Could not find gmsh.h_cwrap at: ${GMSH_INCLUDE_DIR}/gmsh.h_cwrap")
endif()

if(NOT EXISTS "${GMSH_LIB}")
    message(FATAL_ERROR "Could not find gmsh.dll.lib at: ${GMSH_LIB}")
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
# Executable
# ------------------------------------------------------------

add_executable(AxiSim
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/externals/stb_image/stb_image.cpp"
 "structs/app_struct.h")

set(GMSH_DLL "${GMSH_ROOT}/lib/gmsh.dll")

if(NOT EXISTS "${GMSH_DLL}")
    set(GMSH_DLL "${GMSH_ROOT}/bin/gmsh.dll")
endif()

if(NOT EXISTS "${GMSH_DLL}")
    set(GMSH_DLL "${GMSH_ROOT}/lib/gmsh-4.15.dll")
endif()

if(EXISTS "${GMSH_DLL}")
    add_custom_command(TARGET AxiSim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GMSH_DLL}"
        "$<TARGET_FILE_DIR:AxiSim>"
    )
else()
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
    CUDA::cudart
    "${GMSH_LIB}"
)

if(WIN32)
    target_compile_definitions(AxiSim PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(AxiSim PRIVATE
        Comdlg32
    )
endif()

# CUDA GPU target. Building native SASS (-real) for this machine's GPU only --
# sm_86 (Ampere, RTX 30 series) -- to keep local build times short.
#
# For distribution across many consumer cards, use a multi-arch list instead,
# e.g. "75-real;86-real;89-real;120-real;75-virtual" (Turing/Ampere/Ada/Blackwell
# SASS plus compute_75 PTX as a JIT fallback for anything >= 7.5). CUDA 13.0
# dropped Maxwell/Pascal/Volta, so 7.5 is the floor. Check arches with
# `nvcc --list-gpu-arch`.
set_target_properties(AxiSim PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES "86-real"
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
#     assets/      (fonts + icons, loaded at runtime by relative path)
#     graphics/shaders/  (GLSL, loaded at runtime by relative path)
#
# Build the package with package.ps1, or manually:
#   cmake --install <build-dir> --prefix dist/AxiSim
#
# Note: AxiSim is CUDA software -- the target machine still needs an
# NVIDIA GPU (compute capability >= 7.5) with a recent driver. That
# cannot be bundled.
# ------------------------------------------------------------

install(TARGETS AxiSim RUNTIME DESTINATION .)

# Runtime asset folders are loaded via paths relative to the working
# directory (e.g. "assets/icons/house.png", "graphics/shaders/mesh.vert"),
# so they must sit alongside the exe with the same layout.
# "raw icons" holds the oversized source art that tools/icon128.py downsamples
# into icons/; it is never read at runtime, so keep it out of the package.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"           DESTINATION .
        PATTERN "raw icons" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders" DESTINATION graphics)

# Bundled presets, resolved next to the exe by loadPresetProject().
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/presets" DESTINATION .)

# gmsh ships only as a DLL (GMSH_DLL is resolved above in the build section).
if(EXISTS "${GMSH_DLL}")
    install(FILES "${GMSH_DLL}" DESTINATION .)
endif()

# DLLs of linked imported targets (glfw3.dll, and any others vcpkg provides).
# gmsh is linked via a raw .lib path, not an imported target, so it is not
# captured here -- hence the explicit install above.
install(FILES $<TARGET_RUNTIME_DLLS:AxiSim> DESTINATION .)

# Bundle the Visual C++ runtime DLLs (MSVCP140.dll, VCRUNTIME140*.dll) so
# users do not need to install the redistributable separately.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
include(InstallRequiredSystemLibraries)
if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
endif()
