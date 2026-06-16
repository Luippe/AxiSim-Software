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
# tinyfiledialogs
# ------------------------------------------------------------

set(TINYFILEDIALOGS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/graphics/tinyfiledialogs")

set(TINYFILEDIALOGS_SOURCES
    "${TINYFILEDIALOGS_DIR}/tinyfiledialogs.c"
)

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

# ------------------------------------------------------------
# clip clipboard library
# ------------------------------------------------------------

set(CLIP_EXAMPLES OFF CACHE BOOL "Compile clip examples")
set(CLIP_TESTS OFF CACHE BOOL "Compile clip tests")

# Only needed if you want image clipboard support later
# set(CLIP_ENABLE_IMAGE ON CACHE BOOL "Enable image clipboard support")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/externals/clip-main")


add_executable(AxiSim
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
    ${TINYFILEDIALOGS_SOURCES}
  "externals/stb_image/stb_image.cpp" "manager/header/flag_manager.h"   "graphics/gui/source/mesh_inspector.cpp" "graphics/gui/header/base_surface_viewer.h" "manager/header/boundary_func.h"  "manager/header/font_manager.h" "src/source/project.cpp")

target_include_directories(AxiSim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/structs"

    ${PROJECT_INCLUDE_DIRS}

    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/graphics/imgui/backends"
    "${CMAKE_SOURCE_DIR}/externals/stb_image"
    "${CMAKE_SOURCE_DIR}/externals/font_awesome"


    "${IMPLOT_DIR}"
    "${TINYFILEDIALOGS_DIR}"

    ${CUDAToolkit_INCLUDE_DIRS}
)

target_include_directories(AxiSim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/externals/CDT/CDT/include"
)

target_link_libraries(AxiSim PRIVATE
    OpenGL::GL
    glad::glad
    glfw
    glm::glm
    CUDA::cudart
    clip
)

set_target_properties(AxiSim PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
    ${IMGUI_SOURCES}
    ${IMPLOT_SOURCES}
    ${TINYFILEDIALOGS_SOURCES}
)