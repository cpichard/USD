//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "hdPrman/portalLightResolvingSceneIndexPlugin.h"

#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix3d.h"
#if PXR_VERSION >= 2311
#include "pxr/base/tf/hash.h"
#endif
#include "pxr/base/tf/staticTokens.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceMaterialNetworkInterface.h"
#include "pxr/imaging/hd/dependencySchema.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/version.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/usd/sdf/assetPath.h"
#include "hdPrman/tokens.h"

#if PXR_VERSION <= 2308
#include <boost/functional/hash.hpp>
#endif

#include <algorithm>
#include <iterator>
#include <set>
#include <unordered_map>
#include <unordered_set>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (PortalLight)
    (PxrPortalLight)
    ((sceneIndexPluginName, "HdPrman_PortalLightResolvingSceneIndexPlugin"))

    // light schema tokens
    (domeOffset)

    // material network tokens
    (color)
    ((colorMap,                "texture:file"))
    ((domeColorMap,            "ri:light:domeColorMap"))
    (exposure)
    (intensity)
    ((intensityMult,           "ri:light:intensityMult"))
    ((exposureAdjust,          "ri:light:exposureAdjust"))
    ((portalName,              "ri:light:portalName"))
    ((portalToDome,            "ri:light:portalToDome"))
    ((tint,                    "ri:light:tint"))

    // render context / material network selector
    ((renderContext, "ri"))
);

