set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdRiPxrImaging)

pxr_library(usdRiPxrImaging
    LIBRARIES
        gf
        tf
        plug
        trace
        vt
        work
        hd
        pxOsd
        sdf
        usd
        usdGeom
        usdLux
        usdShade
        usdImaging
        usdVol
        ar

    PUBLIC_CLASSES
        pxrAovLightAdapter
        pxrCameraAPIAdapter
        pxrDisplayFilterAdapter
        pxrIntegratorAdapter
        pxrSampleFilterAdapter
        tokens

    PRIVATE_CLASSES
        pxrRenderTerminalHelper

    PRIVATE_HEADERS
        dataSourcePxrRenderTerminalPrims.h

    PUBLIC_HEADERS
        api.h
        version.h

    RESOURCE_FILES
        plugInfo.json
)

