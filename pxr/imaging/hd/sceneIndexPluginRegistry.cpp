//
// Copyright 2021 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/imaging/hd/sceneIndexUtil.h"

#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/iterator.h"
#include "pxr/base/tf/refPtr.h"
#include "pxr/base/tf/weakPtr.h"

#include <string>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(HdSceneIndexPluginRegistry);

TF_DEFINE_PUBLIC_TOKENS(HdSceneIndexPluginRegistryTokens,
    HDSCENEINDEXPLUGINREGISTRY_TOKENS);


HdSceneIndexPluginRegistry &
HdSceneIndexPluginRegistry::GetInstance()
{
    return TfSingleton<HdSceneIndexPluginRegistry>::GetInstance();
}

HdSceneIndexPluginRegistry::HdSceneIndexPluginRegistry()
 : HfPluginRegistry(TfType::Find<HdSceneIndexPlugin>())
{
    TfSingleton<HdSceneIndexPluginRegistry>::SetInstanceConstructed(*this);
    TfRegistryManager::GetInstance().SubscribeTo<HdSceneIndexPlugin>();

    // Force discovery at instantiation time
    std::vector<HfPluginDesc> descs;
    HdSceneIndexPluginRegistry::GetInstance().GetPluginDescs(&descs);
}

HdSceneIndexPluginRegistry::~HdSceneIndexPluginRegistry() = default;

HdSceneIndexPlugin *
HdSceneIndexPluginRegistry::_GetSceneIndexPlugin(const TfToken &pluginId)
{
    return static_cast<HdSceneIndexPlugin*>(GetPlugin(pluginId));
}

void HdSceneIndexPluginRegistry::_PopulateMetadata(
    const HdSceneIndexBaseRefPtr& sceneIndex,
    const PluginInsertionMetadata&& metadata,
    _VisitedSet&& visited)
{
    if (visited.count(sceneIndex)) {
        return;
    }
    TfWeakPtr<HdSceneIndexBase> weakSI = TfCreateWeakPtr(&(*sceneIndex));
    _metadata[weakSI] = metadata;
    visited.insert(sceneIndex);

    if (auto *const encapsulatingSI =
        HdEncapsulatingSceneIndexBase::Cast(sceneIndex)) {
        for (const auto& scene : encapsulatingSI->GetEncapsulatedScenes()) {
            _PopulateMetadata(
                scene,
                std::forward<const PluginInsertionMetadata>(metadata),
                std::forward<_VisitedSet>(visited));
        }
    }

    if (const auto filteringSI =
        TfDynamic_cast<HdFilteringSceneIndexBasePtr>(sceneIndex)) {
        for (const auto& scene : filteringSI->GetInputScenes()) {
            _PopulateMetadata(
                scene,
                std::forward<const PluginInsertionMetadata>(metadata),
                std::forward<_VisitedSet>(visited));
        }
    }
}

static
std::string
_GetRendererName(const HdContainerDataSourceHandle& inputArgs)
{
    std::string rendererName;
    if (!inputArgs) {
        return rendererName;
    }
    if (const auto& nameDS = HdStringDataSource::Cast(inputArgs->Get(
        HdSceneIndexPluginRegistryTokens->rendererDisplayName))) {
        rendererName = nameDS->GetTypedValue(0.f);
    }
    return rendererName;
}

HdSceneIndexBaseRefPtr
HdSceneIndexPluginRegistry::AppendSceneIndex(
    const TfToken &sceneIndexPluginId,
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs,
    const std::string &renderInstanceId)
{
    if (HdSceneIndexPlugin *plugin = _GetSceneIndexPlugin(sceneIndexPluginId)) {
        HdSceneIndexBaseRefPtr result =
            plugin->AppendSceneIndex(renderInstanceId, inputScene, inputArgs);

        // NOTE: While HfPluginRegistry has a ref count mechanism for
        //       life time of plug-in instances, we don't need them to be
        //       cleaned up -- so we won't manually decrement their ref count
        //ReleasePlugin(plugin);

        const std::string rendererName = _GetRendererName(inputArgs);

        _PopulateMetadata(
            result,
            { rendererName, sceneIndexPluginId, -1 },
            { inputScene });

        return result;
    } else {
        return inputScene;
    }
}

