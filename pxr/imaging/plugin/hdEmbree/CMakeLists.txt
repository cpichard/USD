set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdEmbree)

if(NOT EMBREE_FOUND)
    message(WARNING "Not building ${PXR_PACKAGE} because of missing dependency: Embree")
    return()
endif()
if (NOT ${PXR_BUILD_GPU_SUPPORT})
    message(STATUS
        "Skipping ${PXR_PACKAGE} because PXR_BUILD_GPU_SUPPORT is OFF")
    return()
endif()

pxr_plugin(hdEmbree
   LIBRARIES
        plug
        tf
        vt
        gf
        work
        hf
        hd
        hdx
        ${EMBREE_LIBRARY}

    INCLUDE_DIRS
        ${EMBREE_INCLUDE_DIR}

    PUBLIC_CLASSES
        config
        instancer
        mesh
        meshSamplers
        renderBuffer
        renderer
        rendererPlugin
        renderDelegate
        renderPass
        sampler

    PUBLIC_HEADERS
        context.h
        renderParam.h

    PRIVATE_CLASSES
        implicitSurfaceSceneIndexPlugin

    RESOURCE_FILES
        plugInfo.json

    DOXYGEN_FILES
        overview.dox
)

if (X11_FOUND OR APPLE)
pxr_build_test(testHdEmbree
    LIBRARIES
        hdEmbree
        garch
        arch
        hio
    CPPFILES
        testenv/testHdEmbree.cpp
)
endif()
