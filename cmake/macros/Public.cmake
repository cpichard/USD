#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
include(Private)

function(pxr_build_documentation)
    # Cmake booleans are often, ie, "OFF", while Doxyfile requires booleans
    # to be "YES" or "NO"
    if(PXR_BUILD_HTML_DOCUMENTATION)
        set(DOXYGEN_GENERATE_HTML "YES")
    else()
        set(DOXYGEN_GENERATE_HTML "NO")
    endif()

    configure_file(${PROJECT_SOURCE_DIR}/docs/doxygen/Doxyfile.in
                   ${PROJECT_BINARY_DIR}/Doxyfile)

    add_custom_target(
        documentation
        ALL
        # We need to manually copy pxr.h into the docs/include directory
        # since it's generated outside of the libraries.
        COMMAND
            ${CMAKE_COMMAND} -E copy
            "${PROJECT_BINARY_DIR}/include/pxr/pxr.h"
            "${PROJECT_BINARY_DIR}/docs/include/pxr/pxr.h"
        COMMAND 
            ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/docs"
            "${PROJECT_BINARY_DIR}/docs"
    )

    # Execute doxygen during the install step. All of the files we want
    # doxygen to process should already have been copied to the docs
    # directory during the build step
    install(CODE "execute_process(COMMAND ${DOXYGEN_EXECUTABLE} \"${PROJECT_BINARY_DIR}/Doxyfile\")")

    set(INST_DOCS_ROOT  "${CMAKE_INSTALL_PREFIX}/docs")

    set(BUILT_DOCS_TAG_FILE "${PROJECT_BINARY_DIR}/docs/USD.tag")
    install(
        FILES ${BUILT_DOCS_TAG_FILE}
        DESTINATION ${INST_DOCS_ROOT}
    )

    if(PXR_BUILD_HTML_DOCUMENTATION)
        set(BUILT_HTML_DOCS "${PROJECT_BINARY_DIR}/docs/doxy_html")
        install(
            DIRECTORY ${BUILT_HTML_DOCS}
            DESTINATION ${INST_DOCS_ROOT}
        )
    endif()

    set(BUILT_XML_DOCS "${PROJECT_BINARY_DIR}/docs/doxy_xml")
    install(
        DIRECTORY ${BUILT_XML_DOCS}
        DESTINATION ${INST_DOCS_ROOT}
    )
endfunction()

