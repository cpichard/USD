set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdsi)

pxr_library(hdsi
    LIBRARIES
        plug
        tf
        trace
        vt
        work
        sdf
        cameraUtil
        geomUtil
        hf
        hd
        pxOsd
        TBB::tbb

    PUBLIC_CLASSES
        computeSceneIndexDiff
        coordSysPrimSceneIndex
        debuggingSceneIndex
        debuggingSceneIndexPlugin
        domeLightCameraVisibilitySceneIndex
        extComputationDependencySceneIndex
        extComputationPrimvarPruningSceneIndex
        implicitSurfaceSceneIndex
        legacyDisplayStyleOverrideSceneIndex
        lightLinkingSceneIndex
        materialBindingResolvingSceneIndex
        materialOverrideResolvingSceneIndex
        materialPrimvarTransferSceneIndex
        nodeIdentifierResolvingSceneIndex
        nurbsApproximatingSceneIndex
        prefixPathPruningSceneIndex
        pinnedCurveExpandingSceneIndex
        primManagingSceneIndexObserver
        primTypeAndPathPruningSceneIndex
        primTypeNoticeBatchingSceneIndex
        primTypePruningSceneIndex
        renderSettingsFilteringSceneIndex
        sceneGlobalsSceneIndex
        switchingSceneIndex
        tetMeshConversionSceneIndex
        unboundMaterialPruningSceneIndex
        utils
        velocityMotionResolvingSceneIndex

    PUBLIC_HEADERS
        api.h
        version.h

    RESOURCE_FILES
        plugInfo.json
)

pxr_build_test(testHdsiComputeSceneIndexDiff
   LIBRARIES
       hd
       hdsi
       tf

   CPPFILES
       testenv/testHdsiComputeSceneIndexDiff.cpp
)
pxr_build_test(testHdsiPinnedCurveExpandingSceneIndex
   LIBRARIES
       hd
       hdsi
       tf

   CPPFILES
       testenv/testHdsiPinnedCurveExpandingSceneIndex.cpp
)
pxr_build_test(testHdsiSwitchingSceneIndex
   LIBRARIES
       hd
       hdsi
       tf

   CPPFILES
       testenv/testHdsiSwitchingSceneIndex.cpp
)
pxr_build_test(testHdsiPrefixPathPruningSceneIndex
   LIBRARIES
       hd
       hdsi
       tf

   CPPFILES
       testenv/testHdsiPrefixPathPruningSceneIndex.cpp
)

pxr_register_test(testHdsiComputeSceneIndexDiff
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdsiComputeSceneIndexDiff"
)
pxr_register_test(testHdsiPinnedCurveExpandingSceneIndex
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdsiPinnedCurveExpandingSceneIndex"
)
pxr_register_test(testHdsiSwitchingSceneIndex
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdsiSwitchingSceneIndex"
)
pxr_register_test(testHdsiPrefixPathPruningSceneIndex
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/testHdsiPrefixPathPruningSceneIndex"
)
