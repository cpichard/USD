set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdGp)

set(optionalPublicClasses "")

pxr_library(hdGp
    LIBRARIES
        hd
        hf
        TBB::tbb

    PUBLIC_CLASSES
        generativeProcedural
        generativeProceduralPlugin
        generativeProceduralPluginRegistry
        generativeProceduralFilteringSceneIndex
        generativeProceduralResolvingSceneIndex
        sceneIndexPlugin
        
    PUBLIC_HEADERS
        api.h

    RESOURCE_FILES
        plugInfo.json
)