function(pxr_python_bin BIN_NAME)
    set(oneValueArgs
        PYTHON_FILE
    )
    set(multiValueArgs
        DEPENDENCIES
    )
    cmake_parse_arguments(pb
        ""
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # If we can't build Python modules then do nothing.
    if(NOT TARGET python_modules)
        message(STATUS "Skipping Python program ${BIN_NAME}, Python modules required")
        return()
    endif()

    _get_install_dir(bin installDir)

    # Source file.
    if( "${pb_PYTHON_FILE}" STREQUAL "")
        set(infile ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_NAME}.py)
    else()
        set(infile ${CMAKE_CURRENT_SOURCE_DIR}/${pb_PYTHON_FILE})
    endif()

    # Destination file.
    set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${BIN_NAME})

    # /pxrpythonsubst will be replaced with the full path to the configured
    # python executable. This doesn't use the CMake ${...} or @...@ syntax
    # for backwards compatibility with other build systems.
    add_custom_command(
        OUTPUT ${outfile}
        DEPENDS ${infile}
        COMMENT "Substituting Python shebang"
        COMMAND
            ${PYTHON_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/cmake/macros/shebang.py
            ${PXR_PYTHON_SHEBANG}
            ${infile}
            ${outfile}
    )
    list(APPEND outputs ${outfile})

    install(
        PROGRAMS ${outfile}
        DESTINATION ${installDir}
        RENAME ${BIN_NAME}
    )

    # Windows by default cannot execute Python files so here
    # we create a batch file wrapper that invokes the python
    # files.
    if(WIN32)
        add_custom_command(
            OUTPUT ${outfile}.cmd
            COMMENT "Creating Python cmd wrapper"
            COMMAND
                ${PYTHON_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/cmake/macros/shebang.py
                ${BIN_NAME}
                ${outfile}.cmd
        )
        list(APPEND outputs ${outfile}.cmd)

        install(
            PROGRAMS ${outfile}.cmd
            DESTINATION ${installDir}
            RENAME ${BIN_NAME}.cmd
        )
    endif()

    # Add the target.
    add_custom_target(${BIN_NAME}_script
        DEPENDS ${outputs} ${pb_DEPENDENCIES}
    )
    add_dependencies(python_modules ${BIN_NAME}_script)

    _get_folder("" folder)
    set_target_properties(${BIN_NAME}_script
        PROPERTIES
            FOLDER "${folder}"
    )
endfunction() # pxr_python_bin

function(pxr_cpp_bin BIN_NAME)
    _get_install_dir(bin installDir)
    
    set(multiValueArgs
        LIBRARIES
        INCLUDE_DIRS
    )

    cmake_parse_arguments(cb
        ""  
        ""
        "${multiValueArgs}"
        ${ARGN}
    )

    add_executable(${BIN_NAME} ${BIN_NAME}.cpp)

    # Turn PIC ON otherwise ArchGetAddressInfo() on Linux may yield
    # unexpected results.
    _get_folder("" folder)
    set_target_properties(${BIN_NAME}
        PROPERTIES
            FOLDER "${folder}"
            POSITION_INDEPENDENT_CODE ON
    )

    # Install and include headers from the build directory.
    get_filename_component(
        PRIVATE_INC_DIR
        "${PROJECT_BINARY_DIR}/include"
        ABSOLUTE
    )

    target_include_directories(${BIN_NAME}
        PRIVATE 
        ${PRIVATE_INC_DIR}
        ${cb_INCLUDE_DIRS}
    )

    _pxr_init_rpath(rpath "${installDir}")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${BIN_NAME})

    _pxr_target_link_libraries(${BIN_NAME}
        ${cb_LIBRARIES}
        ${PXR_MALLOC_LIBRARY}
    )

    install(
        TARGETS ${BIN_NAME}
        DESTINATION ${installDir}
    )
endfunction()

function(pxr_library NAME)
    set(options
        DISABLE_PRECOMPILED_HEADERS
        INCLUDE_SCHEMA_FILES
    )
    set(oneValueArgs
        TYPE
        PRECOMPILED_HEADER_NAME
    )
    set(multiValueArgs
        PUBLIC_CLASSES
        PUBLIC_HEADERS
        PRIVATE_CLASSES
        PRIVATE_HEADERS
        CPPFILES
        LIBRARIES
        INCLUDE_DIRS
        DOXYGEN_FILES
        RESOURCE_FILES
        PYTHON_PUBLIC_CLASSES
        PYTHON_PRIVATE_CLASSES
        PYTHON_PUBLIC_HEADERS
        PYTHON_PRIVATE_HEADERS
        PYTHON_CPPFILES
        PYMODULE_CPPFILES
        PYMODULE_FILES
        PYSIDE_UI_FILES
    )

    cmake_parse_arguments(args
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # If python support is enabled, merge the python specific categories
    # with the more general before setting up compilation.
    if(PXR_ENABLE_PYTHON_SUPPORT)
        set(libraryRequiresPython 0)
        if(args_PYTHON_PUBLIC_CLASSES)
            list(APPEND args_PUBLIC_CLASSES ${args_PYTHON_PUBLIC_CLASSES})
            set(libraryRequiresPython 1)
        endif()
        if(args_PYTHON_PUBLIC_HEADERS)
            list(APPEND args_PUBLIC_HEADERS ${args_PYTHON_PUBLIC_HEADERS})
            set(libraryRequiresPython 1)
        endif()
        if(args_PYTHON_PRIVATE_CLASSES)
            list(APPEND args_PRIVATE_CLASSES ${args_PYTHON_PRIVATE_CLASSES})
            set(libraryRequiresPython 1)
        endif()
        if(args_PYTHON_PRIVATE_HEADERS)
            list(APPEND args_PRIVATE_HEADERS ${args_PYTHON_PRIVATE_HEADERS})
            set(libraryRequiresPython 1)
        endif()
        if(args_PYTHON_CPPFILES)
            list(APPEND args_CPPFILES ${args_PYTHON_CPPFILES})
            set(libraryRequiresPython 1)
        endif()

        if(libraryRequiresPython)
            list(APPEND args_LIBRARIES ${PYTHON_LIBRARIES} python)
            list(APPEND args_INCLUDE_DIRS ${PYTHON_INCLUDE_DIRS})
        endif()
    endif()

    # If this is a schema library, add schema classes
    if (args_INCLUDE_SCHEMA_FILES)
        set(filePath "generatedSchema.classes.txt")

        # Register a dependency so that cmake will regenerate the build
        # system if generatedSchema.classes.txt changes
        set_property(
            DIRECTORY 
            APPEND 
            PROPERTY CMAKE_CONFIGURE_DEPENDS 
            ${filePath}
        )

        # Read the generated classes
        file(STRINGS ${filePath} fileContents)

        # fileType potential values:
        # -1: Skip line
        # 0: Public Classes
        # 1: Python Module Files
        # 2: Resource Files
        set(fileType -1)

        foreach(line ${fileContents})
            # Determine which section of the generated file we are in.
            if (${fileType} EQUAL -1)
                string(FIND ${line} "# Public Classes" found)
                if (NOT ${found} EQUAL -1)
                    set(fileType 0)
                    continue()
                endif()
            elseif(${fileType} EQUAL 0)
                string(FIND ${line} "# Python Module Files" found)
                if (NOT ${found} EQUAL -1)
                    set(fileType 1)
                    continue()
                endif()
            elseif(${fileType} EQUAL 1)
                string(FIND ${line} "# Resource Files" found)
                if (NOT ${found} EQUAL -1)
                    set(fileType 2)
                    continue()
                endif()
            endif()

            # Depending on the file type, append to the appropriate list.
            if (${fileType} EQUAL 0)
                list(APPEND args_PUBLIC_CLASSES ${line})
            elseif(${fileType} EQUAL 1)
                list(APPEND args_PYMODULE_CPPFILES ${line})
            elseif(${fileType} EQUAL 2)
                list(APPEND args_RESOURCE_FILES ${line})
            endif()
        endforeach()
    endif()

    # Collect libraries.
    if(NOT args_TYPE STREQUAL "PLUGIN")
        get_property(help CACHE PXR_ALL_LIBS PROPERTY HELPSTRING)
        list(APPEND PXR_ALL_LIBS ${NAME})
        set(PXR_ALL_LIBS "${PXR_ALL_LIBS}" CACHE INTERNAL "${help}")
        if(args_TYPE STREQUAL "STATIC")
            # Note if this library is explicitly STATIC.
            get_property(help CACHE PXR_STATIC_LIBS PROPERTY HELPSTRING)
            list(APPEND PXR_STATIC_LIBS ${NAME})
            set(PXR_STATIC_LIBS "${PXR_STATIC_LIBS}" CACHE INTERNAL "${help}")
        endif()
    endif()

    # Expand classes into filenames.
    _classes(${NAME} ${args_PRIVATE_CLASSES} PRIVATE)
    _classes(${NAME} ${args_PUBLIC_CLASSES} PUBLIC)

    # If building a core plugin for a monolithic build then treat it almost
    # like any other library and include it in the monolithic library.
    if (_building_core AND _building_monolithic AND args_TYPE STREQUAL "PLUGIN")
        set(args_TYPE "OBJECT_PLUGIN")
    endif()

    # Custom tweaks.
    if(args_TYPE STREQUAL "PLUGIN")
        # We can't build plugins if we're not building shared libraries.
        if(NOT TARGET shared_libs)
            message(STATUS "Skipping plugin ${NAME}, shared libraries required")
            return()
        endif()

        set(prefix "")
        set(suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
    else()
        # If the caller didn't specify the library type then choose the
        # type now.
        if("x${args_TYPE}" STREQUAL "x")
            if(_building_monolithic)
                set(args_TYPE "OBJECT")
            elseif(BUILD_SHARED_LIBS)
                set(args_TYPE "SHARED")
            else()
                set(args_TYPE "STATIC")
            endif()
        endif()

        _get_library_prefix(prefix)
        if(args_TYPE STREQUAL "STATIC")
            set(suffix ${CMAKE_STATIC_LIBRARY_SUFFIX})
        else()
            set(suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
        endif()
    endif()

    set(pch "ON")
    if(args_DISABLE_PRECOMPILED_HEADERS)
        set(pch "OFF")
    endif()

    if (PXR_ENABLE_PYTHON_SUPPORT AND args_PYMODULE_CPPFILES)
        # If moduleDeps.cpp does not exist, create one
        set(moduleDepsFileName "moduleDeps.cpp")
        list(FIND args_PYTHON_CPPFILES ${moduleDepsFileName} foundModuleDeps)
        if (${foundModuleDeps} EQUAL -1)
            # Add moduleDeps.cpp as a built file
            list(APPEND args_CPPFILES ${moduleDepsFileName})

            # Keep only our libraries in the module dependencies
            foreach(library ${args_LIBRARIES})
                if (TARGET ${library}) 
                    list(APPEND localLibs ${library})
                endif()
            endforeach()

            # Generate moduleDeps.cpp
            _get_python_module_name(${NAME} pyModuleName)
            add_custom_command(
                OUTPUT ${moduleDepsFileName}
                COMMAND ${CMAKE_COMMAND} -DlibraryName=${NAME} -DmoduleName=${pyModuleName} -DsourceDir=${PROJECT_SOURCE_DIR} -Dlibraries="${localLibs}" -Doutfile=${moduleDepsFileName} -P "${PROJECT_SOURCE_DIR}/cmake/macros/genModuleDepsCpp.cmake"
                DEPENDS "CMakeLists.txt")
        endif()
    endif()

    _pxr_library(${NAME}
        TYPE "${args_TYPE}"
        PREFIX "${prefix}"
        SUFFIX "${suffix}"
        SUBDIR "${subdir}"
        CPPFILES "${args_CPPFILES};${${NAME}_CPPFILES}"
        PUBLIC_HEADERS "${args_PUBLIC_HEADERS};${${NAME}_PUBLIC_HEADERS}"
        PRIVATE_HEADERS "${args_PRIVATE_HEADERS};${${NAME}_PRIVATE_HEADERS}"
        LIBRARIES "${args_LIBRARIES}"
        INCLUDE_DIRS "${args_INCLUDE_DIRS}"
        RESOURCE_FILES "${args_RESOURCE_FILES}"
        DOXYGEN_FILES "${args_DOXYGEN_FILES}"
        PRECOMPILED_HEADERS "${pch}"
        PRECOMPILED_HEADER_NAME "${args_PRECOMPILED_HEADER_NAME}"
        LIB_INSTALL_PREFIX_RESULT libInstallPrefix
    )

    if(PXR_ENABLE_PYTHON_SUPPORT AND (args_PYMODULE_CPPFILES OR args_PYMODULE_FILES OR args_PYSIDE_UI_FILES))
        list(APPEND pythonModuleIncludeDirs ${PYTHON_INCLUDE_DIRS})

        _pxr_python_module(
            ${NAME}
            WRAPPED_LIB_INSTALL_PREFIX "${libInstallPrefix}"
            PYTHON_FILES ${args_PYMODULE_FILES}
            PYSIDE_UI_FILES ${args_PYSIDE_UI_FILES}
            CPPFILES ${args_PYMODULE_CPPFILES}
            INCLUDE_DIRS "${args_INCLUDE_DIRS};${pythonModuleIncludeDirs}"
            PRECOMPILED_HEADERS ${pch}
            PRECOMPILED_HEADER_NAME ${args_PRECOMPILED_HEADER_NAME}
        )
    endif()
endfunction()

macro(pxr_shared_library NAME)
    pxr_library(${NAME} TYPE "SHARED" ${ARGN})
endmacro(pxr_shared_library)

macro(pxr_static_library NAME)
    pxr_library(${NAME} TYPE "STATIC" ${ARGN})
endmacro(pxr_static_library)

macro(pxr_plugin NAME)
    pxr_library(${NAME} TYPE "PLUGIN" ${ARGN})
endmacro(pxr_plugin)

function(pxr_setup_python)
    get_property(pxrPythonModules GLOBAL PROPERTY PXR_PYTHON_MODULES)

    # A new list where each python module is quoted
    set(converted "")
    foreach(module ${pxrPythonModules})
        list(APPEND converted "'${module}'")
    endforeach()

    # Join these with a ', '
    string(REPLACE ";" ", " pyModulesStr "${converted}")

    # Install a pxr __init__.py with an appropriate __all__
    _get_install_dir(lib/python/pxr installPrefix)

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated_modules_init.py"
         "__all__ = [${pyModulesStr}]\n")

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/generated_modules_init.py"
        DESTINATION ${installPrefix}
        RENAME "__init__.py"
    )
endfunction() # pxr_setup_python

function (pxr_create_test_module MODULE_NAME)
    # If we can't build Python modules then do nothing.
    if(NOT TARGET python_modules)
        return()
    endif()

    if (NOT PXR_BUILD_TESTS) 
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(tm "" "INSTALL_PREFIX;SOURCE_DIR;DEST_DIR" "" ${ARGN})

    if (NOT tm_SOURCE_DIR)
        set(tm_SOURCE_DIR testenv)
    endif()

    if (NOT tm_DEST_DIR)
        set(tm_DEST_DIR ${MODULE_NAME})
    endif()

    # Look specifically for an __init__.py and a plugInfo.json prefixed by the
    # module name. These will be installed without the module prefix.
    set(initPyFile ${tm_SOURCE_DIR}/${MODULE_NAME}__init__.py)
    set(plugInfoFile ${tm_SOURCE_DIR}/${MODULE_NAME}_plugInfo.json)

    # XXX -- We shouldn't have to install to run tests.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${initPyFile}")
        install(
            FILES 
                ${initPyFile}
            RENAME 
                __init__.py
            DESTINATION 
                tests/${tm_INSTALL_PREFIX}/lib/python/${tm_DEST_DIR}
        )
    endif()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${plugInfoFile}")
        install(
            FILES 
                ${plugInfoFile}
            RENAME 
                plugInfo.json
            DESTINATION 
                tests/${tm_INSTALL_PREFIX}/lib/python/${tm_DEST_DIR}
        )
    endif()
endfunction() # pxr_create_test_module

function(pxr_build_test_shared_lib LIBRARY_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        ""
        "INSTALL_PREFIX;SOURCE_DIR"
        "LIBRARIES;CPPFILES"
        ${ARGN}
    )
        
    add_library(${LIBRARY_NAME}
        SHARED
        ${bt_CPPFILES}
    )
    _pxr_target_link_libraries(${LIBRARY_NAME}
        ${bt_LIBRARIES}
    )
    _get_folder("tests/lib" folder)
    set_target_properties(${LIBRARY_NAME}
        PROPERTIES 
            FOLDER "${folder}"
    )

    # Find libraries under the install prefix, which has the core USD
    # libraries.
    _pxr_init_rpath(rpath "tests/lib")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${LIBRARY_NAME})

    if (NOT bt_SOURCE_DIR)
        set(bt_SOURCE_DIR testenv)
    endif()
    set(testPlugInfoSrcPath ${bt_SOURCE_DIR}/${LIBRARY_NAME}_plugInfo.json)

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${testPlugInfoSrcPath}")
        set(TEST_PLUG_INFO_RESOURCE_PATH "Resources")
        set(TEST_PLUG_INFO_ROOT "..")
        set(LIBRARY_FILE "${CMAKE_SHARED_LIBRARY_PREFIX}${LIBRARY_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")

        set(testPlugInfoLibDir "tests/${bt_INSTALL_PREFIX}/lib/${LIBRARY_NAME}")
        set(testPlugInfoResourceDir "${testPlugInfoLibDir}/${TEST_PLUG_INFO_RESOURCE_PATH}")
        set(testPlugInfoPath "${PROJECT_BINARY_DIR}/${testPlugInfoResourceDir}/plugInfo.json")

        file(RELATIVE_PATH 
            TEST_PLUG_INFO_LIBRARY_PATH
            "${CMAKE_INSTALL_PREFIX}/${testPlugInfoLibDir}"
            "${CMAKE_INSTALL_PREFIX}/tests/lib/${LIBRARY_FILE}")

        configure_file("${testPlugInfoSrcPath}" "${testPlugInfoPath}")
        # XXX -- We shouldn't have to install to run tests.
        install(
            FILES ${testPlugInfoPath}
            DESTINATION ${testPlugInfoResourceDir})
    endif()

    # We always want this test to build after the package it's under, even if
    # it doesn't link directly. This ensures that this test is able to include
    # headers from its parent package.
    add_dependencies(${LIBRARY_NAME} ${PXR_PACKAGE})

    # Test libraries can include the private headers of their parent PXR_PACKAGE
    # library
    target_include_directories(${LIBRARY_NAME}
        PRIVATE $<TARGET_PROPERTY:${PXR_PACKAGE},INCLUDE_DIRECTORIES>
    )

    # XXX -- We shouldn't have to install to run tests.
    install(
        TARGETS ${LIBRARY_NAME}
        LIBRARY DESTINATION "tests/lib"
        ARCHIVE DESTINATION "tests/lib"
        RUNTIME DESTINATION "tests/lib"
    )
endfunction() # pxr_build_test_shared_lib

function(pxr_build_test TEST_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        "" ""
        "LIBRARIES;CPPFILES"
        ${ARGN}
    )

    add_executable(${TEST_NAME}
        ${bt_CPPFILES}
    )

    # Turn PIC ON otherwise ArchGetAddressInfo() on Linux may yield
    # unexpected results.
    _get_folder("tests/bin" folder)
    set_target_properties(${TEST_NAME}
        PROPERTIES 
            FOLDER "${folder}"
        	POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(${TEST_NAME}
        PRIVATE $<TARGET_PROPERTY:${PXR_PACKAGE},INCLUDE_DIRECTORIES>
    )
    _pxr_target_link_libraries(${TEST_NAME}
        ${bt_LIBRARIES}
    )

    # Find libraries under the install prefix, which has the core USD
    # libraries.
    _pxr_init_rpath(rpath "tests")
    _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
    _pxr_install_rpath(rpath ${TEST_NAME})

    # XXX -- We shouldn't have to install to run tests.
    install(TARGETS ${TEST_NAME}
        RUNTIME DESTINATION "tests"
    )
endfunction() # pxr_build_test

function(pxr_test_scripts)
    # If we can't build Python modules then do nothing.
    if(NOT TARGET python_modules)
        return()
    endif()

    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    foreach(file ${ARGN})
        # Perform regex match to extract both source resource path and
        # destination resource path.
        # Regex match appropriately takes care of windows drive letter followed
        # by a ":", which is also the token we use to separate the source and
        # destination resource paths.
        string(REGEX MATCHALL "([A-Za-z]:)?([^:]+)" file "${file}")

        list(LENGTH file n)
        if (n EQUAL 1)
            get_filename_component(destFile ${file} NAME_WE)
        elseif (n EQUAL 2)
           list(GET file 1 destFile)
           list(GET file 0 file)
        else()
           message(FATAL_ERROR
               "Failed to parse test file path ${file}")
        endif()        

        # XXX -- We shouldn't have to install to run tests.
        install(
            PROGRAMS ${file}
            DESTINATION tests
            RENAME ${destFile}
        )
    endforeach()
endfunction() # pxr_test_scripts

function(pxr_install_test_dir)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    cmake_parse_arguments(bt
        "" 
        "SRC;DEST"
        ""
        ${ARGN}
    )

    # XXX -- We shouldn't have to install to run tests.
    install(
        DIRECTORY ${bt_SRC}/
        DESTINATION tests/ctest/${bt_DEST}
    )
endfunction() # pxr_install_test_dir

function(pxr_register_test TEST_NAME)
    if (NOT PXR_BUILD_TESTS)
        return()
    endif()

    # If the package for this test does not have a target it must not be
    # getting built, in which case we can skip building associated tests.
    if (NOT TARGET ${PXR_PACKAGE})
        return()
    endif()

    set(OPTIONS RUN_SERIAL PYTHON REQUIRES_SHARED_LIBS REQUIRES_PYTHON_MODULES PERCEPTUAL)
    set(ONE_VALUE_ARGS
            CUSTOM_PYTHON
            COMMAND
            STDOUT_REDIRECT STDERR_REDIRECT
            POST_COMMAND POST_COMMAND_STDOUT_REDIRECT POST_COMMAND_STDERR_REDIRECT
            PRE_COMMAND PRE_COMMAND_STDOUT_REDIRECT PRE_COMMAND_STDERR_REDIRECT
            FILES_EXIST FILES_DONT_EXIST
            CLEAN_OUTPUT
            EXPECTED_RETURN_CODE
            TESTENV
            WARN WARN_PERCENT HARD_WARN FAIL FAIL_PERCENT HARD_FAIL)
    set(MULTI_VALUE_ARGS DIFF_COMPARE IMAGE_DIFF_COMPARE ENV PRE_PATH POST_PATH)

    cmake_parse_arguments(bt
        "${OPTIONS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}"
        ${ARGN}
    )

    # Discard tests that required shared libraries.
    if(NOT TARGET shared_libs)
        # Explicit requirement.  This is for C++ tests that dynamically
        # load libraries linked against USD code.  These tests will have
        # multiple copies of symbols and will likely re-execute
        # ARCH_CONSTRUCTOR and registration functions, which will almost
        # certainly cause problems.
        if(bt_REQUIRES_SHARED_LIBS)
            message(STATUS "Skipping test ${TEST_NAME}, shared libraries required")
            return()
        endif()
    endif()

    if(NOT TARGET python_modules)
        # Implicit requirement.  Python modules require shared USD
        # libraries.  If the test runs python it's certainly going
        # to load USD modules.  If the test uses C++ to load USD
        # modules it tells us via REQUIRES_PYTHON_MODULES.
        if(bt_PYTHON OR bt_CUSTOM_PYTHON OR bt_REQUIRES_PYTHON_MODULES)
            message(STATUS "Skipping test ${TEST_NAME}, Python modules required")
            return()
        endif()
    endif()

    # This harness is a filter which allows us to manipulate the test run, 
    # e.g. by changing the environment, changing the expected return code, etc.
    set(testWrapperCmd ${PROJECT_SOURCE_DIR}/cmake/macros/testWrapper.py --verbose)

    if (bt_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --stdout-redirect=${bt_STDOUT_REDIRECT})
    endif()

    if (bt_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --stderr-redirect=${bt_STDERR_REDIRECT})
    endif()

    if (bt_PRE_COMMAND_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --pre-command-stdout-redirect=${bt_PRE_COMMAND_STDOUT_REDIRECT})
    endif()

    if (bt_PRE_COMMAND_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --pre-command-stderr-redirect=${bt_PRE_COMMAND_STDERR_REDIRECT})
    endif()

    if (bt_POST_COMMAND_STDOUT_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --post-command-stdout-redirect=${bt_POST_COMMAND_STDOUT_REDIRECT})
    endif()

    if (bt_POST_COMMAND_STDERR_REDIRECT)
        set(testWrapperCmd ${testWrapperCmd} --post-command-stderr-redirect=${bt_POST_COMMAND_STDERR_REDIRECT})
    endif()

    # Not all tests will have testenvs, but if they do let the wrapper know so
    # it can copy the testenv contents into the run directory. By default,
    # assume the testenv has the same name as the test but allow it to be
    # overridden by specifying TESTENV.
    if (bt_TESTENV)
        set(testenvDir ${CMAKE_INSTALL_PREFIX}/tests/ctest/${bt_TESTENV})
    else()
        set(testenvDir ${CMAKE_INSTALL_PREFIX}/tests/ctest/${TEST_NAME})
    endif()

    set(testWrapperCmd ${testWrapperCmd} --testenv-dir=${testenvDir})

    if (bt_DIFF_COMPARE)
        foreach(compareFile ${bt_DIFF_COMPARE})
            set(testWrapperCmd ${testWrapperCmd} --diff-compare=${compareFile})
        endforeach()
    endif()

    if (bt_IMAGE_DIFF_COMPARE)
        if (IMAGE_DIFF_TOOL)
            foreach (compareFile ${bt_IMAGE_DIFF_COMPARE})
                set(testWrapperCmd ${testWrapperCmd} --image-diff-compare=${compareFile})
            endforeach ()

            if (bt_WARN)
                set(testWrapperCmd ${testWrapperCmd} --warn=${bt_WARN})
            endif()

            if (bt_WARN_PERCENT)
                set(testWrapperCmd ${testWrapperCmd} --warnpercent=${bt_WARN_PERCENT})
            endif()

            if (bt_HARD_WARN)
                set(testWrapperCmd ${testWrapperCmd} --hardwarn=${bt_HARD_WARN})
            endif()

            if (bt_FAIL)
                set(testWrapperCmd ${testWrapperCmd} --fail=${bt_FAIL})
            endif()

            if (bt_FAIL_PERCENT)
                set(testWrapperCmd ${testWrapperCmd} --failpercent=${bt_FAIL_PERCENT})
            endif()

            if (bt_HARD_FAIL)
                set(testWrapperCmd ${testWrapperCmd} --hardfail=${bt_HARD_FAIL})
            endif()

            if(bt_PERCEPTUAL)
                set(testWrapperCmd ${testWrapperCmd} --perceptual)
            endif()

            # Make sure to add the image diff tool to the PATH so
            # it can be easily found within the testWrapper
            get_filename_component(IMAGE_DIFF_TOOL_PATH ${IMAGE_DIFF_TOOL} DIRECTORY)
            set(testWrapperCmd ${testWrapperCmd} --post-path=${IMAGE_DIFF_TOOL_PATH})
        endif()
    endif()

    if (bt_DIFF_COMPARE OR bt_IMAGE_DIFF_COMPARE)
        # Common settings we only want to set once if either is used

        # For now the baseline directory is assumed by convention from the test
        # name. There may eventually be cases where we'd want to specify it by
        # an argument though.
        set(baselineDir ${testenvDir}/baseline)
        set(testWrapperCmd ${testWrapperCmd} --baseline-dir=${baselineDir})

        # <PXR_CTEST_RUN_ID> will be set by CTestCustom.cmake, and then
        # expanded by testWrapper.py
        set(failuresDir ${PROJECT_BINARY_DIR}/Testing/Failed-Diffs/<PXR_CTEST_RUN_ID>/${TEST_NAME})
        set(testWrapperCmd ${testWrapperCmd} --failures-dir=${failuresDir})
    endif()

    if (bt_CLEAN_OUTPUT)
        set(testWrapperCmd ${testWrapperCmd} --clean-output-paths=${bt_CLEAN_OUTPUT})
    endif()

    if (bt_FILES_EXIST)
        set(testWrapperCmd ${testWrapperCmd} --files-exist=${bt_FILES_EXIST})
    endif()

    if (bt_FILES_DONT_EXIST)
        set(testWrapperCmd ${testWrapperCmd} --files-dont-exist=${bt_FILES_DONT_EXIST})
    endif()

    if (bt_PRE_COMMAND)
        set(testWrapperCmd ${testWrapperCmd} --pre-command=${bt_PRE_COMMAND})
    endif()

    if (bt_POST_COMMAND)
        set(testWrapperCmd ${testWrapperCmd} --post-command=${bt_POST_COMMAND})
    endif()

    if (bt_EXPECTED_RETURN_CODE)
        set(testWrapperCmd ${testWrapperCmd} --expected-return-code=${bt_EXPECTED_RETURN_CODE})
    endif()

    # Ensure that TF_FATAL_VERIFY is enabled for tests, so that failed verifies
    # turn into test failures.
    # Set this first, so that env vars passed to pxr_register_test can turn off
    # TF_FATAL_VERIFY where desired.
    set(testWrapperCmd ${testWrapperCmd} --env-var=TF_FATAL_VERIFY=1)

    if (bt_ENV)
        foreach(env ${bt_ENV})
            set(testWrapperCmd ${testWrapperCmd} --env-var=${env})
        endforeach()
    endif()

    if (bt_PRE_PATH)
        foreach(path ${bt_PRE_PATH})
            set(testWrapperCmd ${testWrapperCmd} --pre-path=${path})
        endforeach()
    endif()

    if (bt_POST_PATH)
        foreach(path ${bt_POST_PATH})
            set(testWrapperCmd ${testWrapperCmd} --post-path=${path})
        endforeach()
    endif()
        
    # If we're building static libraries, the C++ tests that link against
    # these libraries will look for resource files in the "usd" subdirectory
    # relative to where the tests are installed. However, the build installs
    # these files in the "lib" directory where the libraries are installed. 
    #
    # We don't want to copy these resource files for each test, so instead
    # we set the PXR_PLUGINPATH_NAME env var to point to the "lib/usd"
    # directory where these files are installed.
    if (NOT TARGET shared_libs)
        set(testWrapperCmd ${testWrapperCmd} --env-var=${PXR_PLUGINPATH_NAME}=${CMAKE_INSTALL_PREFIX}/lib/usd)
    endif()

    if (PXR_TEST_RUN_TEMP_DIR_PREFIX)
          set(testWrapperCmd ${testWrapperCmd} --tempdirprefix=${PXR_TEST_RUN_TEMP_DIR_PREFIX})
    endif()

    # Ensure that Python imports the Python files built by this build.
    # On Windows convert backslash to slash and don't change semicolons
    # to colons.
    set(_testPythonPath "${CMAKE_INSTALL_PREFIX}/lib/python;$ENV{PYTHONPATH}")
    if(WIN32)
        string(REGEX REPLACE "\\\\" "/" _testPythonPath "${_testPythonPath}")
    else()
        string(REPLACE ";" ":" _testPythonPath "${_testPythonPath}")
    endif()

    # Ensure we run with the appropriate python executable.
    if (bt_CUSTOM_PYTHON)
        set(testCmd "${bt_CUSTOM_PYTHON} ${bt_COMMAND}")
    elseif (bt_PYTHON)
        set(testCmd "${PYTHON_EXECUTABLE} ${bt_COMMAND}")
    else()
        set(testCmd "${bt_COMMAND}")
    endif()

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${PYTHON_EXECUTABLE} ${testWrapperCmd}
                "--env-var=PYTHONPATH=${_testPythonPath}" ${testCmd}
    )

    # But in some cases, we need to pass cmake properties directly to cmake
    # run_test, rather than configuring the environment
    if (bt_RUN_SERIAL)
        set_tests_properties(${TEST_NAME} PROPERTIES RUN_SERIAL TRUE)
    endif()

endfunction() # pxr_register_test

function(pxr_setup_plugins)
    # Install a top-level plugInfo.json in the shared area and into the 
    # top-level plugin area
    _get_resources_dir_name(resourcesDir)

    # Add extra plugInfo.json include paths to the top-level plugInfo.json,
    # relative to that top-level file.
    set(extraIncludes "")
    list(REMOVE_DUPLICATES PXR_EXTRA_PLUGINS)
    foreach(dirName ${PXR_EXTRA_PLUGINS})
        file(RELATIVE_PATH
            relDirName
            "${CMAKE_INSTALL_PREFIX}/lib/usd"
            "${CMAKE_INSTALL_PREFIX}/${dirName}"
        )
        set(extraIncludes "${extraIncludes},\n        \"${relDirName}/\"")
    endforeach()

    set(plugInfoContents "{\n    \"Includes\": [\n        \"*/${resourcesDir}/\"${extraIncludes}\n    ]\n}\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugins_plugInfo.json"
         "${plugInfoContents}")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/plugins_plugInfo.json"
        DESTINATION lib/usd
        RENAME "plugInfo.json"
    )

    set(plugInfoContents "{\n    \"Includes\": [ \"*/${resourcesDir}/\" ]\n}\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/usd_plugInfo.json"
         "${plugInfoContents}")
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/usd_plugInfo.json"
        DESTINATION plugin/usd
        RENAME "plugInfo.json"
    )
