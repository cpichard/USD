set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE sdrOsl)

if(NOT PXR_ENABLE_OSL_SUPPORT)
    return()
endif()

# Use the import targets set by Imath's package config
if (Imath_FOUND)
    set(__OSL_IMATH_LIBS "Imath::Imath")
else()
    set(__OSL_IMATH_INCLUDE ${OPENEXR_INCLUDE_DIRS})
    set(__OSL_IMATH_LIBS ${OPENEXR_LIBRARIES})
endif()

pxr_plugin(sdrOsl
    LIBRARIES
        gf
        tf
        vt
        ar
        sdr
        ${OSL_QUERY_LIBRARY}
        ${OIIO_LIBRARIES}
        ${__OSL_IMATH_LIBS}

    INCLUDE_DIRS
        ${OSL_INCLUDE_DIR}
        ${OIIO_INCLUDE_DIRS}
        ${__OSL_IMATH_INCLUDE}

    PUBLIC_CLASSES
        oslParser

    PRIVATE_HEADERS
        api.h

    PYMODULE_CPPFILES
        module.cpp
        wrapOslParser.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
)

pxr_test_scripts(
    testenv/testOslParser.py
)

pxr_install_test_dir(
    SRC testenv/testOslParser.testenv
    DEST testOslParser
)

pxr_register_test(testOslParser
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testOslParser"
    EXPECTED_RETURN_CODE 0
)
