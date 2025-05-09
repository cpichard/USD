//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdImaging/materialAdapter.h"
#include "pxr/usdImaging/usdImaging/dataSourceMaterial.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"
#include "pxr/usdImaging/usdImaging/materialParamUtils.h"
#include "pxr/usdImaging/usdImaging/dataSourcePrim.h"

#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"

#include "pxr/imaging/hd/perfLog.h"

#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"

#include "pxr/usd/ar/resolverScopedCache.h"
#include "pxr/usd/ar/resolverContextBinder.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    {
    typedef UsdImagingMaterialAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
    }

    {
    typedef UsdImagingShaderAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
    }

    {
    typedef UsdImagingNodeGraphAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
    }

}

UsdImagingMaterialAdapter::~UsdImagingMaterialAdapter()
{
}

TfTokenVector
UsdImagingMaterialAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
UsdImagingMaterialAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return HdPrimTypeTokens->material;
    }
    return TfToken();
}

HdContainerDataSourceHandle
UsdImagingMaterialAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (subprim.IsEmpty()) {
        return UsdImagingDataSourceMaterialPrim::New(
            prim.GetPath(),
            prim,
            stageGlobals);
    }

    return nullptr;
}

// Generate a terminal data source locator using the terminal
// attribute name.
HdDataSourceLocator
_CreateTerminalLocator(const TfToken& output)
{
    const std::vector<std::string> baseNameComponents
        = SdfPath::TokenizeIdentifier(output);

    // If it's not namespaced use the universal token.
    if (baseNameComponents.size() == 1u) {
        return HdDataSourceLocator(
            HdMaterialSchema::GetSchemaToken(),
            HdMaterialSchemaTokens->universalRenderContext,
            HdMaterialSchemaTokens->terminals, TfToken(baseNameComponents[0])
        );
    }
    // If it's namespaced (eg. mtlx) include that.
    else if (baseNameComponents.size() > 1u) {
        return HdDataSourceLocator(
            HdMaterialSchema::GetSchemaToken(), TfToken(baseNameComponents[0]),
            HdMaterialSchemaTokens->terminals,
            TfToken(SdfPath::StripPrefixNamespace(output, baseNameComponents[0]).first)
        );
    }

    // Just point to the whole data source.
    return HdMaterialSchema::GetDefaultLocator();
}

// Recusively check nodes starting at the terminal to find the dirty prim.
// If the dirty prim is the source material also check the specific dirty
// property.
bool
_IsConnectionDirty(
    const UsdPrim& dirtyPrim,
    const TfTokenVector& dirtyProperties,
    const UsdShadeMaterial& material,
    const UsdShadeConnectionSourceInfo& connection)
{
    if (!connection.IsValid())
        return false;

    // If we reach the root material only dirty if we are connected to the
    // specific property which is dirty and don't recurse further.
    if (connection.source.GetPrim() == material.GetPrim()) {
        if (connection.source.GetPrim() == dirtyPrim) {
            for (const TfToken& dirtyProperty : dirtyProperties) {
                if ((connection.sourceType == UsdShadeAttributeType::Output
                     && dirtyProperty
                         == connection.source.GetOutput(connection.sourceName)
                                .GetFullName())
                    || (connection.sourceType == UsdShadeAttributeType::Input
                        && dirtyProperty
                            == connection.source.GetInput(connection.sourceName)
                                   .GetFullName())) {
                    return true;
                }
            }
        }
        return false;
    }

    // We are connected to the dirty prim
    if (connection.source.GetPrim() == dirtyPrim) {
        return true;
    }

    // If the output we connected to had a direct connection check this.
    if (connection.sourceType == UsdShadeAttributeType::Output) {
        const UsdShadeOutput& output
            = connection.source.GetOutput(connection.sourceName);
        if (output) {
            for (UsdShadeConnectionSourceInfo& outputConnection :
                 output.GetConnectedSources()) {
                if (_IsConnectionDirty(
                        dirtyPrim, dirtyProperties, material,
                        outputConnection)) {
                    return true;
                }
            }
        }
    }

    // Check the input connections on the node.
    for (UsdShadeInput& input : connection.source.GetInputs()) {
        for (UsdShadeConnectionSourceInfo& inputConnection :
             input.GetConnectedSources()) {
            if (_IsConnectionDirty(
                    dirtyPrim, dirtyProperties, material, inputConnection)) {
                return true;
            }
        }
    }

    return false;
}

HdDataSourceLocatorSet
UsdImagingMaterialAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        const UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;

    UsdShadeMaterial material(prim);
    if (!material) {
        return result;
    }

    // If we dirtied an interface input dirty that terminal
    for (UsdShadeOutput& output : material.GetOutputs()) {
        for (UsdShadeConnectionSourceInfo& connection :
             output.GetConnectedSources()) {
            if (_IsConnectionDirty(prim, properties, material, connection)) {
                result.insert(_CreateTerminalLocator(output.GetBaseName()));
            }
        }
    }

    // Otherwise dirty our whole material
    if (result.IsEmpty() && subprim.IsEmpty()) {
        result.insert(UsdImagingDataSourceMaterialPrim::Invalidate(
            prim, subprim, properties, invalidationType));
    }

    return result;
}