endfunction() # pxr_setup_plugins

function(pxr_add_extra_plugins PLUGIN_AREAS)
    # Install a top-level plugInfo.json in the given plugin areas.
    _get_resources_dir_name(resourcesDir)
    set(plugInfoContents "{\n    \"Includes\": [ \"*/${resourcesDir}/\" ]\n}\n")

    get_property(help CACHE PXR_EXTRA_PLUGINS PROPERTY HELPSTRING)

    foreach(area ${PLUGIN_AREAS})
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${area}_plugInfo.json"
            "${plugInfoContents}")
        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/${area}_plugInfo.json"
            DESTINATION "${PXR_INSTALL_SUBDIR}/${area}"
            RENAME "plugInfo.json"
        )
        list(APPEND PXR_EXTRA_PLUGINS "${PXR_INSTALL_SUBDIR}/${area}")
    endforeach()

    set(PXR_EXTRA_PLUGINS "${PXR_EXTRA_PLUGINS}" CACHE INTERNAL "${help}")
endfunction() # pxr_setup_third_plugins

function(pxr_toplevel_prologue)
    # Generate a namespace declaration header, pxr.h, at the top level of
    # pxr at configuration time.
    configure_file(${PROJECT_SOURCE_DIR}/pxr/pxr.h.in
        ${PROJECT_BINARY_DIR}/include/pxr/pxr.h     
    )  
    install(
        FILES ${PROJECT_BINARY_DIR}/include/pxr/pxr.h
        DESTINATION include/pxr
    )

    # Create a monolithic shared library target if we should import one
    # or create one.
    if(PXR_BUILD_MONOLITHIC)
        if(PXR_MONOLITHIC_IMPORT)
            # Gather the export information for usd_ms.
            include("${PXR_MONOLITHIC_IMPORT}" OPTIONAL RESULT_VARIABLE found)

            # If the import wasn't found then create it and import it.
            # This ensures that the build files will be regenerated if
            # the file's contents change.  If this isn't desired or
            # write permissions aren't granted the client can configure
            # first without PXR_MONOLITHIC_IMPORT, build the 'monolithic'
            # target, build their own shared library and export file,
            # then configure again with PXR_MONOLITHIC_IMPORT.
            if(found STREQUAL "NOTFOUND")
                file(WRITE "${PXR_MONOLITHIC_IMPORT}" "")
                include("${PXR_MONOLITHIC_IMPORT}")
            endif()

            # If there's an IMPORTED_LOCATION then its parent must be
            # the install directory ${CMAKE_INSTALL_PREFIX}.  If it
            # isn't then we won't be able to find plugInfo.json files
            # at runtime because they're found relative to the library
            # that contains pxr/base/lib/plug/initConfig.cpp.  The
            # exception is if ${PXR_INSTALL_LOCATION} is set;  in that
            # case we assume the files will be found there regardless
            # of IMPORTED_LOCATION.  Note, however, that the install
            # cannot be relocated in this case.
            if(NOT PXR_INSTALL_LOCATION AND TARGET usd_ms)
                get_property(location TARGET usd_ms PROPERTY IMPORTED_LOCATION)
                if(location)
                    # Remove filename and directory.
                    get_filename_component(parent "${location}" PATH)
                    get_filename_component(parent "${parent}" PATH)
                    get_filename_component(parent "${parent}" ABSOLUTE)
                    get_filename_component(prefix "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
                    if(NOT "${parent}" STREQUAL "${prefix}")
                        message("IMPORTED_LOCATION for usd_ms ${location} inconsistent with install directory ${CMAKE_INSTALL_PREFIX}.")
                        message(WARNING "May not find plugins at runtime.")
                    endif()
                endif()
            endif()
        else()
            # Note that we ignore BUILD_SHARED_LIBS when building monolithic
            # when PXR_MONOLITHIC_IMPORT isn't set:  we always build an
            # archive library from the core libraries and then build a
            # shared library from that.  BUILD_SHARED_LIBS is still used
            # for libraries outside of the core.

            # We need at least one source file for the library so we
            # create an empty one.
            add_custom_command(
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp"
                COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp"
            )

            # Our shared library.
            add_library(usd_ms SHARED "${CMAKE_CURRENT_BINARY_DIR}/usd_ms.cpp")
            _get_folder("" folder)
            _get_library_prefix(libPrefix)
            set_target_properties(usd_ms
                PROPERTIES
                    FOLDER "${folder}"
                    PREFIX "${libPrefix}"
                    IMPORT_PREFIX "${libPrefix}"
            )
            _get_install_dir("lib" libInstallPrefix)
            install(
                TARGETS usd_ms
                EXPORT pxrTargets
                LIBRARY DESTINATION ${libInstallPrefix}
                ARCHIVE DESTINATION ${libInstallPrefix}
                RUNTIME DESTINATION ${libInstallPrefix}
            )
            if(WIN32)
                install(
                    FILES $<TARGET_PDB_FILE:usd_ms>
                    DESTINATION ${libInstallPrefix}
                    OPTIONAL
                )
            endif()
        endif()
    endif()

    # Create a target for shared libraries.  We currently use this only
    # to test its existence.
    if(BUILD_SHARED_LIBS OR TARGET usd_ms)
        add_custom_target(shared_libs)
    endif()

    # Create a target for targets that require Python.  Each should add
    # itself as a dependency to the "python_modules" target.
    if(TARGET shared_libs AND PXR_ENABLE_PYTHON_SUPPORT)
        add_custom_target(python_modules ALL)
    endif()
endfunction() # pxr_toplevel_prologue

function(pxr_toplevel_epilogue)
    # If we're building a shared monolithic library then link it against
    # usd_m.
    if(TARGET usd_ms AND NOT PXR_MONOLITHIC_IMPORT)
        # We need to use whole-archive to get all the symbols.  Also note
        # that we carefully avoid adding the usd_m target itself by using
        # TARGET_FILE.  Linking the usd_m target would link usd_m and
        # everything it links to.
        
        if(MSVC)
            target_link_libraries(usd_ms
                PRIVATE
                    -WHOLEARCHIVE:$<BUILD_INTERFACE:$<TARGET_FILE:usd_m>>
            )
        elseif(CMAKE_COMPILER_IS_GNUCXX)
            target_link_libraries(usd_ms
                PRIVATE
                    -Wl,--whole-archive $<BUILD_INTERFACE:$<TARGET_FILE:usd_m>> -Wl,--no-whole-archive
            )
        elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
            target_link_libraries(usd_ms
                PRIVATE
                    -Wl,-force_load $<BUILD_INTERFACE:$<TARGET_FILE:usd_m>>
            )
        endif()
        if(APPLE AND PXR_PY_UNDEFINED_DYNAMIC_LOOKUP)
            # When not explicitly linking to the python lib we need to allow
            # the linker to complete without resolving all symbols. This lets
            # python resolve at runtime, and use this to support python
            # versions built with different compilers and point versions.
            # This only needed on macOS; this is not an issue on Windows,
            # and on Linux the equivalent --allow-shlib-undefined option for ld
            # is enabled by default when creating shared libraries.
            target_link_options(usd_ms
                PUBLIC
                "LINKER:SHELL:-undefined dynamic_lookup"
            )
        endif()

        # Since we didn't add a dependency to usd_ms on usd_m above, we
        # manually add it here along with compile definitions, include
        # directories, etc
        add_dependencies(usd_ms usd_m)

        # Add the stuff we didn't get because we didn't link against the
        # usd_m target.
        target_compile_definitions(usd_ms
            PUBLIC
                $<BUILD_INTERFACE:$<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>>
        )
        target_include_directories(usd_ms
            PUBLIC
                $<BUILD_INTERFACE:$<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>>
        )
        target_include_directories(usd_ms
            SYSTEM
            PUBLIC
                $<BUILD_INTERFACE:$<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>>
        )
        foreach(lib ${PXR_OBJECT_LIBS})
            get_property(libs TARGET ${lib} PROPERTY INTERFACE_LINK_LIBRARIES)
            target_link_libraries(usd_ms
                PUBLIC
                    ${libs}
            )
        endforeach()
        target_link_libraries(usd_ms
            PUBLIC
                ${PXR_MALLOC_LIBRARY}
                ${PXR_THREAD_LIBS}
        )

        _pxr_init_rpath(rpath "${libInstallPrefix}")
        _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/${PXR_INSTALL_SUBDIR}/lib")
        _pxr_add_rpath(rpath "${CMAKE_INSTALL_PREFIX}/lib")
        _pxr_install_rpath(rpath usd_ms)
    endif()

    # Setup the plugins in the top epilogue to ensure that everybody has had a
    # chance to update PXR_EXTRA_PLUGINS with their plugin paths.
    pxr_setup_plugins()
endfunction() # pxr_toplevel_epilogue

function(pxr_monolithic_epilogue)
    # When building a monolithic library we want all API functions to be
    # exported.  So add FOO_EXPORTS=1 for every library in PXR_OBJECT_LIBS,
    # where FOO is the uppercase version of the library name, to every
    # library in PXR_OBJECT_LIBS.
    set(exports "")
    foreach(lib ${PXR_OBJECT_LIBS})
        string(TOUPPER ${lib} uppercaseName)
        list(APPEND exports "${uppercaseName}_EXPORTS=1")
    endforeach()
    foreach(lib ${PXR_OBJECT_LIBS})
        set(objects "${objects};\$<TARGET_OBJECTS:${lib}>")
        target_compile_definitions(${lib} PRIVATE ${exports})
    endforeach()

    # Collect all of the objects for all of the core libraries to add to
    # the monolithic library.
    set(objects "")
    foreach(lib ${PXR_OBJECT_LIBS})
        set(objects "${objects};\$<TARGET_OBJECTS:${lib}>")
    endforeach()

    # Add the monolithic library.  This has to be delayed until now
    # because $<TARGET_OBJECTS> isn't a real generator expression
    # in that it can only appear in the sources of add_library() or
    # add_executable();  it can't appear in target_sources().  We
    # need at least one source file so we create an empty one
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp"
    )
    add_library(usd_m STATIC "${CMAKE_CURRENT_BINARY_DIR}/usd_m.cpp" ${objects})

    _get_folder("" folder)
    _get_library_prefix(libPrefix)
    set_target_properties(usd_m
        PROPERTIES
            FOLDER "${folder}"
            POSITION_INDEPENDENT_CODE ON
            PREFIX "${libPrefix}"
            IMPORT_PREFIX "${libPrefix}"
    )

    # Adding $<TARGET_OBJECTS:foo> will not bring along compile
    # definitions, include directories, etc.  Since we'll want those
    # attached to usd_m we explicitly add them.
    foreach(lib ${PXR_OBJECT_LIBS})
        target_compile_definitions(usd_m
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_COMPILE_DEFINITIONS>
        )
        target_include_directories(usd_m
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_include_directories(usd_m
            SYSTEM
            PUBLIC
                $<TARGET_PROPERTY:${lib},INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>
        )

        get_property(libs TARGET ${lib} PROPERTY INTERFACE_LINK_LIBRARIES)
        target_link_libraries(usd_m
            PUBLIC
                ${libs}
        )
    endforeach()

    # Manual export targets.  We can't use install(EXPORT) because usd_m
    # depends on OBJECT libraries which cannot be exported yet must be
    # in order to export usd_m.  We also have boilerplate for usd_ms, the
    # externally built monolithic shared library containing usd_m.  The
    # client should replace the FIXMEs with the appropriate paths or
    # use the usd_m export to build against and generate a usd_ms export.
    set(export "")
    set(export "${export}add_library(usd_m STATIC IMPORTED)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY IMPORTED_LOCATION $<TARGET_FILE:usd_m>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_COMPILE_DEFINITIONS $<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_m PROPERTY INTERFACE_LINK_LIBRARIES $<TARGET_PROPERTY:usd_m,INTERFACE_LINK_LIBRARIES>)\n")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
        CONTENT "${export}"
    )
    set(export "")
    set(export "${export}# Boilerplate for export of usd_ms.  Replace FIXMEs with appropriate paths\n")
    set(export "${export}# or include usd-targets-$<CONFIG>.cmake in your own build and generate your\n")
    set(export "${export}# own export file.  Configure with PXR_MONOLITHIC_IMPORT set to the path of\n")
    set(export "${export}# the export file.\n")
    set(export "${export}add_library(usd_ms SHARED IMPORTED)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY IMPORTED_LOCATION FIXME)\n")
    set(export "${export}#set_property(TARGET usd_ms PROPERTY IMPORTED_IMPLIB FIXME)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_COMPILE_DEFINITIONS $<TARGET_PROPERTY:usd_m,INTERFACE_COMPILE_DEFINITIONS>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:usd_m,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>)\n")
    set(export "${export}set_property(TARGET usd_ms PROPERTY INTERFACE_LINK_LIBRARIES $<TARGET_PROPERTY:usd_m,INTERFACE_LINK_LIBRARIES>)\n")
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
        CONTENT "${export}"
    )

    # Convenient name for building the monolithic library.
    add_custom_target(monolithic
        DEPENDS
            usd_m
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
            "${PROJECT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
            "${PROJECT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake"
        COMMAND ${CMAKE_COMMAND} -E echo Export file: ${PROJECT_BINARY_DIR}/usd-targets-$<CONFIG>.cmake
        COMMAND ${CMAKE_COMMAND} -E echo Import file: ${PROJECT_BINARY_DIR}/usd-imports-$<CONFIG>.cmake
    )
