set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usdShaders)

pxr_plugin(usdShaders
    LIBRARIES
        ar
        sdr
        usdShade

    PRIVATE_CLASSES
        discoveryPlugin

    PYMODULE_FILES
        __init__.py

    RESOURCE_FILES
        plugInfo.json
        shaders/shaderDefs.usda
        shaders/previewSurface.glslfx
        shaders/uvTexture.glslfx
        shaders/primvarReader.glslfx
        shaders/transform2d.glslfx

    DOXYGEN_FILES
        overview.dox
)