// Material parameters for which we should overwrite unauthored values
// on a portal light with authored values from the portal's dome light.
TF_DEFINE_PRIVATE_TOKENS(
    _inheritedAttrTokens,

    (colorEnableTemperature)
    ((colorMapGamma,           "ri:light:colorMapGamma"))
    ((colorMapSaturation,      "ri:light:colorMapSaturation"))
    (colorTemperature)
    (diffuse)
    ((importanceMultiplier,    "ri:light:importanceMultiplier"))
    ((shadowColor,             "shadow:color"))
    ((shadowDistance,          "shadow:distance"))
    ((shadowEnable,            "shadow:enable"))
    ((shadowFalloff,           "shadow:falloff"))
    ((shadowFalloffGamma,      "shadow:falloffGamma"))
    (specular)
    ((thinShadow,              "ri:light:thinShadow"))
    ((traceLightPaths,         "ri:light:traceLightPaths"))
    ((visibleInRefractionPath, "ri:light:visibleInRefractionPath"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<
        HdPrman_PortalLightResolvingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // We need an insertion point that's *after* general material resolve.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 115;

    for(const auto& pluginDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            pluginDisplayName,
            _tokens->sceneIndexPluginName,
            nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
}

#if PXR_VERSION >= 2502 // only from H21

namespace {

HdContainerDataSourceHandle
_GetMaterialDataSource(const HdContainerDataSourceHandle &primDataSource)
{
    return HdMaterialSchema::GetFromParent(primDataSource)
            .GetMaterialNetwork(_tokens->renderContext)
#if HD_API_VERSION >= 63
            .GetContainer()
#endif
        ;
}

bool
_IsPortalLight(const HdSceneIndexPrim& prim, const SdfPath& primPath)
{
    const HdContainerDataSourceHandle matDataSource =
        _GetMaterialDataSource(prim.dataSource);
    HdDataSourceMaterialNetworkInterface matInterface(primPath, matDataSource,
                                                      prim.dataSource);

    const auto matTerminal =
        matInterface.GetTerminalConnection(HdMaterialTerminalTokens->light);
    const auto nodeName = matTerminal.second.upstreamNodeName;

    const TfToken nodeTypeName = matInterface.GetNodeType(nodeName);

    // We accept either the generic UsdLux "PortalLight" or the
    // RenderMan-specific "PxrPortalLight" here.  (The former can
    // occur when using Hydra render index emulation.  In that
    // setup, the scene index chain runs prior to applying the
    // renderContextNodeIdentifier to individual nodes.)
    return (nodeTypeName == _tokens->PxrPortalLight
        || nodeTypeName == _tokens->PortalLight);
}

// Helper function to extract a value from a light data source.
template <typename T>
T
_GetLightData(
    const HdContainerDataSourceHandle& primDataSource,
    const TfToken& name,
    const T defaultValue=T())
{
    if (auto lightSchema = HdLightSchema::GetFromParent(primDataSource)) {
        if (auto dataSource = HdTypedSampledDataSource<T>::Cast(
                lightSchema.GetContainer()->Get(name))) {
            return dataSource->GetTypedValue(0.0f);
        }
    }

    return defaultValue;
}

TfToken
_ResolveLinking(
    const HdContainerDataSourceHandle& portalDataSource,
    const HdContainerDataSourceHandle& domeDataSource,
    const TfToken& linkType)
{
    // Light/shadow linking set directly on the portal light wins, if present.
    // Otherwise, fall back to linking set on the dome light.
    TfToken linking = _GetLightData<TfToken>(portalDataSource, linkType);
    if (linking.IsEmpty()) {
        linking = _GetLightData<TfToken>(domeDataSource, linkType);
    }
    return linking;
}

SdfPathVector
_GetPortalPaths(const HdContainerDataSourceHandle& primDataSource)
{
    return _GetLightData<SdfPathVector>(primDataSource, HdTokens->portals);
}

SdfPathVector
_GetLightFilterPaths(const HdContainerDataSourceHandle& primDataSource)
{
    return _GetLightData<SdfPathVector>(primDataSource, HdTokens->filters);
}

std::string
_GetPortalName(
    const std::string& domeColorMap,
    const GfMatrix4d& domeXform,
    const GfMatrix4d& portalXform)
{
#if PXR_VERSION >= 2311
    size_t hashValue = TfHash::Combine(
        domeColorMap, 
        domeXform.ExtractRotationMatrix(),
        portalXform.ExtractRotationMatrix());
#else
    size_t hashValue = 0;
    boost::hash_combine(hashValue, domeColorMap);
    boost::hash_combine(hashValue, domeXform.ExtractRotationMatrix());
    boost::hash_combine(hashValue, portalXform.ExtractRotationMatrix());
#endif

    return std::to_string(hashValue);
}

// Prim-level data source for dome lights.
//
class HdPrman_DomeLightDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(HdPrman_DomeLightDataSource);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

    HdPrman_DomeLightDataSource(
        SdfPath const& domePrimPath,
        HdContainerDataSourceHandle const& domePrimDataSource)
     : _domePrimPath(domePrimPath)
     , _domePrimDataSource(domePrimDataSource)
    {
    }

    const SdfPath _domePrimPath;
    const HdContainerDataSourceHandle _domePrimDataSource;
};

TfTokenVector
HdPrman_DomeLightDataSource::GetNames()
{
    if (!_domePrimDataSource) {
        return TfTokenVector();
    }
    TfTokenVector names = _domePrimDataSource->GetNames();
    // Add HdDependenciesSchema.
    if (std::find(names.cbegin(), names.cend(),
                  HdDependenciesSchema::GetSchemaToken()) == names.cend()) {
        names.push_back(HdDependenciesSchema::GetSchemaToken());
    }
    return names;
}

HdDataSourceBaseHandle
HdPrman_DomeLightDataSource::Get(const TfToken &name)
{
    if (!_domePrimDataSource) {
        return nullptr;
    }

    // XXX -- Maybe we should also clear the filters in the dome's light data
    //        source. These filters will apply directly to the dome's portals
    //        rather than to the dome (which is muted anyway). However, it
    //        doesn't appear to be necessary to remove filters from the dome,
    //        and might require us to store and update dome filter paths in
    //        the scene index class (lest they be cleared prematurely here)
    //        so we won't bother for now.

    // Domes with portals are not visible.
    if (name == HdVisibilitySchema::GetSchemaToken()) {
        if (!_GetPortalPaths(_domePrimDataSource).empty()) {
            static const HdContainerDataSourceHandle invisDs =
                HdVisibilitySchema::Builder()
                    .SetVisibility(
                        HdRetainedTypedSampledDataSource<bool>::New(false))
                    .Build();
            return invisDs;
        }
    }

    HdDataSourceBaseHandle ds = _domePrimDataSource->Get(name);

    if (name == HdDependenciesSchema::GetSchemaToken()) {
        // Dome light visibility depends on its portals.
        static const std::vector<TfToken> names = {
            TfToken("visibility_depOn_portals") };
        const std::vector<HdDataSourceBaseHandle> sources = {
            HdDependencySchema::Builder()
                .SetDependedOnPrimPath(
                    HdRetainedTypedSampledDataSource<SdfPath>::New(
                        _domePrimPath))
                .SetDependedOnDataSourceLocator(
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdLightSchema::GetDefaultLocator()
                            .Append(HdTokens->portals)))
                .SetAffectedDataSourceLocator(
                    HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                        HdVisibilitySchema::GetDefaultLocator()))
                .Build() };
        const HdContainerDataSourceHandle overlayDs =
            HdDependenciesSchema::BuildRetained(
                names.size(), names.data(), sources.data());
        if (auto dependenciesDs = HdContainerDataSource::Cast(ds)) {
            return HdOverlayContainerDataSource::New(overlayDs, dependenciesDs);
        }
        return overlayDs;
    }

    return ds;
}

// Prim-level data source for portal lights.
//
class HdPrman_PortalLightDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(HdPrman_PortalLightDataSource);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

    HdPrman_PortalLightDataSource(
        SdfPath const& portalPrimPath,
        HdContainerDataSourceHandle const& portalPrimDataSource,
        SdfPath const& domePrimPath,
        HdContainerDataSourceHandle const& domePrimDataSource)
     : _portalPrimPath(portalPrimPath)
     , _portalPrimDataSource(portalPrimDataSource)
     , _domePrimPath(domePrimPath)
     , _domePrimDataSource(domePrimDataSource)
    {
    }

    HdDataSourceBaseHandle _GetMaterial();
    HdDataSourceBaseHandle _GetLight();
    HdDataSourceBaseHandle _GetDependencies();

    const SdfPath _portalPrimPath;
    const HdContainerDataSourceHandle _portalPrimDataSource;
    const SdfPath _domePrimPath;
    const HdContainerDataSourceHandle _domePrimDataSource;
};

