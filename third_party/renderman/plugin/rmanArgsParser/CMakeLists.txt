set(PXR_PREFIX "")
set(PXR_PACKAGE rmanArgsParser)

set(optionalLibs "")
if(PXR_VERSION AND PXR_VERSION LESS 2508)
    list(APPEND optionalLibs ndr)
endif()

pxr_plugin(rmanArgsParser
    LIBRARIES
        arch
        gf
        tf
        vt
        ar
        sdf
        sdr
        ${optionalLibs}

    PRIVATE_CLASSES
        rmanArgsParser

    CPPFILES
        pugixml/pugixml.cpp
    
    PRIVATE_HEADERS
        api.h
        pugixml/pugiconfig.hpp
        pugixml/pugixml.hpp

    RESOURCE_FILES
        plugInfo.json
)
