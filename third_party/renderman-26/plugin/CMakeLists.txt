add_subdirectory(rmanArgsParser)
add_subdirectory(rmanDiscovery)
add_subdirectory(hdPrman)
add_subdirectory(hdPrmanLoader)
add_subdirectory(hdPrmanXpuLoader)
add_subdirectory(rtx_hioImage)
add_subdirectory(usdRiPxr)
add_subdirectory(rmanUsdValidators)
# Only build rmanOslParser when sdrOsl is disabled
# This allows hdPrman to render OSL files without OSL pre-installed
if (NOT PXR_ENABLE_OSL_SUPPORT)
    add_subdirectory(rmanOslParser)
endif()
