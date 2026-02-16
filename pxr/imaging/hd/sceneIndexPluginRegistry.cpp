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
    const _EntryList &orderedEntries,
    const HdContainerDataSourceHandle &argsUnderlay,
    const std::string &renderInstanceId)
{
    HdSceneIndexBaseRefPtr result = inputScene;
    const std::string rendererName = _GetRendererName(argsUnderlay);
    for (const auto &entry : orderedEntries) {
        HdSceneIndexBaseRefPtr input = result;
        HdContainerDataSourceHandle args = entry.args;

        args = HdOverlayContainerDataSource::OverlayedContainerDataSources(
            args, argsUnderlay);
        
        if (entry.callback) {
            result = entry.callback(renderInstanceId, input, args);
        } else {
            result = AppendSceneIndex(
                entry.sceneIndexPluginId, input, args, renderInstanceId);
        }
        _PopulateMetadata(
            result,
            { rendererName, entry.sceneIndexPluginId, entry.phase },
            { input });
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

// Computes an ordered list of entries from the registered entries for
// all renderers and the specified renderer.
//
HdSceneIndexPluginRegistry::_EntryList
HdSceneIndexPluginRegistry::_ComputeOrderedEntriesForRenderer(
    const std::string& rendererDisplayName) const
{
    _EntriesByRendererMap::const_iterator allRenderersIt =
        _entriesForRenderers.find("");
    _EntriesByRendererMap::const_iterator rendererIt =
        rendererDisplayName.empty()
        ? _entriesForRenderers.end()
        : _entriesForRenderers.find(rendererDisplayName);
    
    auto sortFn = [](const _Entry& a, const _Entry& b) {
        if (a.phase != b.phase) {
            return a.phase < b.phase;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        // Sort by plugin id to get a stable order that does not depend on the
        // order in which the plugins are discovered/loaded.
        // Note that callback-registered entries will have an empty plugin id, 
        // so they will be sorted before any entries with a non-empty plugin id.
        return a.sceneIndexPluginId < b.sceneIndexPluginId;
    };

    // Ideally, we honor the insertion order more strictly by merging entries
    // for "all" renderers and the specified renderer together and then sorting
    // that merged list together.
    // But this requires changes to various scene index plugins that have
    // worked-around the existing adhoc-ordering where "all renderers" plugins
    // always run before "specific renderer" plugins for a given phase,
    // regardless of insertion order.
    //
    constexpr bool useIdealImpl = false;
    if constexpr (useIdealImpl) {
        _EntryList mergedEntries;

        if (allRenderersIt != _entriesForRenderers.end()) {
            // Copy entries for all renderers first
            mergedEntries = allRenderersIt->second;
        }
        
        if (rendererIt != _entriesForRenderers.end()) {
            // Append entries tied to the specified renderer.
            mergedEntries.insert(
                mergedEntries.end(), rendererIt->second.begin(),
                rendererIt->second.end());
        }
        std::sort(mergedEntries.begin(), mergedEntries.end(), sortFn);
        return mergedEntries;
    }

    // Non-ideal ordering implementation:
    _EntryList allEntries;
    if (allRenderersIt != _entriesForRenderers.end()) {
        allEntries = allRenderersIt->second;
        std::sort(allEntries.begin(), allEntries.end(), sortFn);
    }

    _EntryList rendererEntries;
    if (rendererIt != _entriesForRenderers.end()) {
        rendererEntries = rendererIt->second;    
        std::sort(rendererEntries.begin(), rendererEntries.end(), sortFn);
    }

    _EntryList mergedEntries;
    mergedEntries.reserve(allEntries.size() + rendererEntries.size());
    // Merge the sorted lists together, ensuring that entries with the same
    // phase are grouped together with "all renderers" entries coming before
    // "specific renderer" entries (i.e. not strictly honoring insertion 
    // order across the two lists).
    auto it1 = allEntries.begin();
    auto it2 = rendererEntries.begin();
    while (it1 != allEntries.end() && it2 != rendererEntries.end()) {
        if (it1->phase < it2->phase) {
            mergedEntries.push_back(*it1);
            ++it1;
        } else if (it1->phase > it2->phase) {
            mergedEntries.push_back(*it2);
            ++it2;
        } else {
            mergedEntries.push_back(*it1);
            ++it1;
        }
    }
    mergedEntries.insert(mergedEntries.end(), it1, allEntries.end());
    mergedEntries.insert(mergedEntries.end(), it2, rendererEntries.end());
    
    return mergedEntries;
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
        inputScene, _ComputeOrderedEntriesForRenderer(rendererDisplayName),
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
    // will be resolved later.
    _entriesForRenderers[rendererDisplayName].emplace_back(
        sceneIndexPluginId, inputArgs, insertionPhase, insertionOrder);
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
    // will be resolved later.
    _entriesForRenderers[rendererDisplayName].emplace_back(
        callback, inputArgs, insertionPhase, insertionOrder);
}

std::vector<TfToken>
HdSceneIndexPluginRegistry::LoadAndGetSceneIndexPluginIds(
    const std::string& rendererDisplayName, const std::string& appName)
{
    std::vector<TfToken> ret;
    _LoadPluginsForRenderer(rendererDisplayName, appName);
    const auto entries = _ComputeOrderedEntriesForRenderer(rendererDisplayName);
    for (const _Entry& entry: entries) {
        if (entry.sceneIndexPluginId.IsEmpty()) {
            // Skip callback-registered entries.
            continue;
        }
        ret.push_back(entry.sceneIndexPluginId);
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