HdSceneIndexBaseRefPtr
HdSceneIndexPluginRegistry::_AppendForPhases(
    const HdSceneIndexBaseRefPtr &inputScene,
    const _EntriesByPhaseMap &entriesByPhases,
    const HdContainerDataSourceHandle &argsUnderlay,
    const std::string &renderInstanceId)
{
    HdSceneIndexBaseRefPtr result = inputScene;
    const std::string rendererName = _GetRendererName(argsUnderlay);
    for (const auto &phasesPair : entriesByPhases) {
        for (const _Entry &entry : phasesPair.second) {
            HdSceneIndexBaseRefPtr input = result;
            HdContainerDataSourceHandle args = entry.args;

            if (args) {
                if (argsUnderlay) {

                    HdContainerDataSourceHandle c[] = {
                        args,
                        argsUnderlay,
                    };

                    args = HdOverlayContainerDataSource::New(2, c);

                }
            } else {
                args = argsUnderlay;
            }

            if (entry.callback) {
                result = entry.callback(renderInstanceId, result, args);
            } else {
                result = AppendSceneIndex(
                    entry.sceneIndexPluginId, result, args, renderInstanceId);
            }
            _PopulateMetadata(
                result,
                { rendererName, entry.sceneIndexPluginId, phasesPair.first },
                { input });
        }
    }
    return result;
}

// The control flow to get here is rather adhoc:
// HdSceneIndexPluginRegistry c'tor
//   > HfPluginRegistry::GetPluginDescs
//     > HfPluginRegistry::DiscoverPlugins
//       > HdSceneIndexPluginRegistry::_CollectAdditionalMetadata
void
HdSceneIndexPluginRegistry::_CollectAdditionalMetadata(
        const PlugRegistry &plugRegistry, const TfType &pluginType)
{
    const JsValue &loadWithRendererValue =
        plugRegistry.GetDataFromPluginMetaData(pluginType,
            "loadWithRenderer");

    const TfToken pluginTypeToken(pluginType.GetTypeName());

    if (loadWithRendererValue.GetType() == JsValue::StringType) {
        _preloadsForRenderers[loadWithRendererValue.GetString()].push_back(
            pluginTypeToken);

    } else if (loadWithRendererValue.GetType() == JsValue::ArrayType) {
        for (const std::string &s  :
                loadWithRendererValue.GetArrayOf<std::string>()) {
            _preloadsForRenderers[s].push_back(pluginTypeToken);
        }
    }

    const JsValue &loadWithAppsValue =
        plugRegistry.GetDataFromPluginMetaData(
            pluginType, "loadWithApps");
    if (loadWithAppsValue.GetType() == JsValue::StringType) {
        _preloadAppsForPlugins[pluginTypeToken]
            .insert(loadWithAppsValue.GetString());
    } else if (loadWithAppsValue.GetType() == JsValue::ArrayType) {
        const std::vector<std::string> loadWithApps =
            loadWithAppsValue.GetArrayOf<std::string>();
        _preloadAppsForPlugins[pluginTypeToken] = std::set<std::string>(
            loadWithApps.begin(), loadWithApps.end());
    }
}

void
HdSceneIndexPluginRegistry::_LoadPluginsForRenderer(
    const std::string &rendererDisplayName,
    const std::string &appName)
{
    auto loadPluginForApp = [this, &appName](const TfToken& pluginId) {
        const auto appsIter = _preloadAppsForPlugins.find(pluginId);
        // No loadWithApps entry => the plugin is to be loaded for all apps.
        if (appsIter == _preloadAppsForPlugins.end()) {
            return true;
        }

        const std::set<std::string>& apps = appsIter->second;
        if (!apps.empty() && !apps.count(appName)) {
            // This plugin has a non-empty array entry for
            // loadWithApps and the app we're making scene indexes
            // for isn't in the array: don't load it.
            return false;
        }
        return true;
    };

    // Preload any renderer plug-ins which have been tagged (via plugInfo) to
    // be loaded along with the specified renderer (or any renderer)
    const std::string preloadKeys[] = {
        std::string(""),        // preload for any renderer
        rendererDisplayName,    // preload for this renderer
    };

    for (size_t i = 0; i < TfArraySize(preloadKeys); ++i) {
        _PreloadMap::iterator plit = _preloadsForRenderers.find(preloadKeys[i]);
        if (plit == _preloadsForRenderers.end()) {
            continue;
        }
        TfTokenVector &rendererPlugins = plit->second;

        for (auto iter = rendererPlugins.begin();
                iter != rendererPlugins.end();) {
            const TfToken &id = *iter;

            if (!loadPluginForApp(id)) {
                ++iter;
                continue;
            }

            // This only ensures that the plug-in is loaded as the plug-in
            // itself might do further registration relevant to below.
            _GetSceneIndexPlugin(id);

            // Preload only needs to happen once per process. Remove this
            // plugin so we don't try to load it again later.
            iter = rendererPlugins.erase(iter);
        }

        if (rendererPlugins.empty()) {
            // Common case: we loaded all the plugins. We can entirely
            // erase the entry for this renderer from the overall map.
            _preloadsForRenderers.erase(plit);
        }
    }
}

