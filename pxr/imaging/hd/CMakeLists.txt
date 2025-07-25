set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hd)

set(optionalPublicClasses "")

pxr_library(hd
    LIBRARIES
        plug
        tf
        trace
        vt
        work
        sdf
        cameraUtil
        hf
        pxOsd
        sdr
        TBB::tbb

    PUBLIC_CLASSES
        aov
        basisCurves
        basisCurvesSchema
        basisCurvesTopology
        basisCurvesTopologySchema
        bprim
        bufferArray
        bufferArrayRange
        bufferSource
        bufferSpec
        cachingSceneIndex
        camera
        cameraSchema
        capsuleSchema
        categoriesSchema
        changeTracker
        collectionExpressionEvaluator
        collectionPredicateLibrary
        collectionSchema
        collectionsSchema
        command
        coneSchema
        containerDataSourceEditor
        containerSchema
        coordSys
        coordSysSchema
        coordSysBindingSchema
        cubeSchema
        cylinderSchema
        dataSource
        dataSourceHash
        dataSourceLegacyPrim
        dataSourceLegacyTaskPrim
        dataSourceLocator
        dataSourceMaterialNetworkInterface
        debugCodes
        dependenciesSchema
        dependencyForwardingSceneIndex
        dependencySchema
        dirtyBitsTranslator
        dirtyList
        displayFilterSchema
        drawingCoord
        drawItem
        engine
        enums
        extComputation
        extComputationContext
        extComputationContextInternal
        extComputationCpuCallback
        extComputationInputComputationSchema
        extComputationOutputSchema
        extComputationPrimvarSchema
        extComputationPrimvarsSchema
        extComputationSchema
        extComputationUtils
        extentSchema
        field
        filteringSceneIndex
        flattenedDataSourceProvider
        flattenedDataSourceProviders
        flattenedOverlayDataSourceProvider
        flattenedPrimvarsDataSourceProvider
        flattenedPurposeDataSourceProvider
        flattenedVisibilityDataSourceProvider
        flattenedXformDataSourceProvider
        flatteningSceneIndex
        flatNormals
        geomSubset
        geomSubsetSchema
        imageShader
        imageShaderSchema
        instanceCategoriesSchema
        instancedBySchema
        instanceIndicesSchema
        instancer
        instanceRegistry
        instancerTopologySchema
        instanceSchema
        integratorSchema
        lazyContainerDataSource
        legacyDisplayStyleSchema
        legacyGeomSubsetSceneIndex
        legacyPrimSceneIndex
        legacyTaskFactory
        legacyTaskSchema
        lensDistortionSchema
        light
        lightSchema
        mapContainerDataSource
        material
        materialBindingSchema
        materialBindingsSchema
        materialConnectionSchema
        materialFilteringSceneIndexBase
        materialInterfaceMappingSchema
        materialNetworkSchema
        materialNetwork2Interface
        materialNodeSchema
        materialNodeParameterSchema
        materialOverrideSchema
        materialSchema
        mergingSceneIndex
        mesh
        meshSchema
        meshTopology
        meshTopologySchema
        meshUtil
        noticeBatchingSceneIndex
        nurbsCurvesSchema
        nurbsPatchSchema
        nurbsPatchTrimCurveSchema
        overlayContainerDataSource
        perfLog
        planeSchema
        pluginRenderDelegateUniqueHandle
        points
        prefixingSceneIndex
        primDataSourceOverlayCache
        primGather
        primOriginSchema
        primTypeIndex
        primvarSchema
        primvarsSchema
        purposeSchema
        renderBuffer
        renderBufferSchema
        renderCapabilitiesSchema
        renderDelegate
        rendererPlugin
        rendererPluginHandle
        rendererPluginRegistry
        renderIndex
        renderPass
        renderPassSchema
        renderPassState
        renderProductSchema
        renderSettings
        renderSettingsSchema
        renderVarSchema
        renderThread
        repr
        resourceRegistry
        retainedDataSource
        retainedSceneIndex
        rprim
        rprimCollection
        rprimSharedData
        sampleFilterSchema
        sceneDelegate
        sceneGlobalsSchema
        sceneIndex
        sceneIndexAdapterSceneDelegate
        sceneIndexObserver
        sceneIndexPlugin
        sceneIndexPluginRegistry
        sceneIndexPrimView
        sceneIndexUtil
        schema
        selection
        selectionSchema
        selectionsSchema
        smoothNormals
        sortedIds
        sphereSchema
        splitDiopterSchema
        sprim
        subdivisionTagsSchema
        systemMessages
        systemSchema
        task
        tetMeshSchema
        tetMeshTopologySchema
        timeSampleArray
        tokens
        topology
        types
        unitTestDelegate
        unitTestHelper
        unitTestNullRenderDelegate
        unitTestNullRenderPass
        utils
        vectorSchema
        vertexAdjacency
        visibilitySchema
        volume
        volumeFieldBindingSchema
        volumeFieldSchema
        vtBufferSource
        xformSchema

    PUBLIC_HEADERS
        api.h
        dataSourceTypeDefs.h
        driver.h
        invalidatableContainerDataSource.h
        materialNetworkInterface.h
        schemaTypeDefs.h
        vectorSchemaTypeDefs.h
        version.h

    PRIVATE_CLASSES
        ${optionalPrivateClasses}

    RESOURCE_FILES
        plugInfo.json
        codegenTemplates/schemaClass.h
        codegenTemplates/schemaClass.cpp

    DOXYGEN_FILES
        overview.dox
)

