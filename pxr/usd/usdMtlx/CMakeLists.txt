set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdMtlx)

if (MATERIALX_STDLIB_DIR)
    add_definitions(-DPXR_MATERIALX_STDLIB_DIR="${MATERIALX_STDLIB_DIR}")
endif()

pxr_library(usdMtlx
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        arch
        gf
        sdf
        sdr
        tf
        vt
        usd
        usdGeom
        usdShade
        usdUI
        usdUtils
        MaterialXCore
        MaterialXFormat

    CPPFILES
        discovery.cpp
        parser.cpp

    PRIVATE_CLASSES
        backdoor
        fileFormat

    PUBLIC_CLASSES
        debugCodes
        reader
        utils

    PUBLIC_HEADERS
        api.h

    PYMODULE_CPPFILES
        wrapBackdoor.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.dox
)

pxr_test_scripts(
    testenv/testUsdMtlxDiscovery.py
    testenv/testUsdMtlxFileFormat.py
    testenv/testUsdMtlxParser.py
)

pxr_install_test_dir(
    SRC testenv/testUsdMtlxDiscovery.testenv
    DEST testUsdMtlxDiscovery
)

pxr_install_test_dir(
    SRC testenv/testUsdMtlxFileFormat.testenv
    DEST testUsdMtlxFileFormat
)

pxr_install_test_dir(
    SRC testenv/testUsdMtlxParser.testenv
    DEST testUsdMtlxParser
)

pxr_register_test(testUsdMtlxDiscovery
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdMtlxDiscovery"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdMtlxFileFormat
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdMtlxFileFormat"
    DIFF_COMPARE Looks.usda NodeGraphs.usda GraphlessNodes.usda usd_preview_surface_gold.usda Include.usda Include_From_Usdz.usda CustomNodeDef.usda
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdMtlxParser
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdMtlxParser"
    EXPECTED_RETURN_CODE 0
)
