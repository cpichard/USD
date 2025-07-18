set(PXR_PREFIX pxr/base)
set(PXR_PACKAGE plug)

pxr_library(plug
    LIBRARIES
        arch
        tf
        js
        trace
        work
        TBB::tbb

    PUBLIC_CLASSES
        interfaceFactory
        notice
        plugin
        registry
        staticInterface

    PRIVATE_CLASSES
        debugCodes
        info
        testPlugBase

    PUBLIC_HEADERS
        api.h
        thisPlugin.h

    CPPFILES
        initConfig.cpp

    PYMODULE_CPPFILES
        module.cpp
        wrapNotice.cpp
        wrapPlugin.cpp
        wrapRegistry.cpp
        wrapTestPlugBase.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.dox
)

pxr_test_scripts(
    testPlug.py
)

pxr_build_test_shared_lib(TestPlugDso1
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDso1.cpp
) 
pxr_build_test_shared_lib(TestPlugDso2
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDso2.cpp
) 
pxr_build_test_shared_lib(TestPlugDso3
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDso3.cpp
) 
pxr_build_test_shared_lib(TestPlugDsoEmpty
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDsoEmpty.cpp
) 
pxr_build_test_shared_lib(TestPlugDsoIncomplete
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDsoIncomplete.cpp
) 
pxr_build_test_shared_lib(TestPlugDsoUnloadable
    INSTALL_PREFIX PlugPlugins
    LIBRARIES
        tf
    CPPFILES
        testenv/TestPlugDsoUnloadable.cpp
) 
# This library helps test behavior when a plugin is unloadable,
# but some linkers by default will error out when building this due
# to the references to undefined symbols. So we need to specify a
# link flag to force these symbols to be looked up at runtime.
if (TARGET TestPlugDsoUnloadable)
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        set_target_properties(TestPlugDsoUnloadable
            PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # This forces the link to complete but the linker will still
        # report the missing symbol as an error and will also emit a
        # warning that the link was forced.  This is unfortunate as
        # it causes confusion.  There doesn't appear to be any way to
        # suppress either the warning or the error, though.  Rest
        # assured that the unresolved external symbol error is okay
        # for this target.
        set_target_properties(TestPlugDsoUnloadable
            PROPERTIES
            LINK_FLAGS "/FORCE:UNRESOLVED"
        )
    endif()
endif()

pxr_create_test_module(TestPlugModule1
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModule2
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModule3
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleEmpty
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleLoaded
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleLoadedBadBase
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleUnloadable
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleUnloadable2
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleDepBadBase
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleDepBadDep
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleDepBadDep2
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleDepBadLoad
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleDepCycle
    INSTALL_PREFIX PlugPlugins
)
pxr_create_test_module(TestPlugModuleIncomplete
    INSTALL_PREFIX PlugPlugins
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Create a test module that has a unicode character in it's path
    # for Windows
    pxr_create_test_module(TestPlugModuleUnicode
        INSTALL_PREFIX PlugPlugins
        DEST_DIR TestPlugMödüleÜnicöde
    )
else()
    # Create a test module without a unicode character in the path
    # to make this test consistent on other OSes
    pxr_create_test_module(TestPlugModuleUnicode
        INSTALL_PREFIX PlugPlugins
    )
endif()

pxr_register_test(testPlug
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testPlug"
)

