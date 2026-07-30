//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "hdPrman/renderPassPruneSceneIndexPlugin.h"

#if PXR_VERSION >= 2408

#include "hdPrman/tokens.h"

#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/renderPassPruneSceneIndex.h"

#include "pxr/usdImaging/usdImaging/version.h"
#if USD_IMAGING_API_VERSION >= 26
#define HDPRMAN_DISABLE_RENDERPASS_PRUNE_PLUGIN
#endif

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdPrman_RenderPassPruneSceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry
        ::Define<HdPrman_RenderPassPruneSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Run after the scene is assembled and scene globals are authored, but
    // before procedurals are expanded.
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 1;

    for (auto const& pluginDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            pluginDisplayName,
            _tokens->sceneIndexPluginName,
            nullptr, // No input args.
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
}

HdPrman_RenderPassPruneSceneIndexPlugin::
HdPrman_RenderPassPruneSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr
HdPrman_RenderPassPruneSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdsiRenderPassPruneSceneIndex::New(inputScene);
}

bool
HdPrman_RenderPassPruneSceneIndexPlugin::_IsEnabled(
    const HdContainerDataSourceHandle &inputArgs) const
{
    // If the UsdImaging_RenderPassPruneSceneIndexPlugin is available, use
    // that instead.
#if defined(HDPRMAN_DISABLE_RENDERPASS_PRUNE_PLUGIN)
    return false;
#else
    return true;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_VERSION >= 2408
