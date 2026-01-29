#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "libzlink" for configuration "Release"
set_property(TARGET libzlink APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(libzlink PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/libzlink-v145-mt-4_3_5.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/libzlink-v145-mt-4_3_5.dll"
  )

list(APPEND _cmake_import_check_targets libzlink )
list(APPEND _cmake_import_check_files_for_libzlink "${_IMPORT_PREFIX}/lib/libzlink-v145-mt-4_3_5.lib" "${_IMPORT_PREFIX}/bin/libzlink-v145-mt-4_3_5.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
