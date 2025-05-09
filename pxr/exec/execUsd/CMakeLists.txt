set(PXR_PREFIX pxr/exec)
set(PXR_PACKAGE execUsd)

pxr_library(execUsd
    LIBRARIES
        esf
        esfUsd
        exec
        tf
        trace
        sdf
        usd
        
    PUBLIC_HEADERS
        api.h

    PUBLIC_CLASSES
        cacheView
        request
        system
        valueKey

    PRIVATE_HEADERS
        visitValueKey.h

    PRIVATE_CLASSES
        requestImpl

    RESOURCE_FILES
        plugInfo.json
)

pxr_build_test(testExecUsdBasicCompilation
    LIBRARIES
        gf
        tf
        esf
        exec
        execUsd
        vdf
        usdGeom
    CPPFILES
        testenv/testExecUsdBasicCompilation.cpp
)

pxr_install_test_dir(
    SRC testenv/testExecUsdBasicCompilation
    DEST testExecUsdBasicCompilation
)

pxr_register_test(testExecUsdBasicCompilation
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testExecUsdBasicCompilation"
    EXPECTED_RETURN_CODE 0
    POST_COMMAND "${PYTHON_EXECUTABLE} normalizeDotFile.py testCompiler.dot testCompiler.out"
    DIFF_COMPARE testCompiler.out
    TESTENV testExecUsdBasicCompilation
)

pxr_build_test(testExecUsdExternalRegistration
    LIBRARIES
        exec
        tf
        vdf
    CPPFILES
        testenv/testExecUsdExternalRegistration.cpp
)

pxr_register_test(testExecUsdExternalRegistration
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testExecUsdExternalRegistration"
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testExecUsdRequest
    LIBRARIES
        gf
        plug
        tf
        work
        esf
        exec
        execUsd
        vdf
    CPPFILES
        testenv/testExecUsdRequest.cpp
)

pxr_install_test_dir(
    SRC testenv/testExecUsdRequest
    DEST testExecUsdRequest
)

pxr_register_test(testExecUsdRequest
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testExecUsdRequest"
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testExecUsdRequestInvalidation
    LIBRARIES
        gf
        plug
        tf
        ef
        esf
        exec
        execUsd
        vdf
    CPPFILES
        testenv/testExecUsdRequestInvalidation.cpp
)

pxr_install_test_dir(
    SRC testenv/testExecUsdRequestInvalidation
    DEST testExecUsdRequestInvalidation
)

pxr_register_test(testExecUsdRequestInvalidation
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testExecUsdRequestInvalidation"
    EXPECTED_RETURN_CODE 0
)

pxr_build_test(testExecUsdUncompilation
    LIBRARIES
        exec
        execUsd
        plug
        tf
        sdf
        usd
    CPPFILES
        testenv/testExecUsdUncompilation.cpp
)

pxr_install_test_dir(
    SRC testenv/testExecUsdUncompilation
    DEST testExecUsdUncompilation
)

pxr_register_test(testExecUsdUncompilation
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testExecUsdUncompilation"
    EXPECTED_RETURN_CODE 0
    POST_COMMAND "${PYTHON_EXECUTABLE} normalizeDotFiles.py                    \
        TestUncompileAttributeInput-compiled.dot                               \
        TestUncompileAttributeInput-uncompiled.dot                             \
        TestUncompileConstantComputation-compiled.dot                          \
        TestUncompileConstantComputation-uncompiled.dot                        \
        TestUncompileNamespaceAncestorInput-compiled.dot                       \
        TestUncompileNamespaceAncestorInput-uncompiled.dot                     \
        TestUncompileRecursiveResync-compiled.dot                              \
        TestUncompileRecursiveResync-uncompiled.dot                            \
        "
    DIFF_COMPARE TestUncompileAttributeInput-compiled.out
    DIFF_COMPARE TestUncompileAttributeInput-uncompiled.out
    DIFF_COMPARE TestUncompileConstantComputation-compiled.out
    DIFF_COMPARE TestUncompileConstantComputation-uncompiled.out
    DIFF_COMPARE TestUncompileNamespaceAncestorInput-compiled.out
    DIFF_COMPARE TestUncompileNamespaceAncestorInput-uncompiled.out
    DIFF_COMPARE TestUncompileRecursiveResync-compiled.out
    DIFF_COMPARE TestUncompileRecursiveResync-uncompiled.out
)
