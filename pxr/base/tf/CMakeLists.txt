# Make sure not to get the wrong semaphore.h
set(CMAKE_INCLUDE_CURRENT_DIR OFF)

set(PXR_PACKAGE tf)
set(PXR_PREFIX pxr/base)

if(WIN32)
    set(WINLIBS Shlwapi.lib)
endif()

# Add a test that WindowsImportWrapper adds entries on the PATH in the correct order, to ensure
# that the correct dlls needed by python modules are loaded
function(add_py_dll_link_test)
    # early return if we are not building tests or if we are not on windows.
    if (NOT (PXR_BUILD_TESTS AND WIN32))
        return()
    endif()

    # early return if python target is not built! (No python build enabled or
    # doing a static build, etc)
    if (NOT TARGET python_modules)
        return()
    endif()

    # The structure we are going to make is:
    #  - python test script: testTfPyDllLink.py
    #  - python module: testTfPyDllLinkModule/__init__.py
    #  - python c extension: testTfPyDllLinkModule/_testTfPyDllLinkModule.pyd
    #  - cpp lib: testTfPyDllLinkImplementation.dll
    #    - for the cpp lib that the python extension links to, we create two dlls with the same name
    #      - a "Good" one which returns 0, and
    #      - a "Bad" one which returns 0xBAD
    #    - they are placed in subdirs, in both build and install dirs:
    #      - ImplementationGood/testTfPyDllLink.dll
    #      - ImplementationBad/testTfPyDllLink.dll
    #    - this will allow us to test which is loading by adding different directories to the PATH

    # Create the good/bad implementations: testTfPyDllLinkImplementation.dll
    foreach(implementation_suffix Good Bad)
        set(implementation_name "testTfPyDllLinkImplementation${implementation_suffix}")
        add_library(${implementation_name} SHARED
            "testenv/${implementation_name}.cpp"
        )

        set(directory_name "Implementation${implementation_suffix}")

        set_target_properties(${implementation_name} PROPERTIES
            OUTPUT_NAME "testTfPyDllLinkImplementation"
            RUNTIME_OUTPUT_DIRECTORY "${directory_name}"  # for the windows .dll
            ARCHIVE_OUTPUT_DIRECTORY "${directory_name}"  # for the windows .lib
        )

        install(
            TARGETS ${implementation_name}
            RUNTIME DESTINATION "tests/ctest/TfPyDllLinkTestEnv/testTfPyDllLinkModule/Implementation${implementation_suffix}"
        )
    endforeach()

    # create the _testTfPyDllLinkModule.pyd compiled python module
    add_library(testTfPyDllLinkModule SHARED
        "testenv/testTfPyDllLinkModule.c"
    )
    add_dependencies(python_modules testTfPyDllLinkModule)

    set(module_name testTfPyDllLinkModule)
    if (PXR_USE_DEBUG_PYTHON)
        target_compile_definitions(testTfPyDllLinkModule PRIVATE PXR_USE_DEBUG_PYTHON)
        # On Windows when compiling with debug python the library must be named with _d.
        set(module_filename "_${module_name}_d")
    else()
        set(module_filename "_${module_name}")
    endif()

    set_target_properties(testTfPyDllLinkModule PROPERTIES
        PREFIX ""
        # Python modules must be suffixed with .pyd on Windows.
        SUFFIX ".pyd"
        OUTPUT_NAME ${module_filename}
    )
    target_include_directories(testTfPyDllLinkModule
        SYSTEM
        PRIVATE
            ${PYTHON_INCLUDE_DIRS}
    )

    target_link_libraries(testTfPyDllLinkModule PRIVATE ${PYTHON_LIBRARIES} testTfPyDllLinkImplementation)

    # tell our python module to link against the "good" implementation while building
    # (When actually running the test, which dll is found is controlled by the PATH env var)
    add_dependencies(testTfPyDllLinkModule testTfPyDllLinkImplementationGood)
    target_link_directories(testTfPyDllLinkModule PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ImplementationGood")

    install(
        TARGETS testTfPyDllLinkModule
        RUNTIME DESTINATION "tests/ctest/TfPyDllLinkTestEnv/testTfPyDllLinkModule"
    )

    # install the testenv dir, which has the testTfPyDllLinkModule.py python module
    pxr_install_test_dir(
        SRC testenv/TfPyDllLinkTestEnv
        DEST TfPyDllLinkTestEnv
    )

    # finally register the test itself
    pxr_register_test(testTfPyDllLink
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyDllLink"
        TESTENV TfPyDllLinkTestEnv
    )

endfunction()

pxr_library(tf
    LIBRARIES
        arch
        ${WINLIBS}
        TBB::tbb

    PUBLIC_CLASSES
        anyUniquePtr
        anyWeakPtr
        atomicOfstreamWrapper
        bigRWMutex
        bits
        bitUtils
        compressedBits
        debug
        debugNotice
        denseHashMap
        denseHashSet
        diagnostic
        diagnosticBase
        diagnosticHelper
        diagnosticMgr
        dl
        enum
        envSetting
        error
        errorMark
        errorTransport
        exception
        expiryNotifier
        fastCompression
        fileUtils
        getenv
        hash
        iterator
        mallocTag
        notice
        nullPtr
        ostreamMethods
        pathUtils
        patternMatcher
        pointerAndBits
        pyLock
        pyObjWrapper
        pyTracing
        refBase
        refPtr
        refPtrTracker
        regTest
        registryManager
        safeOutputFile
        scoped
        scopeDescription
        setenv
        singleton
        smallVector
        spinMutex
        spinRWMutex
        stackTrace
        stacked
        status
        stl
        stopwatch
        stringUtils
        templateString
        tf
        token
        type
        typeFunctions
        typeNotice
        unicodeUtils
        warning
        weakBase
        weakPtr
        weakPtrFacade

    PYTHON_PUBLIC_CLASSES
        makePyConstructor
        pyAnnotatedBoolResult
        pyArg
        pyCall
        pyCallContext
        pyClassMethod
        pyContainerConversions
        pyEnum
        pyError
        pyExceptionState
        pyFunction
        pyIdentity
        pyInterpreter
        pyInvoke
        pyModule
        pyModuleNotice
        pyNoticeWrapper
        pyObjectFinder
        pyOptional
        pyOverride
        pyPolymorphic
        pyPtrHelpers
        pyResultConversions
        pySingleton
        pyUtils
        pyWrapContext
        scriptModuleLoader
        wrapTypeHelpers

    PUBLIC_HEADERS
        api.h
        callContext.h
        cxxCast.h
        declarePtrs.h
        delegatedCountPtr.h
        diagnosticLite.h
        functionTraits.h
        functionRef.h
        hashmap.h
        hashset.h
        instantiateSingleton.h
        instantiateStacked.h
        instantiateType.h
        meta.h
        pxrCLI11/CLI11.h
        pxrTslRobinMap/robin_growth_policy.h
        pxrTslRobinMap/robin_hash.h
        pxrTslRobinMap/robin_map.h
        pxrTslRobinMap/robin_set.h
        preprocessorUtilsLite.h
        safeTypeCompare.h
        span.h
        staticData.h
        staticTokens.h
        typeInfoMap.h
        type_Impl.h

    PYTHON_PUBLIC_HEADERS
        py3Compat.h
        pySafePython.h
        pySignatureExt.h
        pyStaticTokens.h

    PRIVATE_CLASSES
        atomicRenameUtil
        debugCodes
        noticeRegistry
        unicodeCharacterClasses

    PYTHON_PRIVATE_CLASSES
        pyErrorInternal

    PRIVATE_HEADERS
        scopeDescriptionPrivate.h
        pxrDoubleConversion/ieee.h
        pxrDoubleConversion/utils.h
        pxrDoubleConversion/double-conversion.h
        pxrDoubleConversion/bignum-dtoa.h
        pxrDoubleConversion/bignum.h
        pxrDoubleConversion/cached-powers.h
        pxrDoubleConversion/diy-fp.h
        pxrDoubleConversion/double-to-string.h
        pxrDoubleConversion/fast-dtoa.h
        pxrDoubleConversion/fixed-dtoa.h
        pxrDoubleConversion/string-to-double.h
        pxrDoubleConversion/strtod.h
        pxrLZ4/lz4.h
        pyWeakObject.h

    CPPFILES
        initConfig.cpp
        pxrDoubleConversion/bignum-dtoa.cc
        pxrDoubleConversion/bignum.cc
        pxrDoubleConversion/cached-powers.cc
        pxrDoubleConversion/double-to-string.cc
        pxrDoubleConversion/fast-dtoa.cc
        pxrDoubleConversion/fixed-dtoa.cc
        pxrDoubleConversion/string-to-double.cc
        pxrDoubleConversion/strtod.cc
        pxrLZ4/lz4.cpp

    PYMODULE_CPPFILES
        module.cpp
        pyWeakObject.cpp
        wrapAnyWeakPtr.cpp
        wrapCallContext.cpp
        wrapDebug.cpp
        wrapDiagnostic.cpp
        wrapDiagnosticBase.cpp
        wrapEnum.cpp
        wrapEnvSetting.cpp
        wrapError.cpp
        wrapException.cpp
        wrapFileUtils.cpp
        wrapFunction.cpp
        wrapMallocTag.cpp
        wrapNotice.cpp
        wrapPathUtils.cpp
        wrapPyContainerConversions.cpp
        wrapPyModuleNotice.cpp
        wrapPyObjWrapper.cpp
        wrapPyOptional.cpp
        wrapRefPtrTracker.cpp
        wrapScopeDescription.cpp
        wrapScriptModuleLoader.cpp
        wrapSingleton.cpp
        wrapStackTrace.cpp
        wrapStatus.cpp
        wrapStopwatch.cpp
        wrapStringUtils.cpp
        wrapTemplateString.cpp
        wrapTestPyAnnotatedBoolResult.cpp
        wrapTestPyContainerConversions.cpp
        wrapTestPyStaticTokens.cpp
        wrapTestTfPython.cpp
        wrapTestTfPyOptional.cpp
        wrapToken.cpp
        wrapType.cpp
        wrapWarning.cpp


    PYMODULE_FILES 
        __init__.py
        testenv/__init__.py
        testenv/testTfScriptModuleLoader_AAA_RaisesError.py
        testenv/testTfScriptModuleLoader_DepLoadsAll.py
        testenv/testTfScriptModuleLoader_LoadsAll.py
        testenv/testTfScriptModuleLoader_LoadsUnknown.py
        testenv/testTfScriptModuleLoader_Other.py
        testenv/testTfScriptModuleLoader_Test.py
        testenv/testTfScriptModuleLoader_Unknown.py
        testenv/testTfPyInvoke_callees.py

    DOXYGEN_FILES
        diagnosticOverview.dox
        errorOverview.dox
        mallocTagOverview.dox
        noteOverview.dox
        overview.dox
        registryOverview.dox
)

if(PXR_ENABLE_PYTHON_SUPPORT)
    pxr_build_test(testTfPyFunction
        LIBRARIES
            tf
        CPPFILES
            testenv/testTfPyFunction.cpp
    ) 

    pxr_build_test(testTfPyInterpreter
        LIBRARIES
            tf
        CPPFILES
            testenv/testTfPyInterpreter.cpp
    )

    pxr_build_test(testTfPyInvoke
        LIBRARIES
            tf
        CPPFILES
            testenv/testTfPyInvoke.cpp
    )

    pxr_build_test(testTfPyLock
        LIBRARIES
            tf
        CPPFILES
            testenv/testTfPyLock.cpp
    )

    pxr_build_test(testTfPyResultConversions
        LIBRARIES
            tf
        CPPFILES
            testenv/testTfPyResultConversions.cpp
    )

    add_py_dll_link_test()
endif()

pxr_build_test(testTfSIGFPE
    LIBRARIES
        tf
    CPPFILES
        testenv/SIGFPE.cpp
)

pxr_build_test(testTfSIGSEGV
    LIBRARIES
        tf
    CPPFILES
        testenv/SIGSEGV.cpp
)

pxr_build_test_shared_lib(TestTfDl
    CPPFILES
        testenv/TestTfDl.cpp
)

pxr_build_test_shared_lib(TestTfRegistryFunctionPlugin
    LIBRARIES
        tf
    CPPFILES
        testenv/TestTfRegistryFunctionPlugin.cpp
)

pxr_build_test(testTfCast
    LIBRARIES
        tf
    CPPFILES
        testenv/testTfCast.cpp
)

pxr_build_test(testTfSpan
    LIBRARIES
        tf
    CPPFILES
        testenv/testTfSpan.cpp
)

pxr_build_test(testTf
    LIBRARIES
        tf
    CPPFILES
        testenv/main.cpp
        testenv/anyUniquePtr.cpp
        testenv/atomicOfstreamWrapper.cpp
        testenv/bits.cpp
        testenv/bitUtils.cpp
        testenv/compressedBits.cpp
        testenv/debug.cpp
        testenv/denseHashMap.cpp
        testenv/delegate.cpp
        testenv/dl.cpp
        testenv/enum.cpp
        testenv/error.cpp
        testenv/envSetting.cpp
        testenv/exception.cpp
        testenv/fastCompression.cpp
        testenv/fileUtils.cpp
        testenv/functionRef.cpp
        testenv/getenv.cpp
        testenv/hash.cpp
        testenv/iterator.cpp
        testenv/mallocTag.cpp
        testenv/meta.cpp
        testenv/notice.cpp
        testenv/pathUtils.cpp
        testenv/patternMatcher.cpp
        testenv/pointerAndBits.cpp
        testenv/preprocessorUtilsLite.cpp
        testenv/probe.cpp
        testenv/refPtr.cpp
        testenv/registryManager.cpp
        testenv/registryManagerUnload.cpp
        testenv/rwMutexes.cpp
        testenv/safeOutputFile.cpp
        testenv/scoped.cpp
        testenv/scopeDescription.cpp
        testenv/setenv.cpp
        testenv/smallVector.cpp
        testenv/stacked.cpp
        testenv/staticData.cpp
        testenv/stl.cpp
        testenv/stopwatch.cpp
        testenv/stringUtils.cpp
        testenv/staticTokens.cpp
        testenv/templateString.cpp
        testenv/token.cpp
        testenv/delegatedCountPtr.cpp
        testenv/type.cpp
        testenv/typeMultipleInheritance.cpp
        testenv/typeInfoMap.cpp
        testenv/unicodeUtils.cpp
        testenv/weakPtr.cpp
)

pxr_test_scripts(
    testenv/testTfCrashHandler.py
    testenv/testTfFileUtils.py
    testenv/testTfMallocTagReport.py
    testenv/testTfNamedTemporaryFile.py
    testenv/testTfPathUtils.py
    testenv/testTfPython.py
    testenv/testTfPyDllLink.py
    testenv/testTfPyNotice.py
    testenv/testTfPyOptional.py
    testenv/testTfPyScopeDescription.py
    testenv/testTfPyStaticTokens.py
    testenv/testTfScriptModuleLoader.py
    testenv/testTfStringUtils.py
    testenv/testTfTemplateString.py
    testenv/testTfType.py
    testenv/testTf_PyContainerConversions.py
)

# These tests will depend on downstream libraries when built monolithic.
# We don't want changes to those libraries to affect test results so we
# simply disable these tests in that case.
if(NOT PXR_BUILD_MONOLITHIC)
pxr_install_test_dir(
    SRC testenv/baseline/TfDebugTestEnv
    DEST TfDebugTestEnv/baseline
)
endif()
pxr_install_test_dir(
    SRC testenv/baseline/TfDelegate
    DEST TfDelegate/baseline
)
pxr_install_test_dir(
    SRC testenv/baseline/TfEnum
    DEST TfEnum/baseline
)
pxr_install_test_dir(
    SRC testenv/baseline/TfEnvSetting
    DEST TfEnvSetting/baseline
)
pxr_install_test_dir(
    SRC testenv/baseline/TfNotice
    DEST TfNotice/baseline
)
pxr_install_test_dir(
    SRC testenv/baseline/testTfScriptModuleLoader
    DEST testTfScriptModuleLoader/baseline
)

pxr_register_test(TfAnyUniquePtr
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfAnyUniquePtr"
)
pxr_register_test(TfAtomicOfstreamWrapper
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfAtomicOfstreamWrapper"
)
pxr_register_test(TfBits
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfBits"
)
pxr_register_test(TfBitUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfBitUtils"
)
pxr_register_test(TfCompressedBits
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfCompressedBits"
)
pxr_register_test(TfDl
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDl"
)
pxr_register_test(TfDebug
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebug"
)
pxr_register_test(TfDebugFatal_1
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebugFatal_1"
    EXPECTED_RETURN_CODE 134
)
pxr_register_test(TfDebugFatal_2
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebugFatal_2"
    EXPECTED_RETURN_CODE 134
)
pxr_register_test(TfDebugFatal_3
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebugFatal_3"
    EXPECTED_RETURN_CODE 134
)
# These tests will depend on downstream libraries when built monolithic.
# We don't want changes to those libraries to affect test results so we
# simply disable these tests in that case.
if(NOT PXR_BUILD_MONOLITHIC)
pxr_register_test(TfDebugTestEnv
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebugTestEnv"
    ENV TF_DEBUG="FOO* FLAM_* FLAM -FOOFLIMFLAM TF_DISCOVERY_D*"
    STDOUT_REDIRECT debugTestEnv.out
    DIFF_COMPARE debugTestEnv.out
)
endif()
pxr_register_test(TfDebugTestEnvHelp
    ENV TF_DEBUG=help
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDebugTestEnvHelp"
)
pxr_register_test(TfDenseHashMap
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDenseHashMap"
)
pxr_register_test(TfDelegate
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDelegateAddRemove"
    STDOUT_REDIRECT output.txt 
    DIFF_COMPARE output.txt 
)
pxr_register_test(TfError
    ENV TF_FATAL_VERIFY=0
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfError"
)
pxr_register_test(TfErrorThreadTransport
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfErrorThreadTransport"
)
pxr_register_test(TfEnvSetting
    ENV
        TF_TEST_BOOL_ENV_SETTING=1
        TF_TEST_INT_ENV_SETTING=123
        TF_TEST_STRING_ENV_SETTING=alpha
        TF_ENV_SETTING_ALERTS_ENABLED=0
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfEnvSetting"
    STDERR_REDIRECT debugTfEnvSettingStderr.txt
    DIFF_COMPARE debugTfEnvSettingStderr.txt
)
pxr_register_test(TfException
    ENV TF_FATAL_THROW=0
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfException"
)
pxr_register_test(TfFileUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfFileUtils"
)
pxr_register_test(TfFileUtils_Python
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfFileUtils"
)
pxr_register_test(TfFunctionRef
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfFunctionRef"
)
pxr_register_test(TfStringUtils_Python
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfStringUtils"
)
pxr_register_test(TfGetenv
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfGetenv"
)
pxr_register_test(TfHash
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfHash"
)
pxr_register_test(TfIterator
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfIterator"
)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pxr_register_test(TfMallocTag
        ENV GLIBCXX_FORCE_NEW=1
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfMallocTag"
    )
endif()
pxr_register_test(TfRefPtr
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfRefPtr"
)
pxr_register_test(TfEnum
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfEnum"
    STDOUT_REDIRECT enum.out
    DIFF_COMPARE enum.out
)
pxr_register_test(TfMallocTagReport
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfMallocTagReport"
)
pxr_register_test(TfNotice
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfNotice"
    STDOUT_REDIRECT notice.out
    DIFF_COMPARE notice.out
)
pxr_register_test(TfNamedTemporaryFile
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfNamedTemporaryFile"
)
pxr_register_test(TfPathUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfPathUtils"
)
pxr_register_test(TfPathUtils_Python
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPathUtils"
)
pxr_register_test(TfPatternMatcher
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfPatternMatcher"
)
pxr_register_test(TfPointerAndBits
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfPointerAndBits"
)
pxr_register_test(TfPreprocessorUtilsLite
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfPreprocessorUtilsLite"
)
pxr_register_test(TfProbe
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfProbe"
)
pxr_register_test(TfRWMutexes
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfRWMutexes"
)
pxr_register_test(TfRegistryManager
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfRegistryManager"
)
pxr_register_test(TfRegistryManagerUnload
    REQUIRES_SHARED_LIBS
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfRegistryManagerUnload"
)
pxr_register_test(TfRegTest
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf"
    EXPECTED_RETURN_CODE 2
)
pxr_register_test(TfRegTest_TfScoped
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfScoped no args expected"
    EXPECTED_RETURN_CODE 2
)
pxr_register_test(TfRegTest_TfUndefinedTest
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfUndefinedTest"
    EXPECTED_RETURN_CODE 3
)
pxr_register_test(TfSafeOutputFile
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfSafeOutputFile"
)
pxr_register_test(TfScopeDescription
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfScopeDescription"
)
pxr_register_test(TfScoped
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfScoped"
)
pxr_register_test(TfScopedVar
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfScopedVar"
)
pxr_register_test(TfSetenv
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfSetenv"
)
pxr_register_test(TfSmallVector
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfSmallVector"
)
pxr_register_test(TfStacked
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStacked"
)
pxr_register_test(TfStaticData
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStaticData"
)
pxr_register_test(TfStaticTokens
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStaticTokens"
)
pxr_register_test(TfStl
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStl"
)
pxr_register_test(TfStopwatch
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStopwatch"
)
pxr_register_test(TfStringUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfStringUtils"
)
pxr_register_test(TfTemplateString
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfTemplateString"
)
pxr_register_test(testTfTemplateString
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfTemplateString"
)
pxr_register_test(TfToken
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfToken"
)
pxr_register_test(TfDelegatedCountPtr
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfDelegatedCountPtr"
)
pxr_register_test(TfType
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfType"
)
pxr_register_test(TfTypeInfoMap
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfTypeInfoMap"
)
pxr_register_test(TfType_MultipleInheritance
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfType_MultipleInheritance"
)
pxr_register_test(TfUnicodeUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfUnicodeUtils"
)
pxr_register_test(TfWeakPtr
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfWeakPtr"
)
pxr_register_test(TfWeakPtrConversion
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf TfWeakPtrConversion"
)

if(PXR_ENABLE_PYTHON_SUPPORT)
    pxr_register_test(testTfPython
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPython"
    )
    pxr_register_test(testTfPyFunction
        REQUIRES_PYTHON_MODULES
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyFunction"
    )
    pxr_register_test(testTfPyNotice
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyNotice"
    )
    pxr_register_test(testTfPyInterpreter
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyInterpreter"
    )
    pxr_register_test(testTfPyInvoke
        REQUIRES_PYTHON_MODULES
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyInvoke"
    )
    pxr_register_test(testTfPyLock
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyLock"
    )
    pxr_register_test(testTfPyResultConversions
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyResultConversions"
    )
    pxr_register_test(testTfPyScopeDescription
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyScopeDescription"
    )
    pxr_register_test(testTfScriptModuleLoader
        PYTHON
        COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfScriptModuleLoader"
        STDOUT_REDIRECT scriptModuleLoader.out
        DIFF_COMPARE scriptModuleLoader.out
    )
endif()

pxr_register_test(testTfType
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfType"
)
pxr_register_test(testTfSIGSEGV
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfCrashHandler ${CMAKE_INSTALL_PREFIX}/tests/testTfSIGSEGV SIGSEGV"
)
pxr_register_test(testTfSIGFPE
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfCrashHandler ${CMAKE_INSTALL_PREFIX}/tests/testTfSIGFPE SIGFPE"
)
pxr_register_test(testTf_PyContainerConversions
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTf_PyContainerConversions"
)
pxr_register_test(testTf_PyOptional
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyOptional"
)
pxr_register_test(testTf_PyStaticTokens
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfPyStaticTokens"
)
pxr_register_test(testTfCxxCast
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfCast"
)
pxr_register_test(testTfSpan
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testTfSpan"
)