endfunction() # pxr_monolithic_epilogue

function(pxr_core_prologue)
    set(_building_core TRUE PARENT_SCOPE)
    if(PXR_BUILD_MONOLITHIC)
        set(_building_monolithic TRUE PARENT_SCOPE)
    endif()
endfunction() # pxr_core_prologue

function(pxr_core_epilogue)
    if(_building_core)
        if(_building_monolithic)
            pxr_monolithic_epilogue()
            set(_building_monolithic FALSE PARENT_SCOPE)
        endif()
        if(PXR_ENABLE_PYTHON_SUPPORT)
            pxr_setup_python()
        endif()
        set(_building_core FALSE PARENT_SCOPE)
    endif()
endfunction() # pxr_core_epilogue

function(pxr_tests_prologue)
    add_custom_target(
        test_setup
        ALL
        DEPENDS "${PROJECT_BINARY_DIR}/CTestCustom.cmake"
    )
    add_custom_command(
        OUTPUT "${PROJECT_BINARY_DIR}/CTestCustom.cmake"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/defaults/CTestCustom.cmake"
            "${PROJECT_BINARY_DIR}/CTestCustom.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/defaults/CTestCustom.cmake"
        COMMENT "Copying CTestCustom.cmake"
    )
endfunction() # pxr_tests_prologue