HdDataSourceLocatorSet
UsdImagingMaterialAdapter::InvalidateImagingSubprimFromDescendent(
        UsdPrim const& prim,
        UsdPrim const& descendentPrim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        const UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;

    UsdShadeMaterial material(prim);
    if (!material) {
        return result;
    }

    // Find which terminal (if any) we should dirty
    for (UsdShadeOutput& output : material.GetOutputs()) {
        for (UsdShadeConnectionSourceInfo& connection :
             output.GetConnectedSources()) {
            if (_IsConnectionDirty(
                    descendentPrim, properties, material, connection)) {
                result.insert(_CreateTerminalLocator(output.GetBaseName()));
            }
        }
    }

    // Otherwise dirty our whole material
    if (result.IsEmpty()) {
        result.insert(HdMaterialSchema::GetDefaultLocator());
    }

    return result;
}

UsdImagingPrimAdapter::PopulationMode
UsdImagingMaterialAdapter::GetPopulationMode()
{
    return RepresentsSelfAndDescendents;
}

bool
UsdImagingMaterialAdapter::IsSupported(UsdImagingIndexProxy const* index) const
{
    return index->IsSprimTypeSupported(HdPrimTypeTokens->material);
}

SdfPath
UsdImagingMaterialAdapter::Populate(
    UsdPrim const& prim,
    UsdImagingIndexProxy* index,
    UsdImagingInstancerContext const* instancerContext)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    // Since material are populated by reference, they need to take care not to
    // be populated multiple times.
    SdfPath cachePath = prim.GetPath();
    if (index->IsPopulated(cachePath)) {
        return cachePath;
    }

    UsdShadeMaterial material(prim);
    if (!material) return SdfPath::EmptyPath();

    // Skip materials that do not match renderDelegate supported types.
    // XXX We can further improve filtering by combining the below descendants
    // gather and validate the Sdr node types are supported by render delegate.
    const TfTokenVector contextVector = _GetMaterialRenderContexts();
    UsdShadeShader surface = material.ComputeSurfaceSource(contextVector);
    if (!surface) {
        UsdShadeShader volume = material.ComputeVolumeSource(contextVector);
        if (!volume) return SdfPath::EmptyPath();
    }

    index->InsertSprim(HdPrimTypeTokens->material,
                       cachePath,
                       prim, shared_from_this());
    HD_PERF_COUNTER_INCR(UsdImagingTokens->usdPopulatedPrimCount);

    // Also register dependencies on behalf of any descendent
    // UsdShadeShader prims, since they are consumed to
    // create the material network. Note that if the material is an instance
    // prim we want dependencies on the descendants inside the prototype...
    UsdPrim ancestor = prim;
    if (prim.IsInstance()) {
        ancestor = prim.GetPrototype();
        index->AddDependency(cachePath, ancestor);
    }
    for (UsdPrim const& child: ancestor.GetDescendants()) {
        if (child.IsA<UsdShadeShader>()) {
            index->AddDependency(cachePath, child);
        }
    }

    return cachePath;
}

/* virtual */
void
UsdImagingMaterialAdapter::TrackVariability(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdDirtyBits* timeVaryingBits,
    UsdImagingInstancerContext const*
    instancerContext) const
{
    TRACE_FUNCTION();
    UsdShadeMaterial material(prim);
    if (!material) {
        TF_RUNTIME_ERROR("Expected material prim at <%s> to be of type "
                         "'UsdShadeMaterial', not type '%s'; ignoring",
                         prim.GetPath().GetText(),
                         prim.GetTypeName().GetText());
        return;
    }

    const TfTokenVector contextVector = _GetMaterialRenderContexts();
    if (UsdShadeShader s = material.ComputeSurfaceSource(contextVector)) {
        if (UsdImagingIsHdMaterialNetworkTimeVarying(s.GetPrim())) {
            *timeVaryingBits |= HdMaterial::DirtyResource;
            return;
        }
        // Only check if displacement is timeVarying if we also have a surface 
        if (UsdShadeShader d = 
                material.ComputeDisplacementSource(contextVector)) {
            if (UsdImagingIsHdMaterialNetworkTimeVarying(d.GetPrim())) {
                *timeVaryingBits |= HdMaterial::DirtyResource;
            }
        }
        return;
    }

    if (UsdShadeShader v = material.ComputeVolumeSource(contextVector)) {
        if (UsdImagingIsHdMaterialNetworkTimeVarying(v.GetPrim())) {
            *timeVaryingBits |= HdMaterial::DirtyResource;
        }
        return;
    }
}

/* virtual */
void
UsdImagingMaterialAdapter::UpdateForTime(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    UsdImagingInstancerContext const*
    instancerContext) const
{
}

