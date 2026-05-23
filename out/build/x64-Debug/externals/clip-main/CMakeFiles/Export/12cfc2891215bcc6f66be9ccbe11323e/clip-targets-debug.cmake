#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "clip::clip" for configuration "Debug"
set_property(TARGET clip::clip APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(clip::clip PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/clip.lib"
  )

list(APPEND _cmake_import_check_targets clip::clip )
list(APPEND _cmake_import_check_files_for_clip::clip "${_IMPORT_PREFIX}/lib/clip.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
