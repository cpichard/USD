//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/imaging/hd/version.h"

// There was no hdsi/version.h before this HD_API_VERSION.
#if HD_API_VERSION >= 58 

#include "pxr/imaging/hdsi/version.h"

#if HDSI_API_VERSION >= 13
#define HDPRMAN_USE_LIGHT_LINKING_SCENE_INDEX
#endif

#if HDSI_API_VERSION >= 21
#define HDPRMAN_CONFIGURE_PREDICATE_LIBRARY
#include "pxr/usdImaging/usdImaging/collectionPredicateLibrary.h"
#endif

#endif // #if HD_API_VERSION >= 58

#ifdef HDPRMAN_USE_LIGHT_LINKING_SCENE_INDEX

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/lightLinkingSceneIndex.h"

#include "pxr/base/tf/envSetting.h"

#include "hdPrman/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "HdPrman_LightLinkingSceneIndexPlugin"))
);

TF_DEFINE_ENV_SETTING(HDPRMAN_ENABLE_LIGHT_LINKING_SCENE_INDEX, true, 
    "Enable registration for the light linking scene index.");

////////////////////////////////////////////////////////////////////////////////
// Plugin registration
////////////////////////////////////////////////////////////////////////////////

class HdPrman_LightLinkingSceneIndexPlugin;

TF_REGISTRY_FUNCTION(TfType)
{
    HdSceneIndexPluginRegistry::Define<HdPrman_LightLinkingSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    // Safe to register the plugin always. If the env var isn't set, the plugin
    // won't be considered when creating the scene index plugin chain.
    //
    // XXX Picking an arbitrary phase for now. If a procedural were to
    //     generate light prims, we'd want this to be after it.
    //     HdGpSceneIndexPlugin::GetInsertionPhase() currently returns 2.
    //
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 4;
    for( auto const& rendererDisplayName : HdPrman_GetPluginDisplayNames()) {
        HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
            rendererDisplayName,
            _tokens->sceneIndexPluginName,
            /* inputArgs = */ nullptr,
            insertionPhase,
            HdSceneIndexPluginRegistry::InsertionOrderAtStart);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Scene Index Implementation
////////////////////////////////////////////////////////////////////////////////

static HdContainerDataSourceHandle
_ConfigureInputArgs(
    const HdContainerDataSourceHandle &pluginInputArgs)
{
    // XXX Update args to provide the list of geometry and light types
    //     supported by hdPrman instead of using the fallback behavior in
    //     HdsiLightLinkingSceneIndex that uses the hardcoded tokens in hd.
    return pluginInputArgs;
}

class HdPrman_LightLinkingSceneIndexPlugin :
    public HdSceneIndexPlugin
{
public:
    HdPrman_LightLinkingSceneIndexPlugin() = default;

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override
    {
        // We don't expect this to called when the plugin is disabled.
        if (!TF_VERIFY(_IsEnabled(inputArgs))) {
            return inputScene;
        }

        #ifdef HDPRMAN_CONFIGURE_PREDICATE_LIBRARY
        // UsdImaging's predicate library supports the evaluation of USD
        // predicates. The assumption here is that light linking collections
        // are authored in USD (via UsdLuxLightAPI, LightFilter) and thus can
        // use only the USD predicate expressions.
        // So, we don't use the Hydra (core) predicate library returned by
        // HdGetCollectionPredicateLibrary() here.
        //
        return HdsiLightLinkingSceneIndex::New(
            inputScene, _ConfigureInputArgs(inputArgs),
            UsdImagingGetCollectionPredicateLibrary());
#else
        return HdsiLightLinkingSceneIndex::New(
            inputScene, _ConfigureInputArgs(inputArgs));
#endif
    }

    bool _IsEnabled(
        const HdContainerDataSourceHandle &/* inputArgs */) const override
    {
        static bool enabled =
            TfGetEnvSetting(HDPRMAN_ENABLE_LIGHT_LINKING_SCENE_INDEX);
        return enabled;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDPRMAN_USE_LIGHT_LINKING_SCENE_INDEX
