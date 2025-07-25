set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdPhysics)

pxr_library(usdPhysics
    INCLUDE_SCHEMA_FILES

    LIBRARIES
        tf
        plug
        vt
        sdf
        trace
        usd
        usdGeom
        usdShade
        work

    PUBLIC_CLASSES
        metrics
        parseUtils        
        
    PUBLIC_HEADERS
        api.h
        massProperties.h
        parseUtils.h
        parseDesc.h   
        
    PYMODULE_CPPFILES
        wrapMetrics.cpp
        wrapParseUtils.cpp

    PYMODULE_FILES
        __init__.py

    DOXYGEN_FILES
        overview.dox
        parsingUtils.dox        
        examples/usdPhysicsBoxOnBox.usda
        examples/usdPhysicsBoxOnQuad.usda
        examples/usdPhysicsDistanceJoint.usda
        examples/usdPhysicsGroupFiltering.usda
        examples/usdPhysicsJoints.usda
        examples/usdPhysicsPairFiltering.usda
        examples/usdPhysicsSpheresWithMaterial.usda
)

pxr_test_scripts(
    testenv/testUsdPhysicsMetrics.py
    testenv/testUsdPhysicsRigidBodyAPI.py
    testenv/testUsdPhysicsCollisionGroupAPI.py
    testenv/testUsdPhysicsParsing.py
)

pxr_register_test(testUsdPhysicsMetrics
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdPhysicsMetrics"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdPhysicsRigidBodyAPI
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdPhysicsRigidBodyAPI"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdPhysicsCollisionGroupAPI
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdPhysicsCollisionGroupAPI"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testUsdPhysicsParsing
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testUsdPhysicsParsing"
    EXPECTED_RETURN_CODE 0
)