TfTokenVector
HdPrman_PortalLightDataSource::GetNames()
{
    if (!_portalPrimDataSource) {
        return TfTokenVector();
    }
    TfTokenVector names = _portalPrimDataSource->GetNames();
    // Add HdDependenciesSchema.
    if (std::find(names.cbegin(), names.cend(),
                  HdDependenciesSchema::GetSchemaToken()) == names.cend()) {
        names.push_back(HdDependenciesSchema::GetSchemaToken());
    }
    return names;
}

HdDataSourceBaseHandle
HdPrman_PortalLightDataSource::Get(const TfToken &name)
{
    if (name == HdMaterialSchema::GetSchemaToken()) {
        return _GetMaterial();
    }
    if (name == HdLightSchema::GetSchemaToken()) {
        return _GetLight();
    }
    if (name == HdDependenciesSchema::GetSchemaToken()) {
        return _GetDependencies();
    }
    return _portalPrimDataSource->Get(name);
}

HdDataSourceBaseHandle
HdPrman_PortalLightDataSource::_GetMaterial()
{
    if (!_domePrimDataSource) {
        // Without a dome prim there's nothing to do here.
        return _portalPrimDataSource->Get(HdMaterialSchema::GetSchemaToken());
    }

    // Get data sources for the associated dome light.
    // -------------------------------------------------------------------------
    const HdContainerDataSourceHandle domeMatDataSource =
        _GetMaterialDataSource(_domePrimDataSource);
    HdDataSourceMaterialNetworkInterface domeMatInterface(
        _domePrimPath, domeMatDataSource, _domePrimDataSource);
    const auto domeMatTerminal =
        domeMatInterface.GetTerminalConnection(HdMaterialTerminalTokens->light);

    HdXformSchema domeXformSchema =
        HdXformSchema::GetFromParent(_domePrimDataSource);

    // Get some relevant values from the dome light's data sources.
    // -------------------------------------------------------------------------
    const auto getDomeMatVal =
        [&domeMatInterface, &domeMatTerminal](const TfToken& paramName){
            return domeMatInterface.GetNodeParameterValue(
                domeMatTerminal.second.upstreamNodeName, paramName);
        };

    // Note that the attribute name for colorMap is "texture:file";
    // That is the attribute name used by USD, and reflected in the
    // RenderMan light plugin args files for most light types -- with
    // the exception of portals, which is why map it to domeColorMap.
    const VtValue domeColorMapVal  = getDomeMatVal(_tokens->colorMap);
    const VtValue domeColorVal     = getDomeMatVal(_tokens->color);
    const VtValue domeIntensityVal = getDomeMatVal(_tokens->intensity);
    const VtValue domeExposureVal  = getDomeMatVal(_tokens->exposure);

    // Use the resolved path of the asset if available, otherwise
    // pass through the original asset path.  This is important in
    // order to support RenderMan's texture plugin system, which
    // uses texture paths of the form "rtxplugin:...".
    const SdfAssetPath domeColorMapAssetPath =
        domeColorMapVal.IsHolding<SdfAssetPath>()
        ? domeColorMapVal.UncheckedGet<SdfAssetPath>() : SdfAssetPath();
    const std::string domeColorMap =
        domeColorMapAssetPath.GetResolvedPath().empty()
        ? domeColorMapAssetPath.GetAssetPath()
        : domeColorMapAssetPath.GetResolvedPath();

    const auto domeColor     = domeColorVal.GetWithDefault(GfVec3f(1.0f));
    const auto domeIntensity = domeIntensityVal.GetWithDefault(1.0f);
    const auto domeExposure  = domeExposureVal.GetWithDefault(0.0f);

    // domeOffset exists in the light schema, not in the material netowrk.
    // See UsdImaging/domeLight_1_Adapter.cpp for an example provider,
    // and hdPrman/light.cpp for where this is used.
    GfMatrix4d domeOffset =
        _GetLightData<GfMatrix4d>(_domePrimDataSource, _tokens->domeOffset,
                                  GfMatrix4d(1.0));

    GfMatrix4d domeXform;
    if (const auto origDomeXform = domeXformSchema.GetMatrix()) {
        // This matrix encodes a -90 deg rotation about the x-axis and a 90 deg
        // rotation about the y-axis, which correspond to the transform needed
        // for prman to convert right handed to left handed.
        static const GfMatrix4d domeXformAdjustment( 0.0, 0.0, -1.0, 0.0,
                                                    -1.0, 0.0,  0.0, 0.0,
                                                     0.0, 1.0,  0.0, 0.0,
                                                     0.0, 0.0,  0.0, 1.0);
        domeXform = domeXformAdjustment * domeOffset *
            origDomeXform->GetTypedValue(0.0f);
    }
    else {
        domeXform.SetIdentity();
    }

    // Get data sources for the portal light.
    // -------------------------------------------------------------------------
    const HdContainerDataSourceHandle portalMatDataSource =
        _GetMaterialDataSource(_portalPrimDataSource);
    HdDataSourceMaterialNetworkInterface portalMatInterface(
        _portalPrimPath, portalMatDataSource, _portalPrimDataSource);
    const auto portalMatTerminal =
        portalMatInterface.GetTerminalConnection(
            HdMaterialTerminalTokens->light);

    HdXformSchema portalXformSchema =
        HdXformSchema::GetFromParent(_portalPrimDataSource);

    // Get some relevant values from the portal light's data sources.
    // -------------------------------------------------------------------------
    const auto getPortalMatVal =
        [&portalMatInterface, &portalMatTerminal](const TfToken& paramName){
            return portalMatInterface.GetNodeParameterValue(
                portalMatTerminal.second.upstreamNodeName, paramName);
        };

    const VtValue portalTintVal    = getPortalMatVal(_tokens->tint);
    const VtValue portalIntMultVal = getPortalMatVal(_tokens->intensityMult);
    const VtValue portalExpAdjtVal = getPortalMatVal(_tokens->exposureAdjust);

    const auto portalTint    = portalTintVal.GetWithDefault(GfVec3f(1.0f));
    const auto portalIntMult = portalIntMultVal.GetWithDefault(1.0f);
    const auto portalExpAdj  = portalExpAdjtVal.GetWithDefault(0.0f);

    GfMatrix4d portalXform;
    if (const auto origPortalXform = portalXformSchema.GetMatrix()) {
        // This matrix encodes a 180 deg rotation about the y-axis and a -1
        // scale in y, which correspond to the transform needed for prman to
        // convert right handed to left handed.
        static const GfMatrix4d portalXformAdjustment(-1.0,  0.0,  0.0, 0.0,
                                                       0.0, -1.0,  0.0, 0.0,
                                                       0.0,  0.0, -1.0, 0.0,
                                                       0.0,  0.0,  0.0, 1.0);

        portalXform = portalXformAdjustment *
                      origPortalXform->GetTypedValue(0.0f);
    }
    else {
        portalXform.SetIdentity();
    }

    // Compute new values for the portal's material data source.
    // -------------------------------------------------------------------------
    const auto setPortalParamVal =
        [&portalMatInterface, &portalMatTerminal](
            const TfToken& paramName, const VtValue& value)
        {
            portalMatInterface.SetNodeParameterValue(
                portalMatTerminal.second.upstreamNodeName, paramName, value);
        };

    const auto computedPortalColor = GfCompMult(portalTint, domeColor);
    const auto computedPortalIntensity = portalIntMult * domeIntensity *
                                         powf(2.0f, domeExposure+portalExpAdj);
    const auto computedPortalToDome = portalXform * domeXform.GetInverse();
    const auto computedPortalName = _GetPortalName(domeColorMap, domeXform,
                                                   portalXform);

    setPortalParamVal(_tokens->domeColorMap, VtValue(domeColorMapAssetPath));
    setPortalParamVal(_tokens->color,        VtValue(computedPortalColor));
    setPortalParamVal(_tokens->intensity,    VtValue(computedPortalIntensity));
    setPortalParamVal(_tokens->portalToDome, VtValue(computedPortalToDome));
    setPortalParamVal(_tokens->portalName,   VtValue(computedPortalName));

    // XXX -- We can probably delete the portal's tint and intensityMult params
    //        now, since they're not used by the RenderMan light shader.

    // Directly copy a bunch of other params from the dome to the portal.
    // XXX -- We'd like to do this only for *unauthored* portal params. However,
    //        there's no obvious way to tell which params are user-authored.
    for (const auto& attr: _inheritedAttrTokens->allTokens) {
        setPortalParamVal(attr, getDomeMatVal(attr));
    }

    HdDataSourceBaseHandle updateMat = portalMatInterface.Finish();
    return HdMaterialSchema::BuildRetained(
        1, &_tokens->renderContext, &updateMat);
}

