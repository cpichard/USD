set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdExecImaging)

# usdExecImaging is always built when PXR_BUILD_USD_IMAGING=ON. However, when
# PXR_BUILD_EXEC=OFF, the library does not link to any exec libraries.

if (PXR_BUILD_EXEC)
    set(USDEXECIMAGING_PRIVATE_CLASSES
        stageSceneIndex)

    set(USDEXECIMAGING_CPPFILES
        stageSceneIndexFactory_execOn.cpp)
else()
    message(STATUS
        "Building a minimal ${PXR_PACKAGE} because PXR_BUILD_EXEC is OFF")

    set(USDEXECIMAGING_CPPFILES
        stageSceneIndexFactory_execOff.cpp)
endif()

pxr_library(usdExecImaging
    LIBRARIES
        hd
        tf
        usd

    PUBLIC_CLASSES
        stageSceneIndexInterface

    PUBLIC_HEADERS
        api.h
        stageSceneIndexFactory.h

    PRIVATE_CLASSES
        ${USDEXECIMAGING_PRIVATE_CLASSES}

    CPPFILES
        ${USDEXECIMAGING_CPPFILES}

    RESOURCE_FILES
        plugInfo.json
)
