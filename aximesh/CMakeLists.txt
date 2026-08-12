# ------------------------------------------------------------
# aximesh -- 2D mesh generation library
#
# Deliberately isolated from AxiSim: no AxiSim headers, no CUDA, no gmsh, no
# PCH. That isolation is the point -- it keeps the target quick to build,
# drivable from aximesh_cli without the GUI, and extractable with
# `git subtree split` if it ever becomes its own repo.
#
# Sources are globbed with CONFIGURE_DEPENDS, same as the root CMakeLists, so
# new files under source/ and include/ are picked up with no edit here.
# ------------------------------------------------------------

file(GLOB_RECURSE AXIMESH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
)

# include/ is the public API. A .h under source/ is private to the library and
# resolves from a sibling .cpp with a plain #include "name.h".
file(GLOB_RECURSE AXIMESH_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h"
)

add_library(aximesh STATIC
    ${AXIMESH_SOURCES}
    ${AXIMESH_HEADERS}
)

# PUBLIC so anything linking aximesh can #include "aximesh/..." with no further
# setup. The aximesh/ prefix keeps these headers out of AxiSim's flat include
# space, where a name like mesh.h would collide.
target_include_directories(aximesh PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(aximesh PUBLIC cxx_std_20)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${AXIMESH_SOURCES}
    ${AXIMESH_HEADERS}
)

# ------------------------------------------------------------
# aximesh_cli -- headless harness. No CUDA in the link, so it builds and runs
# in a fraction of the time AxiSim.exe takes.
# ------------------------------------------------------------

add_executable(aximesh_cli
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/aximesh_cli.cpp"
)

target_link_libraries(aximesh_cli PRIVATE aximesh)
