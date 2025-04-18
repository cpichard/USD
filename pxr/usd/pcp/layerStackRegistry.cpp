//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
/// \file LayerStackRegistry.cpp

#include "pxr/pxr.h"
#include "pxr/usd/pcp/layerStackRegistry.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/pcp/layerStackIdentifier.h"
#include "pxr/usd/pcp/utils.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/staticData.h"

#include <tbb/queuing_rw_mutex.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

using std::pair;
using std::make_pair;

PXR_NAMESPACE_OPEN_SCOPE

class Pcp_LayerStackRegistryData {
public:
    Pcp_LayerStackRegistryData(
        const PcpLayerStackIdentifier& rootLayerStackId_,
        const std::string& fileFormatTarget_, bool isUsd) 
        : rootLayerStackId(rootLayerStackId_)
        , fileFormatTarget(fileFormatTarget_)
        , isUsd(isUsd)
        , mutedLayers(fileFormatTarget_)
    { }

    typedef SdfLayerHandleVector Layers;
    typedef PcpLayerStackPtrVector LayerStacks;
    typedef std::unordered_map<PcpLayerStackIdentifier, PcpLayerStackPtr,
                               TfHash> IdentifierToLayerStack;
    typedef std::unordered_map<SdfLayerHandle, LayerStacks, TfHash>
        LayerToLayerStacks;
    typedef std::unordered_map<PcpLayerStackPtr, Layers, TfHash>
        LayerStackToLayers;

    typedef std::unordered_map<std::string, LayerStacks, TfHash>
        MutedLayerIdentifierToLayerStacks;
    typedef std::unordered_map<PcpLayerStackPtr, std::set<std::string>, TfHash>
        LayerStackToMutedLayerIdentifiers;

    IdentifierToLayerStack identifierToLayerStack;
    LayerToLayerStacks layerToLayerStacks;
    LayerStackToLayers layerStackToLayers;
    MutedLayerIdentifierToLayerStacks mutedLayerIdentifierToLayerStacks;
    LayerStackToMutedLayerIdentifiers layerStackToMutedLayerIdentifiers;
    LayerToLayerStacks layerToLayerStackOverrides;

    const PcpLayerStackPtrVector empty;
    const PcpLayerStackIdentifier rootLayerStackId;
    const std::string fileFormatTarget;
    bool isUsd;
    Pcp_MutedLayers mutedLayers;

    tbb::queuing_rw_mutex mutex;
};

Pcp_LayerStackRegistryRefPtr
Pcp_LayerStackRegistry::New(
    const PcpLayerStackIdentifier& rootLayerStackId,
    const std::string& fileFormatTarget, bool isUsd)
{
    return TfCreateRefPtr(
        new Pcp_LayerStackRegistry(rootLayerStackId, fileFormatTarget, isUsd));
}

Pcp_LayerStackRegistry::Pcp_LayerStackRegistry(
    const PcpLayerStackIdentifier& rootLayerStackId,
    const std::string& fileFormatTarget, bool isUsd)
    : _data(new Pcp_LayerStackRegistryData(
            rootLayerStackId, fileFormatTarget, isUsd))
{
    // Do nothing
}

Pcp_LayerStackRegistry::~Pcp_LayerStackRegistry()
{
    // Do nothing
}

void 
Pcp_LayerStackRegistry::MuteAndUnmuteLayers(
    const SdfLayerHandle& anchorLayer,
    std::vector<std::string>* layersToMute,
    std::vector<std::string>* layersToUnmute)
{
    _data->mutedLayers.MuteAndUnmuteLayers(
        anchorLayer, layersToMute, layersToUnmute);
}

const std::vector<std::string>& 
Pcp_LayerStackRegistry::GetMutedLayers() const
{
    return _data->mutedLayers.GetMutedLayers();
}

bool 
Pcp_LayerStackRegistry::IsLayerMuted(const SdfLayerHandle& anchorLayer,
                                     const std::string& layerIdentifier,
                                     std::string* canonicalSublayerId) const
{
    return _data->mutedLayers.IsLayerMuted(
        anchorLayer, layerIdentifier, canonicalSublayerId);
}

const PcpLayerStackPtrVector&
Pcp_LayerStackRegistry::FindAllUsingMutedLayer(const std::string& layerId) const
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);
    const auto i = _data->mutedLayerIdentifierToLayerStacks.find(layerId);
    return i != _data->mutedLayerIdentifierToLayerStacks.end() ? 
        i->second : _data->empty;
}