if (NOT PXR_ENABLE_PYTHON_SUPPORT)
    message(STATUS "Skipping building hdGenSchema, Python modules required.")
elseif (NOT JINJA2_FOUND)
    message(WARNING "Skipping building hdGenSchema due to missing dependency: Jinja2")
else()
    pxr_python_bin(hdGenSchema
        DEPENDENCIES
    )
endif()

pxr_build_test(testHdBufferSourceEmptyVal
   LIBRARIES
       hd
       vt
       gf
       tf
       arch

   CPPFILES
       testenv/testHdBufferSourceEmptyVal.cpp
)

pxr_build_test(testHdBufferSpec
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdBufferSpec.cpp
)

pxr_build_test(testHdCommand
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdCommand.cpp
)

pxr_build_test(testHdDataSourceLocator
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdDataSourceLocator.cpp
)

pxr_build_test(testHdDirtyBitsTranslator
   LIBRARIES
       hd
       tf

   CPPFILES
       testenv/testHdDirtyBitsTranslator.cpp
)

pxr_build_test(testHdDirtyList
   LIBRARIES
       hd
       tf

   CPPFILES
       testenv/testHdDirtyList.cpp
)

pxr_build_test(testHdExtCompDependencySort
   LIBRARIES
       hd
       sdf
       tf

   CPPFILES
       testenv/testHdExtCompDependencySort.cpp
)

pxr_build_test(testHdExtComputationUtils
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdExtComputationUtils.cpp
)

pxr_build_test(testHdPerfLog
   LIBRARIES
       hd
       sdf
       tf
       arch

   CPPFILES
       testenv/testHdPerfLog.cpp
)

pxr_build_test(testHdDataSource
   LIBRARIES
       hd
       sdf
       tf

   CPPFILES
       testenv/testHdDataSource.cpp
)

pxr_build_test(testHdSceneIndex
   LIBRARIES
       hd
       sdf
       tf

   CPPFILES
       testenv/testHdSceneIndex.cpp
)

pxr_build_test(testHdSortedIds
   LIBRARIES
       hd
       sdf
       tf

   CPPFILES
       testenv/testHdSortedIds.cpp
)

pxr_build_test(testHdSortedIdsPerf
   LIBRARIES
       hd
       sdf
       tf
       arch

   CPPFILES
       testenv/testHdSortedIdsPerf.cpp
)

pxr_build_test(testHdTimeSampleArray
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdTimeSampleArray.cpp
)

pxr_build_test(testHdTypes
   LIBRARIES
       hd

   CPPFILES
       testenv/testHdTypes.cpp
)

pxr_build_test(testHdCollectionExpressionEvaluator
    LIBRARIES
        hd
        tf
        sdf
        
    CPPFILES
        testenv/testHdCollectionExpressionEvaluator.cpp
)

pxr_install_test_dir(
    SRC testenv/testHdSortedIds
    DEST testHdSortedIds
)


pxr_register_test(testHdBufferSourceEmptyVal
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdBufferSourceEmptyVal"
)

pxr_register_test(testHdBufferSpec
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdBufferSpec"
)

pxr_register_test(testHdCommand
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdCommand"
)

pxr_register_test(testHdDataSourceLocator
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdDataSourceLocator"
)

pxr_register_test(testHdDirtyBitsTranslator
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdDirtyBitsTranslator"
)

pxr_register_test(testHdDirtyList
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdDirtyList"
)

pxr_register_test(testHdExtCompDependencySort
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdExtCompDependencySort"
)

pxr_register_test(testHdExtComputationUtils
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdExtComputationUtils"
)

pxr_register_test(testHdPerfLog
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdPerfLog"
)

pxr_register_test(testHdDataSource
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdDataSource"
)

pxr_register_test(testHdSceneIndex
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdSceneIndex"
)

pxr_register_test(testHdSortedIds
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdSortedIds"
    # XXX Disabling compare for now. testHdSortedId_removeTest.txt differs on Mac and Windows.
    # DIFF_COMPARE testHdSortedId_populateTest.txt testHdSortedId_singleInsertTest.txt testHdSortedId_multiInsertTest.txt testHdSortedId_removeOnlyElementTest.txt testHdSortedId_removeRangeTest.txt testHdSortedId_removeBatchTest.txt testHdSortedId_removeSortedTest.txt testHdSortedId_removeUnsortedTest.txt testHdSortedId_removeAfterInsertNoSyncTest.txt
)

pxr_register_test(testHdTimeSampleArray
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdTimeSampleArray"
)

pxr_register_test(testHdTypes
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdTypes"
)

pxr_register_test(testHdCollectionExpressionEvaluator
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdCollectionExpressionEvaluator"
)