/* virtual */
HdDirtyBits
UsdImagingMaterialAdapter::ProcessPropertyChange(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& propertyName)
{
    if (propertyName == UsdGeomTokens->visibility) {
        // Materials aren't affected by visibility
        return HdChangeTracker::Clean;
    }

    // The only meaningful change is to dirty the computed resource,
    // an HdMaterialNetwork.
    return HdMaterial::DirtyResource;
}

/* virtual */
void
UsdImagingMaterialAdapter::MarkDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdDirtyBits dirty,
    UsdImagingIndexProxy* index)
{
    // If this is invoked on behalf of a Shader prim underneath a
    // Material prim, walk up to the enclosing Material.
    SdfPath materialCachePath = cachePath;
    UsdPrim materialPrim = prim;
    while (materialPrim && !materialPrim.IsA<UsdShadeMaterial>()) {
        materialPrim = materialPrim.GetParent();
        materialCachePath = materialCachePath.GetParentPath();
    }
    if (!TF_VERIFY(materialPrim)) {
        return;
    }

    index->MarkSprimDirty(materialCachePath, dirty);
}


/* virtual */
void
UsdImagingMaterialAdapter::MarkMaterialDirty(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    MarkDirty(prim, cachePath, HdMaterial::DirtyResource, index);
}

/* virtual */
void
UsdImagingMaterialAdapter::_RemovePrim(
    SdfPath const& cachePath,
    UsdImagingIndexProxy* index)
{
    index->RemoveSprim(HdPrimTypeTokens->material, cachePath);
}

/* virtual */
void
UsdImagingMaterialAdapter::ProcessPrimResync(
        SdfPath const& cachePath,
        UsdImagingIndexProxy *index)
{
    // Since we're resyncing a material, we can use the cache path as a
    // usd path.  We need to resync dependents to make sure rprims bound to
    // this material are resynced; this is necessary to make sure the material
    // is repopulated, since we don't directly populate materials.
    SdfPath const& usdPath = cachePath;
    _ResyncDependents(usdPath, index);

    UsdImagingPrimAdapter::ProcessPrimResync(cachePath, index);
}

VtValue 
UsdImagingMaterialAdapter::GetMaterialResource(UsdPrim const &prim,
                                               SdfPath const& cachePath, 
                                               UsdTimeCode time) const
{
    TRACE_FUNCTION();
    if (!prim) {
        TF_RUNTIME_ERROR("Received prim is null.");
        return VtValue();
    }

    if (!_GetSceneMaterialsEnabled()) {
        return VtValue();
    }

    UsdShadeMaterial material(prim);
    if (!material) {
        TF_RUNTIME_ERROR("Expected material prim at <%s> to be of type "
                         "'UsdShadeMaterial', not type '%s'; ignoring",
                         prim.GetPath().GetText(),
                         prim.GetTypeName().GetText());
        return VtValue();
    }

    // Bind the usd stage's resolver context for correct asset resolution.
    ArResolverContextBinder binder(prim.GetStage()->GetPathResolverContext());
    ArResolverScopedCache resolverCache;

    HdMaterialNetworkMap networkMap;

    const TfTokenVector contextVector = _GetMaterialRenderContexts();
    TfTokenVector shaderSourceTypes = _GetShaderSourceTypes();

    if (UsdShadeShader surface = material.ComputeSurfaceSource(contextVector)) {
        UsdImagingBuildHdMaterialNetworkFromTerminal(
            surface.GetPrim(), 
            HdMaterialTerminalTokens->surface,
            shaderSourceTypes,
            contextVector,
            &networkMap,
            time);

        // Only build a displacement materialNetwork if we also have a surface
        if (UsdShadeShader displacement = 
                    material.ComputeDisplacementSource(contextVector)) {
            UsdImagingBuildHdMaterialNetworkFromTerminal(
                displacement.GetPrim(),
                HdMaterialTerminalTokens->displacement,
                shaderSourceTypes,
                contextVector,
                &networkMap,
                time);
        }
    }

    // Only build a volume materialNetwork if we do not have a surface
    else if (UsdShadeShader volume = 
                    material.ComputeVolumeSource(contextVector)) {
        UsdImagingBuildHdMaterialNetworkFromTerminal(
            volume.GetPrim(),
            HdMaterialTerminalTokens->volume,
            shaderSourceTypes,
            contextVector,
            &networkMap,
            time);
    }

    // Collect any 'config' on the Material prim
    VtDictionary configDict;
    for (const auto& prop : material.GetPrim().GetPropertiesInNamespace(
            UsdImagingTokens->configPrefix)) {
        const auto& attr = prop.As<UsdAttribute>();
        if (!attr) {
            continue;
        }

        std::string name = attr.GetName().GetString();
        std::pair<std::string, bool> result =
            SdfPath::StripPrefixNamespace(name, UsdImagingTokens->configPrefix);
        name = result.first;

        VtValue value;
        attr.Get(&value);

        configDict.insert({name, value});
    }
    networkMap.config = configDict;

    return VtValue(networkMap);
}


PXR_NAMESPACE_CLOSE_SCOPE