PcpLayerStackRefPtr
Pcp_LayerStackRegistry::FindOrCreate(const PcpLayerStackIdentifier& identifier,
                                     PcpErrorVector *allErrors)
{
    // Can only create layer stacks for valid identifiers so if the identifier
    // is invalid we can't have an entry for it.
    if (!identifier) {
        TF_CODING_ERROR("Cannot build layer stack with null rootLayer");
        return TfNullPtr;
    }

    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);
    PcpLayerStackRefPtr refLayerStack;

    if (const PcpLayerStackPtr & layerStack = _Find(identifier)) {
        refLayerStack = TfCreateRefPtrFromProtectedWeakPtr(layerStack);
    }
    if (!refLayerStack) {
        lock.release();

        PcpLayerStackRefPtr createdLayerStack =
            TfCreateRefPtr(new PcpLayerStack(identifier, *this));

        // Take the lock and check again for an existing layer stack, or
        // install the one we just created.
        lock.acquire(_data->mutex);
        if (const PcpLayerStackPtr & layerStack = _Find(identifier)) {
            refLayerStack = TfCreateRefPtrFromProtectedWeakPtr(layerStack);
        }
        if (!refLayerStack) {
            // No existing entry, or it is being deleted. Add the one we just
            // create to the map.
            refLayerStack = createdLayerStack;
            _data->identifierToLayerStack[identifier] = refLayerStack;
            // Also give it a link back to us so it can remove itself upon
            // destruction, and install its layers into our structures.
            refLayerStack->_registry = TfCreateWeakPtr(this);
            _SetLayers(get_pointer(refLayerStack));
            lock.release();

            // Return errors from newly computed layer stacks.
            PcpErrorVector errors = refLayerStack->GetLocalErrors();
            allErrors->insert(allErrors->end(), errors.begin(), errors.end());
        }
    }

    return refLayerStack;
}

PcpLayerStackPtr
Pcp_LayerStackRegistry::Find(const PcpLayerStackIdentifier& identifier) const
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);
    return _Find(identifier);
}

PcpLayerStackPtr
Pcp_LayerStackRegistry::_Find(const PcpLayerStackIdentifier& identifier) const
{
    auto iter = _data->identifierToLayerStack.find(identifier);
    return (iter != _data->identifierToLayerStack.end()) ?
        iter->second : PcpLayerStackPtr();
}

bool
Pcp_LayerStackRegistry::Contains(const PcpLayerStackPtr& layerStack) const
{
    auto ptr = get_pointer(layerStack);
    return ptr && get_pointer(ptr->_registry) == this;
}

const PcpLayerStackPtrVector&
Pcp_LayerStackRegistry::FindAllUsingLayer(const SdfLayerHandle& layer) const
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);

    auto layerStackOverrides = _data->layerToLayerStackOverrides.find(layer);
    if (layerStackOverrides != _data->layerToLayerStackOverrides.end()) {
        return layerStackOverrides->second;
    }

    auto i = _data->layerToLayerStacks.find(layer);
    return i != _data->layerToLayerStacks.end() ? i->second : _data->empty;
}

void
Pcp_LayerStackRegistry::SetLayerStackVectorOverride(
    const SdfLayerHandle& layer, 
    const PcpLayerStackPtrVector& layerStacks)
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/true);
    _data->layerToLayerStackOverrides[layer] = layerStacks;
}

const PcpLayerStackPtrVector&
Pcp_LayerStackRegistry::GetLayerStackVectorOverride(const SdfLayerHandle& layer) const
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);
    if (_data->layerToLayerStackOverrides.empty()) {
        return _data->empty;
    }

    auto layerStackOverrides = _data->layerToLayerStackOverrides.find(layer);
    return layerStackOverrides != _data->layerToLayerStackOverrides.end() ?
        layerStackOverrides->second : _data->empty;
}

void 
Pcp_LayerStackRegistry::ClearLayerStackVectorOverrides()
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/true);
    _data->layerToLayerStackOverrides.clear();
}

std::vector<PcpLayerStackPtr>
Pcp_LayerStackRegistry::GetAllLayerStacks() const
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/false);
    std::vector<PcpLayerStackPtr> result;
    result.reserve(_data->identifierToLayerStack.size());
    TF_FOR_ALL(i, _data->identifierToLayerStack) {
        TF_VERIFY(i->second, "Unexpected dead layer stack %s",
                  TfStringify(i->first).c_str());
        result.push_back(i->second);
    }
    return result;
}

