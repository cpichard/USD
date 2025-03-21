//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdLux/discoveryPlugin.h"

#include "pxr/usd/usdLux/boundableLightBase.h"
#include "pxr/usd/usdLux/lightDefParser.h"
#include "pxr/usd/usdLux/nonboundableLightBase.h"

#include "pxr/base/plug/thisPlugin.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/tf/staticTokens.h"

#include "pxr/usd/usd/schemaRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

const SdrStringVec& 
UsdLux_DiscoveryPlugin::GetSearchURIs() const
{
    static const SdrStringVec empty;
    return empty;
}

SdrShaderNodeDiscoveryResultVec
UsdLux_DiscoveryPlugin::DiscoverShaderNodes(const Context &context)
{
    SdrShaderNodeDiscoveryResultVec result;

    // We want to discover nodes for all concrete schema types that derive from 
    // UsdLuxBoundableLightBase and UsdLuxNonboundableLightBase. We'll filter
    // out types that aren't defined in UsdLux as we process them.
    static const TfType boundableLightType = 
        TfType::Find<UsdLuxBoundableLightBase>();
    static const TfType nonboundableLightType = 
        TfType::Find<UsdLuxNonboundableLightBase>();

    std::set<TfType> types;
    PlugRegistry::GetAllDerivedTypes(boundableLightType, &types);
    PlugRegistry::GetAllDerivedTypes(nonboundableLightType, &types);

    // We include certain API schema types in the discovery results.
    // - MeshLightAPI
    // - VolumeLightAPI
    // Current UsdLux OM specified MeshLightAPI and VolumeLightAPI as basically 
    // the types for MeshLight and VolumeLight, also notice shaderId defined for
    // these API types is MeshLight and VolumeLight respectively.
    const UsdLux_LightDefParserPlugin::ShaderIdToAPITypeNameMap 
        &shaderIdToAPITypeNameMap = 
            UsdLux_LightDefParserPlugin::_GetShaderIdToAPITypeNameMap();

    // Collect all typenames for which we need to associate discovered nodes.
    TfTokenVector typeNames;
    typeNames.reserve(types.size() + shaderIdToAPITypeNameMap.size());
    for (const TfType &type : types) {
        // Filter out types that weren't declared in the UsdLux library itself.
        static PlugPluginPtr thisPlugin = PLUG_THIS_PLUGIN;
        if (!thisPlugin->DeclaresType(type)) {
            continue;
        }

        const TfToken name = UsdSchemaRegistry::GetConcreteSchemaTypeName(type);

        // The type name from the schema registry will be empty if the type is 
        // not concrete (i.e. abstract); we skip abstract types.
        if (!name.IsEmpty()) {
            // The schema type name is the name and identifier.  
            typeNames.push_back(name);
        }
    }

    for (const auto& shaderIdAPITypeNamePair : shaderIdToAPITypeNameMap) {
        typeNames.push_back(shaderIdAPITypeNamePair.first);
    }

    result.reserve(typeNames.size());
    for(const TfToken &typeName : typeNames) {
        // The URIs are left empty as these nodes can be populated from the 
        // schema registry prim definitions.
        result.emplace_back(
                typeName,
                SdrVersion().GetAsDefault(),
                typeName,
                /*family*/ TfToken(),
                UsdLux_LightDefParserPlugin::_GetDiscoveryType(),
                UsdLux_LightDefParserPlugin::_GetSourceType(),
                /*uri=*/ "",
                /*resolvedUri=*/ "");
    }

    return result;
}

SDR_REGISTER_DISCOVERY_PLUGIN(UsdLux_DiscoveryPlugin);

PXR_NAMESPACE_CLOSE_SCOPE