function(pxr_build_python_documentation)
    set(BUILT_XML_DOCS "${PROJECT_BINARY_DIR}/docs/doxy_xml")
    set(CONVERT_DOXYGEN_TO_PYTHON_DOCS_SCRIPT 
       "${PROJECT_SOURCE_DIR}/docs/python/convertDoxygen.py")
    set(INSTALL_PYTHON_PXR_ROOT "${CMAKE_INSTALL_PREFIX}/lib/python/pxr")

    # Get the list of pxr python modules and run a install command for each
    get_property(pxrPythonModules GLOBAL PROPERTY PXR_PYTHON_MODULES)
    # Create string of module names, joined with ","
    string(REPLACE ";" "," pxrPythonModulesStr "${pxrPythonModules}")
    # Run convertDoxygen on the module list, setting PYTHONPATH 
    # to the install path for the USD Python modules
    if (WIN32)
        set(DLL_PATH_FLAG "--dllPath \"${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/bin;${CMAKE_INSTALL_PREFIX}/plugin/usd;${CMAKE_INSTALL_PREFIX}/share/usd/examples/plugin\"")
    else()
        set(DLL_PATH_FLAG "")
    endif()
    install(CODE "\
        execute_process(\
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/cmake \
            RESULT_VARIABLE convert_doxygen_return_code
            COMMAND ${PYTHON_EXECUTABLE} ${CONVERT_DOXYGEN_TO_PYTHON_DOCS_SCRIPT} \
                --package pxr --module ${pxrPythonModulesStr} \
                --inputIndex ${BUILT_XML_DOCS}/index.xml \
                --pythonPath ${CMAKE_INSTALL_PREFIX}/lib/python \
                ${DLL_PATH_FLAG} \
                --output ${INSTALL_PYTHON_PXR_ROOT})
        if (NOT \${convert_doxygen_return_code} EQUAL \"0\")
            message( FATAL_ERROR \"Error generating python docstrings - ${CONVERT_DOXYGEN_TO_PYTHON_DOCS_SCRIPT} return code: \${convert_doxygen_return_code} \")
        endif()
    ")

endfunction() # pxr_build_python_documentation

# Adding support for a "docs-only" directory, needed when adding doxygen docs
# not associated with a specific library/etc. 
function(pxr_docs_only_dir NAME)
    # Get list of doxygen files, which could include image files and/or 
    # snippets example cpp files 
    set(multiValueArgs
        DOXYGEN_FILES
    )
    cmake_parse_arguments(args
        ""
        ""
        "${multiValueArgs}"
        ${ARGN}
    )
    if(PXR_BUILD_DOCUMENTATION)
        _copy_doxygen_files(${NAME}
            IS_LIB
                FALSE
            HEADER_INSTALL_PREFIX
                "include/${PXR_PREFIX}"
            DOXYGEN_FILES
                ${args_DOXYGEN_FILES}
        )
    endif()
endfunction() # pxr_docs_only_dir

# Sets rpaths for the specified TARGET to the given RPATHS. The target's
# runtime destination directory is given by ORIGIN. If ORIGIN is not
# absolute it is assumed to be relative to CMAKE_INSTALL_PREFIX.
function(pxr_set_rpaths_for_target TARGET)
    set(oneValueArgs ORIGIN)
    set(multiValueArgs RPATHS)
    cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    _pxr_init_rpath(rpath ${args_ORIGIN})

    foreach(path IN LISTS args_RPATHS)
        _pxr_add_rpath(rpath ${path})
    endforeach()

    _pxr_install_rpath(rpath ${TARGET})

endfunction() # pxr_set_rpaths_for_target