void 
Pcp_LayerStackRegistry::ForEachLayerStack(
    const TfFunctionRef<void(const PcpLayerStackPtr&)>& fn)
{
    // Copy all layer stacks so that we can run the callback
    // without holding a read lock on the layer registry.
    const std::vector<PcpLayerStackPtr> layerStacks = GetAllLayerStacks();
    for (const PcpLayerStackPtr& layerStack : layerStacks) {
        fn(layerStack);
    }
}

////////////////////////////////////////////////////////////////////////
// Private helper methods.

void
Pcp_LayerStackRegistry::_SetLayersAndRemove(
    const PcpLayerStackIdentifier& identifier,
    const PcpLayerStack *layerStack)
{
    tbb::queuing_rw_mutex::scoped_lock lock(_data->mutex, /*write=*/true);

    Pcp_LayerStackRegistryData::IdentifierToLayerStack::const_iterator i =
        _data->identifierToLayerStack.find(identifier);
    // It's possible that layerStack has already been removed from the
    // map if a FindOrCreate call intercedes between the moment when the
    // layer stack's ref count drops to zero and the time the layer stack
    // destructor is called (which is how we get into this method).
    // Always call _SetLayers to clear this (now empty) layer stack's
    // pointer from the maps inside _data, even if a new layer stack with the
    // same identifier has already been added to the identifierToLayerStack
    // map.
    _SetLayers(layerStack);
    if (i != _data->identifierToLayerStack.end() &&
        i->second.operator->() == layerStack) {
        _data->identifierToLayerStack.erase(identifier);
    }
}

void
Pcp_LayerStackRegistry::_SetLayers(const PcpLayerStack* layerStack)
{
    PcpLayerStackPtr layerStackPtr = TfCreateNonConstWeakPtr(layerStack);

    // Get the layers for the layer stack.
    Pcp_LayerStackRegistryData::Layers& layers =
        _data->layerStackToLayers[layerStackPtr];

    // Remove layer stack from the table entry for each layer in
    // the layer stack.
    for (const auto& layer : layers) {
        Pcp_LayerStackRegistryData::LayerStacks& layerStacks =
            _data->layerToLayerStacks[layer];
        layerStacks.erase(std::find(layerStacks.begin(),
                                    layerStacks.end(), layerStackPtr));
    }

    // Save the layers for the layer stack.
    const SdfLayerRefPtrVector& newLayers = layerStack->GetLayers();
    if (newLayers.empty()) {
        // Don't leave empty entries hanging around.
        _data->layerStackToLayers.erase(layerStackPtr);
        
        // If this empty entry is currently a part of an override list,
        // It should be removed from it as well.
        for (auto it = _data->layerToLayerStackOverrides.begin(); 
                  it !=  _data->layerToLayerStackOverrides.end();) 
        {
            Pcp_LayerStackRegistryData::LayerStacks& overrides = it->second;
            auto result = std::find(
                overrides.begin(), overrides.end(), layerStackPtr);
            
            if (result != overrides.end()) {
                overrides.erase(result);
            }

            it = overrides.empty() ? 
                _data->layerToLayerStackOverrides.erase(it) : std::next(it); 
        }
    } else {
        layers.assign(newLayers.begin(), newLayers.end());
    }

    // Add the layer stack for each layer in the layer stack.
    for (const auto& layer : newLayers) {
        _data->layerToLayerStacks[layer].push_back(layerStackPtr);
    }

    // Also store mappings from layer stack <-> muted layers in
    // the layer stack.
    std::set<std::string>* mutedLayerIdentifiers = TfMapLookupPtr(
        _data->layerStackToMutedLayerIdentifiers, layerStackPtr);

    if (mutedLayerIdentifiers) {
        for (const auto& layerId : *mutedLayerIdentifiers) {
            Pcp_LayerStackRegistryData::LayerStacks& layerStacks =
                _data->mutedLayerIdentifierToLayerStacks[layerId];
            layerStacks.erase(std::find(layerStacks.begin(),
                                        layerStacks.end(), layerStackPtr));
        }
    }

    const std::set<std::string>& newMutedLayers = layerStack->GetMutedLayers();
    if (newMutedLayers.empty()) {
        if (mutedLayerIdentifiers) {
            // Don't leave empty entries hanging around.
            _data->layerStackToMutedLayerIdentifiers.erase(layerStackPtr);
        }
    }
    else {
        if (!mutedLayerIdentifiers) {
            mutedLayerIdentifiers = 
                &_data->layerStackToMutedLayerIdentifiers[layerStackPtr];
        }
        *mutedLayerIdentifiers = std::set<std::string>(
            newMutedLayers.begin(), newMutedLayers.end());
    }

    for (const auto& mutedLayer : newMutedLayers) {
        _data->mutedLayerIdentifierToLayerStacks[mutedLayer]
            .push_back(layerStackPtr);
    }
}

