# Zlink cmake module
#
# The following import targets are created
#
# ::
#
#   libzlink-static
#   libzlink
#
# This module sets the following variables in your project::
#
#   Zlink_FOUND - true if Zlink found on the system
#   Zlink_INCLUDE_DIR - the directory containing Zlink headers
#   Zlink_LIBRARY - 
#   Zlink_STATIC_LIBRARY


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ZlinkConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

if(NOT TARGET libzlink AND NOT TARGET libzlink-static)
  include("${CMAKE_CURRENT_LIST_DIR}/ZlinkTargets.cmake")

  if (TARGET libzlink)
    get_target_property(Zlink_INCLUDE_DIR libzlink INTERFACE_INCLUDE_DIRECTORIES)
  else ()
    get_target_property(Zlink_INCLUDE_DIR libzlink-static INTERFACE_INCLUDE_DIRECTORIES)
  endif()

  if (TARGET libzlink)
    get_target_property(Zlink_LIBRARY libzlink LOCATION)
  endif()
  if (TARGET libzlink-static)
    get_target_property(Zlink_STATIC_LIBRARY libzlink-static LOCATION)
  endif()
endif()