// Collapses phase-and-order-entries map into a phase-entries map by
// concatenating sorted entry lists for each insertion order in a given phase.
// (Only two insertion orders currently exist: at start and at end.)
// The sort ensures a stable order independent of plugin load order.
/* static */
HdSceneIndexPluginRegistry::_EntriesByPhaseMap
HdSceneIndexPluginRegistry::_Collapse(
    const _EntriesByPhaseAndOrderMap& rendererEntriesMap)
{
    // Assert that we will visit "start" before "end".
    static_assert(
        HdSceneIndexPluginRegistry::InsertionOrderAtStart
        < HdSceneIndexPluginRegistry::InsertionOrderAtEnd);

    _EntriesByPhaseMap ret;
    for (const auto& [phaseAndOrder, entryList] : rendererEntriesMap) {
        const InsertionPhase& insertionPhase = phaseAndOrder.first;

        _EntryList& entries = ret[insertionPhase];
        entries.insert(entries.end(), entryList.begin(), entryList.end());
        // sort the entries inserted above by the id so that we get a stable 
        // order that does not depend on the order in which the plugins are 
        // discovered/loaded.
        std::sort(
            entries.end() - entryList.size(), entries.end(),
            [](const _Entry& a, const _Entry& b) {
                return a.sceneIndexPluginId < b.sceneIndexPluginId;
            });
    }
    return ret;
}

HdSceneIndexPluginRegistry::_EntriesByPhaseMap
HdSceneIndexPluginRegistry::_ComputeEntriesByPhaseMap(
    const std::string& rendererDisplayName) const
{
    if (rendererDisplayName.empty()) {
        TF_CODING_ERROR("Empty rendererDisplayName is reserved to mean all"
            " renderers.");
        return {};
    }

    _PhaseOrderEntriesMapByRenderer::const_iterator allRenderersIt =
        _entriesMapForRenderers.find("");
    _PhaseOrderEntriesMapByRenderer::const_iterator rendererIt =
        _entriesMapForRenderers.find(rendererDisplayName);

    // XXX
    // Ideally, we honor the insertion order more strictly by merging the
    // two phase-and-order maps together and then collapsing that into a
    // phase-only map.
    // But this requires changes to various scene index plugins that have
    // worked-around the existing adhoc-ordering where "all renderers" plugins
    // always run before "specific renderer" plugins for a given phase,
    // regardless of insertion order.
    //
    constexpr bool useIdealImpl = false;
    if constexpr (useIdealImpl) {
        _EntriesByPhaseAndOrderMap mergedEntriesByPhaseAndOrder;
        if (allRenderersIt != _entriesMapForRenderers.end()) {
            // Copy entries for all renderers first
            mergedEntriesByPhaseAndOrder = allRenderersIt->second;
        }
        
        if (rendererIt != _entriesMapForRenderers.end()) {
            // Append entries tied to the specified renderer.
            for (const auto& [phaseAndOrder, entryList] : rendererIt->second) {
                _EntryList& mergedEntryList =
                    mergedEntriesByPhaseAndOrder[phaseAndOrder];
                mergedEntryList.insert(
                    mergedEntryList.end(), entryList.begin(), entryList.end());
            }
        }
        // Now collapse phase-and-order map into a phase-only map by
        // concatenating entry lists in order.
        return _Collapse(mergedEntriesByPhaseAndOrder);
    }

    _EntriesByPhaseMap mergedEntriesByPhase;
    if (allRenderersIt != _entriesMapForRenderers.end()) {
        // Collapse entries for all renderers first
        mergedEntriesByPhase = _Collapse(allRenderersIt->second);
    }

    if (rendererIt != _entriesMapForRenderers.end()) {
        // By collapsing the renderer-specific entries below before appending,
        // we no longer strictly honor insertion order across the two lists...
        const auto rendererEntriesByPhase = _Collapse(rendererIt->second);
        for (const auto& [phase, entryList] : rendererEntriesByPhase) {
            _EntryList& mergedEntryList = mergedEntriesByPhase[phase];
            mergedEntryList.insert(
                mergedEntryList.end(), entryList.begin(), entryList.end());
        }
    }

    return mergedEntriesByPhase;
}