const PcpLayerStackIdentifier&
Pcp_LayerStackRegistry::_GetRootLayerStackIdentifier() const
{
    return _data->rootLayerStackId;
}

const std::string&
Pcp_LayerStackRegistry::_GetFileFormatTarget() const
{
    return _data->fileFormatTarget;
}

bool
Pcp_LayerStackRegistry::_IsUsd() const
{
    return _data->isUsd;
}

const Pcp_MutedLayers&
Pcp_LayerStackRegistry::_GetMutedLayers() const
{
    return _data->mutedLayers;
}

// ------------------------------------------------------------

std::string 
Pcp_MutedLayers::_GetCanonicalLayerId(const SdfLayerHandle& anchorLayer, 
                                      const std::string& layerId) const
{
    // Split out any file format arguments embedded in layerId so we
    // can anchor the layer path separately.
    std::string layerPath;
    SdfLayer::FileFormatArguments args;
    if (!SdfLayer::SplitIdentifier(layerId, &layerPath, &args)) {
        return std::string();
    }
    
    // XXX: 
    // We may ultimately want to use the resolved path here but that's
    // possibly a bigger change and there are questions about what happens if
    // the muted path doesn't resolve to an existing asset and how/when to
    // invalidate the resolved paths stored in the Pcp_MutedLayers object.
    const std::string anchoredPath =
        SdfLayer::IsAnonymousLayerIdentifier(layerPath) ?
        layerPath :
        ArGetResolver().CreateIdentifier(
            layerPath, anchorLayer->GetResolvedPath());

    if (anchoredPath.empty()) {
        return std::string();
    }

    // If the layer identifier specified a file format target that matches
    // our own, we strip it off. This simplifies the matching done in
    // IsLayerMuted, e.g. in the case where the user specified a muted
    // layer with a file format target that matches our own, and the
    // layer identifier being checked has no file format target.
    Pcp_StripFileFormatTarget(_fileFormatTarget, &args);

    return SdfLayer::CreateIdentifier(anchoredPath, args);
}

Pcp_MutedLayers::Pcp_MutedLayers(const std::string& fileFormatTarget)
    : _fileFormatTarget(fileFormatTarget)
{
}

const std::vector<std::string>& 
Pcp_MutedLayers::GetMutedLayers() const
{
    return _layers;
}

void 
Pcp_MutedLayers::MuteAndUnmuteLayers(const SdfLayerHandle& anchorLayer,
                                     std::vector<std::string>* layersToMute,
                                     std::vector<std::string>* layersToUnmute)
{
    std::vector<std::string> mutedLayers, unmutedLayers;

    for (const auto& layerToMute : *layersToMute) {
        const std::string canonicalId = 
            _GetCanonicalLayerId(anchorLayer, layerToMute);
        if (canonicalId.empty()) {
            continue;
        }

        const auto layerIt = std::lower_bound(
            _layers.begin(), _layers.end(), canonicalId);
        if (layerIt == _layers.end() || *layerIt != canonicalId) {
            _layers.insert(layerIt, canonicalId);
            mutedLayers.push_back(canonicalId);
        }
    }

    for (const auto& layerToUnmute : *layersToUnmute) {
        const std::string canonicalId = 
            _GetCanonicalLayerId(anchorLayer, layerToUnmute);
        if (canonicalId.empty()) {
            continue;
        }

        const auto layerIt = std::lower_bound(
            _layers.begin(), _layers.end(), canonicalId);
        if (layerIt != _layers.end() && *layerIt == canonicalId) {
            _layers.erase(layerIt);
            unmutedLayers.push_back(canonicalId);
        }
    }

    layersToMute->swap(mutedLayers);
    layersToUnmute->swap(unmutedLayers);
}

bool 
Pcp_MutedLayers::IsLayerMuted(const SdfLayerHandle& anchorLayer,
                              const std::string& layerId,
                              std::string* canonicalLayerId) const
{
    if (_layers.empty()) {
        return false;
    }

    std::string canonicalId = _GetCanonicalLayerId(anchorLayer, layerId);
    if (canonicalId.empty()) {
        return false;
    }

    if (std::binary_search(_layers.begin(), _layers.end(), canonicalId)) {
        if (canonicalLayerId) {
            canonicalLayerId->swap(canonicalId);
        }
        return true;
    }
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE
