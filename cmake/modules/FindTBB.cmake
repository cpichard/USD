# Obtained from Justus Calvin:
# https://github.com/justusc/FindTBB/blob/25ecdea817b3af4a26d74ddcd439642dbd706acb/FindTBB.cmake
#
# With the following modifications:
# * Move the "tbb" imported library target into a namespace as "TBB::tbb" to
#   conform to modern CMake conventions.
# * Append "lib" as a library path suffix on all platforms.
#
# The MIT License (MIT)
#
# Copyright (c) 2015 Justus Calvin
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

#
# FindTBB
# -------
#
# Find TBB include directories and libraries.
#
# Usage:
#
#  find_package(TBB [major[.minor]] [EXACT]
#               [QUIET] [REQUIRED]
#               [[COMPONENTS] [components...]]
#               [OPTIONAL_COMPONENTS components...]) 
#
# where the allowed components are tbbmalloc and tbb_preview. Users may modify 
# the behavior of this module with the following variables:
#
# * TBB_ROOT_DIR          - The base directory the of TBB installation.
# * TBB_INCLUDE_DIR       - The directory that contains the TBB headers files.
# * TBB_LIBRARY           - The directory that contains the TBB library files.
# * TBB_<library>_LIBRARY - The path of the TBB the corresponding TBB library. 
#                           These libraries, if specified, override the 
#                           corresponding library search results, where <library>
#                           may be tbb, tbb_debug, tbbmalloc, tbbmalloc_debug,
#                           tbb_preview, or tbb_preview_debug.
# * TBB_USE_DEBUG_BUILD   - The debug version of tbb libraries, if present, will
#                           be used instead of the release version.
#
# Users may modify the behavior of this module with the following environment
# variables:
#
# * TBB_INSTALL_DIR 
# * TBBROOT
# * LIBRARY_PATH
#
# This module will set the following variables:
#
# * TBB_FOUND             - Set to false, or undefined, if we haven’t found, or
#                           don’t want to use TBB.
# * TBB_<component>_FOUND - If False, optional <component> part of TBB sytem is
#                           not available.
# * TBB_VERSION           - The full version string
# * TBB_VERSION_MAJOR     - The major version
# * TBB_VERSION_MINOR     - The minor version
# * TBB_INTERFACE_VERSION - The interface version number defined in 
#                           tbb/tbb_stddef.h.
# * TBB_<library>_LIBRARY_RELEASE - The path of the TBB release version of 
#                           <library>, where <library> may be tbb, tbb_debug,
#                           tbbmalloc, tbbmalloc_debug, tbb_preview, or 
#                           tbb_preview_debug.
# * TBB_<library>_LIBRARY_DEGUG - The path of the TBB release version of 
#                           <library>, where <library> may be tbb, tbb_debug,
#                           tbbmalloc, tbbmalloc_debug, tbb_preview, or 
#                           tbb_preview_debug.
#
# The following varibles should be used to build and link with TBB:
#
# * TBB_INCLUDE_DIRS        - The include directory for TBB.
# * TBB_LIBRARIES           - The libraries to link against to use TBB.
# * TBB_LIBRARIES_RELEASE   - The release libraries to link against to use TBB.
# * TBB_LIBRARIES_DEBUG     - The debug libraries to link against to use TBB.
# * TBB_DEFINITIONS         - Definitions to use when compiling code that uses
#                             TBB.
# * TBB_DEFINITIONS_RELEASE - Definitions to use when compiling release code that
#                             uses TBB.
# * TBB_DEFINITIONS_DEBUG   - Definitions to use when compiling debug code that
#                             uses TBB.
#
# This module will also create the "TBB::tbb" target that may be used when
# building executables and libraries.

include(FindPackageHandleStandardArgs)