HdSceneIndexBaseRefPtr
HdSceneIndexPluginRegistry::AppendSceneIndicesForRenderer(
    const std::string &rendererDisplayName,
    const HdSceneIndexBaseRefPtr &inputScene,
    const std::string &renderInstanceId,
    const std::string &appName)
{
    _LoadPluginsForRenderer(rendererDisplayName, appName);

    HdContainerDataSourceHandle underlayArgs =
        HdRetainedContainerDataSource::New(
            HdSceneIndexPluginRegistryTokens->rendererDisplayName,
            HdRetainedTypedSampledDataSource<std::string>::New(
                rendererDisplayName));

    HdSceneIndexBaseRefPtr scene = _AppendForPhases(
        inputScene, _ComputeEntriesByPhaseMap(rendererDisplayName),
        underlayArgs, renderInstanceId);

    if (TfGetEnvSetting<bool>(HD_USE_ENCAPSULATING_SCENE_INDICES)) {
        scene = HdMakeEncapsulatingSceneIndex(
            { inputScene }, scene);
        scene->SetDisplayName("Scene index plugins");
    }
    return scene;
}

void
HdSceneIndexPluginRegistry::RegisterSceneIndexForRenderer(
    const std::string &rendererDisplayName,
    const TfToken &sceneIndexPluginId,
    const HdContainerDataSourceHandle &inputArgs,
    InsertionPhase insertionPhase,
    InsertionOrder insertionOrder)
{
    // Note that we're simply appending to the list here. The ordering
    // will be resolved later when we build the per-phase map.
    _entriesMapForRenderers[rendererDisplayName]
                             [{ insertionPhase, insertionOrder }]
                                 .emplace_back(sceneIndexPluginId, inputArgs);
}

void
HdSceneIndexPluginRegistry::RegisterSceneIndexForRenderer(
    const std::string &rendererDisplayName,
    SceneIndexAppendCallback callback,
    const HdContainerDataSourceHandle &inputArgs,
    InsertionPhase insertionPhase,
    InsertionOrder insertionOrder)
{
    // Note that we're simply appending to the list here. The ordering
    // will be resolved later when we build the per-phase map.
    _entriesMapForRenderers[rendererDisplayName]
                             [{ insertionPhase, insertionOrder }]
                                 .emplace_back(callback, inputArgs);
}

std::vector<TfToken>
HdSceneIndexPluginRegistry::LoadAndGetSceneIndexPluginIds(
    const std::string& rendererDisplayName, const std::string& appName)
{
    std::vector<TfToken> ret;
    _LoadPluginsForRenderer(rendererDisplayName, appName);
    for (const auto& phaseAndEntryList :
         _ComputeEntriesByPhaseMap(rendererDisplayName)) {
        const _EntryList& entryList = phaseAndEntryList.second;
        for (const _Entry& entry: entryList) {
            if (entry.sceneIndexPluginId.IsEmpty()) {
                // Skip callback-registered entries.
                continue;
            }
            ret.push_back(entry.sceneIndexPluginId);
        }
    }
    return ret;
}

bool
HdSceneIndexPluginRegistry::
GetPluginInsertionMetadataForSceneIndex(
    const HdSceneIndexBaseRefPtr& sceneIndex,
    PluginInsertionMetadata& metadata)
{
    auto it = _metadata.find(TfCreateWeakPtr<HdSceneIndexBase>(&(*sceneIndex)));
    if (it == _metadata.end()) {
        return false;
    }
    if (!it->first) {
        _metadata.erase(it);
        return false;
    }
    metadata = it->second;
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
