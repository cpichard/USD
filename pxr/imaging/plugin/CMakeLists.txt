add_subdirectory(hdStorm)
if (${PXR_BUILD_EMBREE_PLUGIN})
    add_subdirectory(hdEmbree)
endif()
if (${PXR_BUILD_PRMAN_PLUGIN} AND "${RENDERMAN_VERSION_MAJOR}" GREATER "22")
    add_subdirectory(hdPrmanLoader)
endif()
if (PXR_BUILD_OPENIMAGEIO_PLUGIN)
    add_subdirectory(hioOiio)
endif()
add_subdirectory(hioAvif)
