set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdAbc)

if(NOT ALEMBIC_FOUND)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: Alembic")
    return()
endif()

set(optionalLibs "")
set(optionalIncludeDirs "")

if (PXR_ENABLE_HDF5_SUPPORT)
   add_definitions(-DPXR_HDF5_SUPPORT_ENABLED)
   list(APPEND optionalLibs ${HDF5_LIBRARIES} ${HDF5_HL_LIBRARIES})
   list(APPEND optionalIncludeDirs ${HDF5_INCLUDE_DIRS})
endif()

# Use the import targets set by Imath's package config
# Note that the ImathConfig target must also be included to insure the
# Imath directory is part of the include paths when compiling this
# library. This is needed to accommodate Alembic headers that include
# <half.h> instead of <Imath/half.h>
if (Imath_FOUND)
    set(__ALEMBIC_IMATH_LIBS "Imath::ImathConfig;Imath::Imath")
else()
    set(__ALEMBIC_IMATH_INCLUDES ${OPENEXR_INCLUDE_DIRS})
    LIST(APPEND __ALEMBIC_IMATH_LIBS ${OPENEXR_Half_LIBRARY})
    LIST(APPEND __ALEMBIC_IMATH_LIBS ${OPENEXR_Imath_LIBRARY})
    LIST(APPEND __ALEMBIC_IMATH_LIBS ${OPENEXR_Iex_LIBRARY})
    LIST(APPEND __ALEMBIC_IMATH_LIBS ${OPENEXR_IexMath_LIBRARY})
endif()


pxr_plugin(usdAbc
    LIBRARIES
        tf
        work
        sdf
        usd
        usdGeom
        ${ALEMBIC_LIBRARIES}
        ${__ALEMBIC_IMATH_LIBS}
        ${optionalLibs}

    INCLUDE_DIRS
        ${ALEMBIC_INCLUDE_DIR}
        ${__ALEMBIC_IMATH_INCLUDES}
        ${optionalIncludeDirs}

    PRIVATE_CLASSES
        alembicData
        alembicFileFormat
        alembicReader
        alembicTest
        alembicUtil
        alembicWriter

    PRIVATE_HEADERS
        api.h

    PYMODULE_CPPFILES
        module.cpp
        wrapAlembicTest.cpp

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        overview.dox
)

pxr_test_scripts(
    testenv/testUsdAbcAlembicData.py
    testenv/testUsdAbcBugs.py
    testenv/testUsdAbcCamera.py
    testenv/testUsdAbcConversionBasisCurves.py
    testenv/testUsdAbcConversionSubdiv.py
    testenv/testUsdAbcFaceset.py
    testenv/testUsdAbcIndexedProperties.py
    testenv/testUsdAbcInstancing.py
    testenv/testUsdAbcIsConstant.py
    testenv/testUsdAbcP_OldEncoding.py
    testenv/testUsdAbcUvReadWrite.py
    testenv/testUsdAbcUvReadWrite_OldEncoding.py
    testenv/testUsdAbcSDFArguments.py
    testenv/testUsdAbcXformPrimCollapsing.py
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcAlembicData
    DEST testUsdAbcAlembicData
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcBugs
    DEST testUsdAbcBugs
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcCamera
    DEST testUsdAbcCamera
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcConversionBasisCurves
    DEST testUsdAbcConversionBasisCurves
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcConversionSubdiv
    DEST testUsdAbcConversionSubdiv
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcFaceset
    DEST testUsdAbcFaceset
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcIndexedProperties
    DEST testUsdAbcIndexedProperties
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcIndexedGeomArb
    DEST testUsdAbcIndexedGeomArb
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcInstancing
    DEST testUsdAbcInstancing
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcInstancingXinst
    DEST testUsdAbcInstancingXinst
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcInstancingNinst
    DEST testUsdAbcInstancingNinst
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcInstancingPinst
    DEST testUsdAbcInstancingPinst
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcIsConstant
    DEST testUsdAbcIsConstant
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcP_OldEncoding
    DEST testUsdAbcP_OldEncoding
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcUvReadWrite
    DEST testUsdAbcUvReadWrite
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcUvReadWrite_OldEncoding
    DEST testUsdAbcUvReadWrite_OldEncoding
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcSDFArguments
    DEST testUsdAbcSDFArguments
)

pxr_install_test_dir(
    SRC testenv/testUsdAbcXformPrimCollapsing
    DEST testUsdAbcXformPrimCollapsing
)

pxr_register_test(testUsdAbcAlembicData
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcAlembicData"
    EXPECTED_RETURN_CODE 0
    ENV 
        USD_ABC_NUM_OGAWA_STREAMS=2
)

pxr_register_test(testUsdAbcBugs
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcBugs"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcCamera
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcCamera"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcConversionSubdiv
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcConversionSubdiv"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcConversionBasisCurves
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcConversionBasisCurves"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcFaceset
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcFaceset"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcIndexedProperties
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcIndexedProperties"
    DIFF_COMPARE indexedTextureCoordinates.def.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=def
)

pxr_register_test(testUsdAbcIndexedGeomArb
    COMMAND "${CMAKE_INSTALL_PREFIX}/bin/usdcat hasColorSet.abc --out good_primvars_namespace_after_fix.usda"
    DIFF_COMPARE good_primvars_namespace_after_fix.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=def
)

pxr_register_test(testUsdAbcInstancing
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcInstancing"
    DIFF_COMPARE nestedInstancing.def.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=def
)

pxr_register_test(testUsdAbcInstancingXinst
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcInstancing"
    DIFF_COMPARE nestedInstancing.xinst.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=xinst
        USD_ABC_EXPAND_INSTANCES=1
)

pxr_register_test(testUsdAbcInstancingPinst
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcInstancing"
    DIFF_COMPARE nestedInstancing.pinst.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=pinst
        USD_ABC_PARENT_INSTANCES=0
)

pxr_register_test(testUsdAbcInstancingNinst
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcInstancing"
    DIFF_COMPARE nestedInstancing.ninst.usda
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_TESTSUFFIX=ninst
        USD_ABC_DISABLE_INSTANCING=1
)

pxr_register_test(testUsdAbcIsConstant
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcIsConstant"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcP_OldEncoding
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcP_OldEncoding"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcUvReadWrite
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcUvReadWrite"
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY=1
)

pxr_register_test(testUsdAbcUvReadWrite_OldEncoding
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcUvReadWrite_OldEncoding"
    EXPECTED_RETURN_CODE 0
    ENV
        USD_ABC_WRITE_UV_AS_ST_TEXCOORD2FARRAY=0
)

pxr_register_test(testUsdAbcSDFArguments
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcSDFArguments"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdAbcXformPrimCollapsing
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdAbcXformPrimCollapsing"
    EXPECTED_RETURN_CODE 0
)