if(NOT TBB_FOUND)

  ##################################
  # Check the build type
  ##################################
  
  if(NOT DEFINED TBB_USE_DEBUG_BUILD)
    if(CMAKE_BUILD_TYPE MATCHES "(Debug|DEBUG|debug|RelWithDebInfo|RELWITHDEBINFO|relwithdebinfo)")
      set(TBB_BUILD_TYPE DEBUG)
    else()
      set(TBB_BUILD_TYPE RELEASE)
    endif()
  elseif(TBB_USE_DEBUG_BUILD)
    set(TBB_BUILD_TYPE DEBUG)
  else()
    set(TBB_BUILD_TYPE RELEASE)
  endif()
  
  ##################################
  # Set the TBB search directories
  ##################################
  
  # Define search paths based on user input and environment variables
  set(TBB_SEARCH_DIR ${TBB_ROOT_DIR} $ENV{TBB_INSTALL_DIR} $ENV{TBBROOT})
  
  # Define the search directories based on the current platform
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TBB_DEFAULT_SEARCH_DIR "C:/Program Files/Intel/TBB"
                               "C:/Program Files (x86)/Intel/TBB")

    # Set the target architecture
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(TBB_ARCHITECTURE "intel64")
    else()
      set(TBB_ARCHITECTURE "ia32")
    endif()

    # Set the TBB search library/runtime path search suffix based on the version of VC
    if(WINDOWS_STORE)
      set(TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc11_ui")
      set(TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc11_ui")
    elseif(MSVC14)
      set(TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc14")
      set(TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc14")
    elseif(MSVC12)
      set(TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc12")
      set(TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc12")
    elseif(MSVC11)
      set(TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc11")
      set(TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc11")
    elseif(MSVC10)
      set(TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc10")
      set(TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc10")
    endif()

    # Add the library/runtime path search suffix for the VC independent version of TBB
    list(APPEND TBB_LIB_PATH_SUFFIX "lib/${TBB_ARCHITECTURE}/vc_mt")
    list(APPEND TBB_RUNTIME_PATH_SUFFIX "bin/${TBB_ARCHITECTURE}/vc_mt")

  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # OS X
    set(TBB_DEFAULT_SEARCH_DIR "/opt/intel/tbb")
    
    # TODO: Check to see which C++ library is being used by the compiler.
    if(NOT ${CMAKE_SYSTEM_VERSION} VERSION_LESS 13.0)
      # The default C++ library on OS X 10.9 and later is libc++
      set(TBB_LIB_PATH_SUFFIX "lib/libc++")
    endif()
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux
    set(TBB_DEFAULT_SEARCH_DIR "/opt/intel/tbb")
    
    # TODO: Check compiler version to see the suffix should be <arch>/gcc4.1 or
    #       <arch>/gcc4.1. For now, assume that the compiler is more recent than
    #       gcc 4.4.x or later.
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      set(TBB_LIB_PATH_SUFFIX "lib/intel64/gcc4.4")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
      set(TBB_LIB_PATH_SUFFIX "lib/ia32/gcc4.4")
    endif()
  endif()
  
  # The above TBB_LIB_PATH_SUFFIX/TBB_RUNTIME_PATH_SUFFIX is based on where Intel puts the libraries
  # in the package of prebuilt libraries it distributes. However, users may
  # install these shared libraries into the more conventional "lib"/"bin" directory
  # (especially when building from source), so we add that as an additional
  # location to search.
  list(APPEND TBB_LIB_PATH_SUFFIX "lib")
  list(APPEND TBB_RUNTIME_PATH_SUFFIX "bin")

  ##################################
  # Find the TBB include dir
  ##################################
  
  find_path(TBB_INCLUDE_DIRS tbb/tbb.h
      HINTS ${TBB_INCLUDE_DIR} ${TBB_SEARCH_DIR}
      PATHS ${TBB_DEFAULT_SEARCH_DIR}
      PATH_SUFFIXES include)

  ##################################
  # Set version strings
  ##################################

  if(TBB_INCLUDE_DIRS)
    # Use new oneTBB version header if it exists.
    if(EXISTS "${TBB_INCLUDE_DIRS}/oneapi/tbb/version.h")
      file(READ "${TBB_INCLUDE_DIRS}/oneapi/tbb/version.h" _tbb_version_file)
    else()
      file(READ "${TBB_INCLUDE_DIRS}/tbb/tbb_stddef.h" _tbb_version_file)
    endif()
    string(REGEX REPLACE ".*#define TBB_VERSION_MAJOR ([0-9]+).*" "\\1"
        TBB_VERSION_MAJOR "${_tbb_version_file}")
    string(REGEX REPLACE ".*#define TBB_VERSION_MINOR ([0-9]+).*" "\\1"
        TBB_VERSION_MINOR "${_tbb_version_file}")
    string(REGEX REPLACE ".*#define TBB_INTERFACE_VERSION ([0-9]+).*" "\\1"
        TBB_INTERFACE_VERSION "${_tbb_version_file}")
    set(TBB_VERSION "${TBB_VERSION_MAJOR}.${TBB_VERSION_MINOR}")
  endif()

  ##################################
  # Find TBB components
  ##################################

  # oneTBB on Windows has interface version in the name.
  if(WIN32 AND TBB_INTERFACE_VERSION GREATER_EQUAL 12000)
    set(_tbb_library_name tbb12)
  else()
    set(_tbb_library_name tbb)
  endif()

  if(TBB_VERSION VERSION_LESS 4.3)
    set(TBB_SEARCH_COMPONENTS tbb_preview tbbmalloc tbb)
  else()
    set(TBB_SEARCH_COMPONENTS tbb_preview tbbmalloc_proxy tbbmalloc tbb)
  endif()

  # Find each component
  foreach(_comp ${TBB_SEARCH_COMPONENTS})
    if(";${TBB_FIND_COMPONENTS};tbb" MATCHES ";${_comp};")

      if(${_comp} STREQUAL tbb)
        set(_lib_name ${_tbb_library_name})
      else()
        set(_lib_name ${_comp})
      endif()

      # Search for the libraries
      find_library(TBB_${_comp}_LIBRARY_RELEASE ${_lib_name}
          HINTS ${TBB_LIBRARY} ${TBB_SEARCH_DIR}
          PATHS ${TBB_DEFAULT_SEARCH_DIR} ENV LIBRARY_PATH
          PATH_SUFFIXES ${TBB_LIB_PATH_SUFFIX})

      find_library(TBB_${_comp}_LIBRARY_DEBUG ${_lib_name}_debug
          HINTS ${TBB_LIBRARY} ${TBB_SEARCH_DIR}
          PATHS ${TBB_DEFAULT_SEARCH_DIR} ENV LIBRARY_PATH
          PATH_SUFFIXES ${TBB_LIB_PATH_SUFFIX})

      # On Windows platforms also looks for .dll binaries.
      # If we find some, assume TBB is built as a shared library with the .lib/.dll pair,
      # otherwise assume it is built as a static library
      set(TBB_${_comp}_TARGET_TYPE SHARED)
      if(WIN32)
        find_file(TBB_${_comp}_SHARED_LIBRARY_RELEASE
            NAMES ${CMAKE_SHARED_LIBRARY_PREFIX}${_lib_name}${CMAKE_SHARED_LIBRARY_SUFFIX}
            HINTS ${TBB_LIBRARY} ${TBB_SEARCH_DIR}
            PATHS ${TBB_DEFAULT_SEARCH_DIR} ENV LIBRARY_PATH
            PATH_SUFFIXES ${TBB_RUNTIME_PATH_SUFFIX})
        
        find_file(TBB_${_comp}_SHARED_LIBRARY_DEBUG
            NAMES ${CMAKE_SHARED_LIBRARY_PREFIX}${_lib_name}_debug${CMAKE_SHARED_LIBRARY_SUFFIX}
            HINTS ${TBB_LIBRARY} ${TBB_SEARCH_DIR}
            PATHS ${TBB_DEFAULT_SEARCH_DIR} ENV LIBRARY_PATH
            PATH_SUFFIXES ${TBB_RUNTIME_PATH_SUFFIX})
      
        if(TBB_${_comp}_LIBRARY_RELEASE AND TBB_${_comp}_SHARED_LIBRARY_RELEASE)
          set(TBB_${_comp}_TARGET_TYPE SHARED)
        elseif(TBB_${_comp}_LIBRARY_DEBUG AND TBB_${_comp}_SHARED_LIBRARY_DEBUG)
          set(TBB_${_comp}_TARGET_TYPE SHARED)
        else()
          set(TBB_${_comp}_TARGET_TYPE STATIC)
        endif()
      endif()

      if(TBB_${_comp}_LIBRARY_DEBUG)
        list(APPEND TBB_LIBRARIES_DEBUG "${TBB_${_comp}_LIBRARY_DEBUG}")
      endif()
      if(TBB_${_comp}_LIBRARY_RELEASE)
        list(APPEND TBB_LIBRARIES_RELEASE "${TBB_${_comp}_LIBRARY_RELEASE}")
      endif()
      if(TBB_${_comp}_LIBRARY_${TBB_BUILD_TYPE} AND NOT TBB_${_comp}_LIBRARY)
        set(TBB_${_comp}_LIBRARY "${TBB_${_comp}_LIBRARY_${TBB_BUILD_TYPE}}")
      endif()

      if(TBB_${_comp}_LIBRARY AND EXISTS "${TBB_${_comp}_LIBRARY}")
        set(TBB_${_comp}_FOUND TRUE)
      else()
        set(TBB_${_comp}_FOUND FALSE)
      endif()

      # Mark internal variables as advanced
      mark_as_advanced(TBB_${_comp}_LIBRARY_RELEASE)
      mark_as_advanced(TBB_${_comp}_LIBRARY_DEBUG)
      mark_as_advanced(TBB_${_comp}_LIBRARY)
      mark_as_advanced(TBB_${_comp}_TARGET_TYPE)

    endif()
  endforeach()

  ##################################
  # Set compile flags and libraries
  ##################################

  set(TBB_DEFINITIONS_RELEASE "")
  set(TBB_DEFINITIONS_DEBUG "-DTBB_USE_DEBUG=1")
    
  if(TBB_LIBRARIES_${TBB_BUILD_TYPE})
    set(TBB_DEFINITIONS "${TBB_DEFINITIONS_${TBB_BUILD_TYPE}}")
    set(TBB_LIBRARIES "${TBB_LIBRARIES_${TBB_BUILD_TYPE}}")
  elseif(TBB_LIBRARIES_RELEASE)
    set(TBB_DEFINITIONS "${TBB_DEFINITIONS_RELEASE}")
    set(TBB_LIBRARIES "${TBB_LIBRARIES_RELEASE}")
  elseif(TBB_LIBRARIES_DEBUG)
    set(TBB_DEFINITIONS "${TBB_DEFINITIONS_DEBUG}")
    set(TBB_LIBRARIES "${TBB_LIBRARIES_DEBUG}")
  endif()

  find_package_handle_standard_args(TBB 
      REQUIRED_VARS TBB_INCLUDE_DIRS TBB_LIBRARIES
      HANDLE_COMPONENTS
      VERSION_VAR TBB_VERSION)

  ##################################
  # Create targets
  ##################################

  if(NOT CMAKE_VERSION VERSION_LESS 3.0 AND TBB_FOUND)
    foreach(_comp ${TBB_SEARCH_COMPONENTS})
      if(";${TBB_FIND_COMPONENTS};tbb" MATCHES ";${_comp};")

        add_library(TBB::${_comp} ${TBB_${_comp}_TARGET_TYPE} IMPORTED)
        set_property(TARGET TBB::${_comp} APPEND PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES ${TBB_INCLUDE_DIRS}
        )

        set(TBB_LINK_DIR_DEBUG)
        set(TBB_LINK_DIR_RELEASE)

        if(WIN32 AND TBB_${_comp}_SHARED_LIBRARY_${TBB_BUILD_TYPE})
          set_target_properties(TBB::${_comp} PROPERTIES
              IMPORTED_IMPLIB            ${TBB_${_comp}_LIBRARY_${TBB_BUILD_TYPE}}
              IMPORTED_LOCATION          ${TBB_${_comp}_SHARED_LIBRARY_${TBB_BUILD_TYPE}})
        else()
          set_target_properties(TBB::${_comp} PROPERTIES
              IMPORTED_LOCATION          ${TBB_${_comp}_LIBRARY_${TBB_BUILD_TYPE}})
        endif()

        if(TBB_${_comp}_LIBRARY_DEBUG)
          set_property(TARGET TBB::${_comp} APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
          set_property(TARGET TBB::${_comp} APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:TBB_USE_DEBUG=1>")
            cmake_path(GET TBB_${_comp}_LIBRARY_DEBUG PARENT_PATH TBB_LINK_DIR_DEBUG)

          if(WIN32 AND TBB_${_comp}_SHARED_LIBRARY_DEBUG)
            set_target_properties(TBB::${_comp} PROPERTIES
                IMPORTED_IMPLIB_DEBUG            ${TBB_${_comp}_LIBRARY_DEBUG}
                IMPORTED_IMPLIB_RELWITHDEBINFO   ${TBB_${_comp}_LIBRARY_DEBUG}

                IMPORTED_LOCATION_DEBUG          ${TBB_${_comp}_SHARED_LIBRARY_DEBUG}
                IMPORTED_LOCATION_RELWITHDEBINFO ${TBB_${_comp}_SHARED_LIBRARY_DEBUG})
          else()
            set_target_properties(TBB::${_comp} PROPERTIES
                IMPORTED_LOCATION_DEBUG          ${TBB_${_comp}_LIBRARY_DEBUG}
                IMPORTED_LOCATION_RELWITHDEBINFO ${TBB_${_comp}_LIBRARY_DEBUG})
          endif()
        endif()

        if(TBB_${_comp}_LIBRARY_RELEASE)
          set_property(TARGET TBB::${_comp} APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
            cmake_path(GET TBB_${_comp}_LIBRARY_RELEASE PARENT_PATH TBB_LINK_DIR_RELEASE)

          if(WIN32 AND TBB_${_comp}_SHARED_LIBRARY_RELEASE)
            set_target_properties(TBB::${_comp} PROPERTIES
                IMPORTED_IMPLIB_RELEASE      ${TBB_${_comp}_LIBRARY_RELEASE}
                IMPORTED_IMPLIB_MINSIZEREL   ${TBB_${_comp}_LIBRARY_RELEASE}

                IMPORTED_LOCATION_RELEASE    ${TBB_${_comp}_SHARED_LIBRARY_RELEASE}
                IMPORTED_LOCATION_MINSIZEREL ${TBB_${_comp}_SHARED_LIBRARY_RELEASE})
          else()
            set_target_properties(TBB::${_comp} PROPERTIES
                IMPORTED_LOCATION_RELEASE    ${TBB_${_comp}_LIBRARY_RELEASE}
                IMPORTED_LOCATION_MINSIZEREL ${TBB_${_comp}_LIBRARY_RELEASE})
          endif()
        endif()

      endif()

      # Set the linker directory path for the target.  This is specifically
      # used on linux so that the linker can find the appropriate 
      # numbered shared object
      if (TBB_LINK_DIR_DEBUG AND TBB_LINK_DIR_RELEASE)
        set(TBB_LINK_DIRS "$<$<CONFIG:Debug>:${TBB_LINK_DIR_DEBUG}>;$<$<CONFIG:Release>:${TBB_LINK_DIR_RELEASE}>")
      elseif(TBB_LINK_DIRS_DEBUG)
        set(TBB_LINK_DIRS ${TBB_LINK_DIR_DEBUG})
      elseif(TBB_LINK_DIR_RELEASE)
        set(TBB_LINK_DIRS ${TBB_LINK_DIR_RELEASE})
      endif()
      if (TBB_LINK_DIRS)
        set_property(TARGET TBB::${_comp} APPEND PROPERTY INTERFACE_LINK_DIRECTORIES ${TBB_LINK_DIRS})
      endif()

      unset(TBB_LINK_DIR_DEBUG)
      unset(TBB_LINK_DIR_RELEASE)
      unset(TBB_LINK_DIRS)
    endforeach()
  endif()

  mark_as_advanced(TBB_INCLUDE_DIRS TBB_LIBRARIES)

  unset(TBB_ARCHITECTURE)
  unset(TBB_BUILD_TYPE)
  unset(TBB_LIB_PATH_SUFFIX)
  unset(TBB_DEFAULT_SEARCH_DIR)
endif()