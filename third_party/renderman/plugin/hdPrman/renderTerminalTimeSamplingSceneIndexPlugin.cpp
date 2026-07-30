//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "hdPrman/renderTerminalTimeSamplingSceneIndexPlugin.h"

#include "hdPrman/tokens.h"

#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#if PXR_VERSION >= 2505 && PXR_VERSION < 2603
#include "pxr/imaging/hd/sampleFilterSchema.h"
#include "pxr/imaging/hd/displayFilterSchema.h"
#include "pxr/imaging/hd/integratorSchema.h"
#endif
#include "pxr/imaging/hd/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((__renderTerminalTimeSampling, "__renderTerminalTimeSampling"))
    ((sceneIndexPluginName, "HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<
        HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    for (const auto& pluginDisplayName : HdPrman_GetPluginDisplayNames())
    {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            pluginDisplayName,
            _tokens->sceneIndexPluginName,
            nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtEnd);
    }
}

#if PXR_VERSION >= 2505 && PXR_VERSION < 2603

namespace {

//
// _RenderTerminalTimeSamplingSceneIndex
//

TF_DECLARE_REF_PTRS(_RenderTerminalTimeSamplingSceneIndex);

class _RenderTerminalTimeSamplingSceneIndex : public HdSingleInputFilteringSceneIndexBase
{
public:
    static _RenderTerminalTimeSamplingSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr& inputSceneIndex)
    {
        return TfCreateRefPtr(new _RenderTerminalTimeSamplingSceneIndex(inputSceneIndex));
    }

    HdSceneIndexPrim GetPrim(const SdfPath& primPath) const override
    {
        HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

        if (!prim.dataSource) {
            return prim;
        }

        TfToken schemaToken;
        if (prim.primType == HdPrimTypeTokens->sampleFilter) {
            schemaToken = HdSampleFilterSchema::GetSchemaToken();
        }
        else if (prim.primType == HdPrimTypeTokens->displayFilter) {
            schemaToken = HdDisplayFilterSchema::GetSchemaToken();
        }
        else if (prim.primType == HdPrimTypeTokens->integrator) {
            schemaToken = HdIntegratorSchema::GetSchemaToken();
        }
        else {
            return prim;
        }

        HdContainerDataSourceHandle timeSampling = HdContainerDataSource::Cast(
            prim.dataSource->Get(_tokens->__renderTerminalTimeSampling));

        if (!timeSampling) {
            return prim;
        }

        prim.dataSource = HdOverlayContainerDataSource::New(
            HdRetainedContainerDataSource::New(schemaToken, timeSampling),
            prim.dataSource);

        return prim;
    }

    SdfPathVector GetChildPrimPaths(const SdfPath& primPath) const override
    {
        return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
    }

protected:
    _RenderTerminalTimeSamplingSceneIndex(const HdSceneIndexBaseRefPtr& inputSceneIndex)
        : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
    {
    }

    ~_RenderTerminalTimeSamplingSceneIndex() = default;

    void _PrimsAdded(const HdSceneIndexBase& sender,
                     const HdSceneIndexObserver::AddedPrimEntries& entries) override
    {
        _SendPrimsAdded(entries);
    }

    void _PrimsRemoved(const HdSceneIndexBase& sender,
                       const HdSceneIndexObserver::RemovedPrimEntries& entries) override
    {
        _SendPrimsRemoved(entries);
    }

    void _PrimsDirtied(const HdSceneIndexBase& sender,
                       const HdSceneIndexObserver::DirtiedPrimEntries& entries) override
    {
        _SendPrimsDirtied(entries);
    }
};

} // anonymous namespace

#endif // PXR_VERSION

//
// HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin
//

HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin::
HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdPrman_RenderTerminalTimeSamplingSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr& inputScene,
    const HdContainerDataSourceHandle& inputArgs)
{
#if PXR_VERSION >= 2505 && PXR_VERSION < 2603
    return _RenderTerminalTimeSamplingSceneIndex::New(inputScene);
#else
    return inputScene;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