HdDataSourceBaseHandle
HdPrman_PortalLightDataSource::_GetLight()
{
    HdContainerDataSourceHandle lightDs =
        HdContainerDataSource::Cast(
            _portalPrimDataSource->Get( HdLightSchema::GetSchemaToken() ));

    // Compute new values for the portal's light data source.
    // -------------------------------------------------------------------------
    // All we're going to do is copy the light filter paths from the dome's
    // light.filters data source to the portal's light.filters data source.
    // This means that the filter prims will still just exist under the dome
    // and filter xforms will be relative to the dome, not the portal. That
    // xform behavior is expected; it matches what happens in Katana.
    SdfPathVector domeFilters = _GetLightFilterPaths(_domePrimDataSource);
    SdfPathVector allFilters  = _GetLightFilterPaths(_portalPrimDataSource);
    allFilters.insert(allFilters.end(),
                      std::make_move_iterator(domeFilters.begin()),
                      std::make_move_iterator(domeFilters.end()));
    const auto computedFiltersDataSource =
        HdRetainedTypedSampledDataSource<SdfPathVector>::New(allFilters);

    // Resolve light and shadow linking.
    const auto lightLink = _ResolveLinking(
        _portalPrimDataSource, _domePrimDataSource, HdTokens->lightLink);
    const auto shadowLink = _ResolveLinking(
        _portalPrimDataSource, _domePrimDataSource, HdTokens->shadowLink);
    const auto computedLightLinkDataSource =
        HdRetainedTypedSampledDataSource<TfToken>::New(lightLink);
    const auto computedShadowLinkDataSource =
        HdRetainedTypedSampledDataSource<TfToken>::New(shadowLink);

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
        HdTokens->filters,    computedFiltersDataSource,
        HdTokens->lightLink,  computedLightLinkDataSource,
            HdTokens->shadowLink, computedShadowLinkDataSource),
        lightDs);
}

