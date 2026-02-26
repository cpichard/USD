set(PXR_PREFIX pxr/exec)
set(PXR_PACKAGE esfUsd)

pxr_library(esfUsd
    LIBRARIES
        arch
        esf
        tf
        sdf
        usd
        
    PUBLIC_HEADERS
        api.h

    PUBLIC_CLASSES
        sceneAdapter
        stageData

    PRIVATE_CLASSES
        attribute
        attributeQuery
        object
        prim
        property
        relationship
        stage

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        README.md        
)

pxr_build_test(testEsfUsdConnectionTables
    LIBRARIES
        esf
        esfUsd
        sdf
        tf
        usd
        usdGeom
    CPPFILES
        testenv/testEsfUsdConnectionTables.cpp
)

pxr_build_test(testEsfUsdSceneObjects
    LIBRARIES
        esf
        esfUsd
        usdGeom
    CPPFILES
        testenv/testEsfUsdSceneObjects.cpp
)

pxr_register_test(testEsfUsdSceneObjects
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testEsfUsdSceneObjects"
    EXPECTED_RETURN_CODE 0
)

pxr_register_test(testEsfUsdConnectionTables
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testEsfUsdConnectionTables"
    EXPECTED_RETURN_CODE 0
    ENV TF_FATAL_VERIFY=1
)