HdDataSourceBaseHandle
HdPrman_PortalLightDataSource::_GetDependencies()
{
    HdContainerDataSourceHandle depsDs =
        HdContainerDataSource::Cast(
            _portalPrimDataSource->Get(
                HdDependenciesSchema::GetSchemaToken() ));

    // Record the dependency of the portal on its dome.
    // (If the dome xform or light parameters change, we need to update
    // the attached portals.)
    static const TfToken depNames[] = { TfToken("dome") };
    const HdDataSourceBaseHandle depDataSources[] = {
        HdDependencySchema::Builder()
            .SetDependedOnPrimPath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(
                    _domePrimPath))
            // Specify a root dependency.
            .SetDependedOnDataSourceLocator(
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDataSourceLocator::EmptyLocator()))
            .SetAffectedDataSourceLocator(
                HdRetainedTypedSampledDataSource<HdDataSourceLocator>::New(
                    HdDataSourceLocator::EmptyLocator()))
            .Build() };
    return HdOverlayContainerDataSource::New(
        HdDependenciesSchema::BuildRetained(
            std::size(depNames), depNames, depDataSources),
        depsDs);
}

//
// _PortalLightResolvingSceneIndex
//

TF_DECLARE_REF_PTRS(_PortalLightResolvingSceneIndex);

// Pixar-only, Prman-specific Hydra scene index to resolve portal lights.
class _PortalLightResolvingSceneIndex
    : public HdSingleInputFilteringSceneIndexBase
{
public:
    static _PortalLightResolvingSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr& inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override;

protected:
    _PortalLightResolvingSceneIndex(
        const HdSceneIndexBaseRefPtr& inputSceneIndex);
    ~_PortalLightResolvingSceneIndex();

    void _PrimsAdded(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase& sender,
        const HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

private:
    SdfPathVector _AddMappingsForDome(const SdfPath& domePrimPath);
    SdfPathVector _RemoveMappingsForDome(const SdfPath& domePrimPath);

private:
    // Map dome light paths to flag indicating presence of associated portals.
    std::unordered_map<SdfPath, bool, SdfPath::Hash> _domesWithPortals;

    // Map portal path to dome path. A previous name for this map was
    // "_portalToDome", but that conflicts with a material param name.
    std::unordered_map<SdfPath, SdfPath, SdfPath::Hash> _portalsToDomes;
};

/* static */
_PortalLightResolvingSceneIndexRefPtr
_PortalLightResolvingSceneIndex::New(
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
{
    return TfCreateRefPtr(
        new _PortalLightResolvingSceneIndex(inputSceneIndex));
}

_PortalLightResolvingSceneIndex::_PortalLightResolvingSceneIndex(
    const HdSceneIndexBaseRefPtr& inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
    // Do nothing
}

_PortalLightResolvingSceneIndex::
~_PortalLightResolvingSceneIndex() = default;

HdSceneIndexPrim 
_PortalLightResolvingSceneIndex::GetPrim(
    const SdfPath& primPath) const
{
    auto prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (prim.primType == HdPrimTypeTokens->light) {
        // Check for portal
        const auto portalIt = _portalsToDomes.find(primPath);
        if (portalIt != _portalsToDomes.end() &&
            _IsPortalLight(prim, primPath)) {
            const auto domePrimPath = portalIt->second;
            HdSceneIndexPrim domePrim =
                _GetInputSceneIndex()->GetPrim(domePrimPath);
            prim.dataSource = HdPrman_PortalLightDataSource::New(
                primPath, prim.dataSource,
                domePrimPath, domePrim.dataSource);
            return prim;
        }
    }

    if (prim.primType == HdPrimTypeTokens->domeLight) {
        prim.dataSource =
            HdPrman_DomeLightDataSource::New(primPath, prim.dataSource);
        return prim;
    }

    return prim;
}

SdfPathVector 
_PortalLightResolvingSceneIndex::GetChildPrimPaths(
    const SdfPath& primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
_PortalLightResolvingSceneIndex::_PrimsAdded(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::AddedPrimEntries& entries)
{    
    if (!_IsObserved()) {
        return;
    }

    for (const auto& entry: entries) {
        if (entry.primType == HdPrimTypeTokens->domeLight) {
            _AddMappingsForDome(entry.primPath);
        }
    }

    _SendPrimsAdded(entries);
}

void 
_PortalLightResolvingSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::RemovedPrimEntries& entries)
{
    if (!_IsObserved()) {
        return;
    }

    for (const auto& entry: entries) {
        if (_domesWithPortals.count(entry.primPath)) {
            _RemoveMappingsForDome(entry.primPath);
        }
    }

    _SendPrimsRemoved(entries);
}

void
_PortalLightResolvingSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase& sender,
    const HdSceneIndexObserver::DirtiedPrimEntries& entries)
{
    if (!_IsObserved()) {
        return;
    }

    static const auto& lightLocator    = HdLightSchema::GetDefaultLocator();
    static const auto& materialLocator = HdMaterialSchema::GetDefaultLocator();
    static const auto& xformLocator    = HdXformSchema::GetDefaultLocator();
    static const HdDataSourceLocatorSet portalLocators =    
        {materialLocator, lightLocator, xformLocator};

    HdSceneIndexObserver::DirtiedPrimEntries dirtied;
    SdfPathSet dirtiedPortals;
    for (const auto& entry: entries) {
        auto domeIt = _domesWithPortals.find(entry.primPath);
        if (domeIt != _domesWithPortals.end()) {
            // entry.primPath is a known dome
            if (entry.dirtyLocators.Intersects(lightLocator)) {
                // The dome's portals may have changed.
                auto removedPortals =
                    _RemoveMappingsForDome(entry.primPath);
                _AddMappingsForDome(entry.primPath);

                dirtiedPortals.insert(
                    std::make_move_iterator(removedPortals.begin()),
                    std::make_move_iterator(removedPortals.end()));
            }
            if (entry.dirtyLocators.Intersects(portalLocators)) {
                // Assume that the dome's portals should be considered dirty.
                for (const auto& [portalPath, domePath]: _portalsToDomes) {
                    if (domePath == entry.primPath) {
                        dirtiedPortals.insert(portalPath);
                    }
                }
            }
            dirtied.push_back(entry);
        }
        else if (_portalsToDomes.count(entry.primPath) &&
                 entry.dirtyLocators.Intersects(xformLocator)) {
            // An xform change will affect portalToDome and portalName,
            // so we need to make sure the material data source gets dirtied.
            HdSceneIndexObserver::DirtiedPrimEntry newEntry(entry);
            newEntry.dirtyLocators.insert(materialLocator);
            dirtied.push_back(newEntry);
        }
        else {
            dirtied.push_back(entry);
        }
    }

    // Check for elements of "dirtiedPortals" that are already in "dirtied".
    for (auto& entry: dirtied) {
        if (dirtiedPortals.find(entry.primPath) != dirtiedPortals.end()) {
            // If the portal is already in the dirtied vector, we don't want to
            // add it again.
            dirtiedPortals.erase(entry.primPath);
            // We do, however, need to invalidate the portal data sources.
            entry.dirtyLocators.insert(portalLocators);
        }
    }

    for (const auto& portalPath: dirtiedPortals) {
        dirtied.emplace_back(portalPath, portalLocators);
    }

    _SendPrimsDirtied(dirtied);
}

SdfPathVector
_PortalLightResolvingSceneIndex::_AddMappingsForDome(
    const SdfPath& domePrimPath)
{
    const auto domePrim = _GetInputSceneIndex()->GetPrim(domePrimPath);

    if (domePrim.primType != HdPrimTypeTokens->domeLight) {
        // Caller should have already confirmed this is a dome.
        TF_CODING_ERROR("_AddMappingsForDome invoked for non-"
                        "domeLight path <%s>", domePrimPath.GetText());
        return SdfPathVector();
    }
    
    SdfPathVector portalPaths = _GetPortalPaths(domePrim.dataSource);

    _domesWithPortals[domePrimPath] = !portalPaths.empty();

    for (const auto& portalPath: portalPaths) {
        const auto it = _portalsToDomes.insert({portalPath,domePrimPath}).first;
        if (it->second != domePrimPath) {
            TF_WARN("Failed to register <%s> as a portal light for <%s>. "
                    "The portal is already in use with <%s> and cannot be "
                    "reused with another dome light.",
                    portalPath.GetText(), domePrimPath.GetText(),
                    it->second.GetText());
        }
    }
    return portalPaths;
}

SdfPathVector
_PortalLightResolvingSceneIndex::_RemoveMappingsForDome(
    const SdfPath& domePrimPath)
{
    SdfPathVector portalPaths;
    auto domeIt = _domesWithPortals.find(domePrimPath);
    if (domeIt != _domesWithPortals.end()) {
        const bool domeHasPortals = domeIt->second;
        if (domeHasPortals) {
            // We successfully found a dome prim to erase, so remove the
            // corresponding _portalsToDomes entries.
            for (auto it = _portalsToDomes.begin(); it != _portalsToDomes.end();) {
                if (it->second == domePrimPath) {
                    portalPaths.push_back(it->first);
                    it = _portalsToDomes.erase(it);
                }
                else {
                    it++;
                }
            }
        }
        _domesWithPortals.erase(domeIt);
    }
    return portalPaths;
}

} // anonymous namespace

#endif

//
// HdPrman_PortalLightResolvingSceneIndexPlugin
//

HdPrman_PortalLightResolvingSceneIndexPlugin::
HdPrman_PortalLightResolvingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdPrman_PortalLightResolvingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& inputArgs)
{
#if PXR_VERSION >= 2502
    return _PortalLightResolvingSceneIndex::New(inputScene);
#else
    return inputScene;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
