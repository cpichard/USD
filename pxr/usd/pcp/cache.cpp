//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/pcp/cache.h"
#include "pxr/usd/pcp/arc.h"
#include "pxr/usd/pcp/changes.h"
#include "pxr/usd/pcp/diagnostic.h"
#include "pxr/usd/pcp/debugCodes.h"
#include "pxr/usd/pcp/dependencies.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/pcp/layerStackIdentifier.h"
#include "pxr/usd/pcp/layerStackRegistry.h"
#include "pxr/usd/pcp/node_Iterator.h"
#include "pxr/usd/pcp/pathTranslation.h"
#include "pxr/usd/pcp/primIndex.h"
#include "pxr/usd/pcp/propertyIndex.h"
#include "pxr/usd/pcp/statistics.h"
#include "pxr/usd/pcp/targetIndex.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverScopedCache.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/work/dispatcher.h"
#include "pxr/base/work/loops.h"
#include "pxr/base/work/utils.h"
#include "pxr/base/work/withScopedParallelism.h"
#include "pxr/base/tf/enum.h"
#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/registryManager.h"

#include <tbb/concurrent_queue.h>
#include <tbb/spin_rw_mutex.h>

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

using std::make_pair;
using std::pair;
using std::vector;

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(
    PCP_CULLING, true,
    "Controls whether culling is enabled in Pcp caches.");

// Helper for applying changes immediately if the client hasn't asked that
// they only be collected instead.
class Pcp_CacheChangesHelper {
public:
    // Construct.  If \p changes is \c NULL then collect changes into an
    // internal object and apply them to \p cache when this object is
    // destroyed.
    Pcp_CacheChangesHelper(PcpChanges* changes) :
        _changes(changes)
    {
        // Do nothing
    }

    ~Pcp_CacheChangesHelper()
    {
        // Apply changes now immediately if _changes is NULL.
        if (!_changes) {
            _immediateChanges.Apply();
        }
    }

    // Act like a pointer to the c'tor PcpChanges or, if that's NULL, the
    // internal changes.
    PcpChanges* operator->()
    {
        return _changes ? _changes : &_immediateChanges;
    }

private:
    PcpChanges* _changes;
    PcpChanges _immediateChanges;
};

PcpCache::PcpCache(
    const PcpLayerStackIdentifier & layerStackIdentifier,
    const std::string& fileFormatTarget,
    bool usd) :
    _rootLayer(layerStackIdentifier.rootLayer),
    _sessionLayer(layerStackIdentifier.sessionLayer),
    _layerStackIdentifier(layerStackIdentifier),
    _usd(usd),
    _fileFormatTarget(fileFormatTarget),
    _layerStackCache(Pcp_LayerStackRegistry::New(
        _layerStackIdentifier, _fileFormatTarget, _usd)),
    _primDependencies(new Pcp_Dependencies())
{
    _primIndexInputs
        .Cache(this)
        .VariantFallbacks(&_variantFallbackMap)
        .IncludedPayloads(&_includedPayloads)
        .Cull(TfGetEnvSetting(PCP_CULLING))
        .USD(_usd)
        .FileFormatTarget(_fileFormatTarget);
}

PcpCache::~PcpCache()
{
    // We have to release the GIL here, since we don't know whether or not we've
    // been invoked by some python-wrapped thing here which might not have
    // released the GIL itself.  Dropping the layer RefPtrs could cause the
    // layers to expire, which might try to invoke the python/c++ shared
    // lifetime management support, which will need to acquire the GIL.  If that
    // happens in a separate worker thread while this thread holds the GIL,
    // we'll deadlock.  Dropping the GIL here prevents this.
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    // Clear the layer stack before destroying the registry, so
    // that it can safely unregister itself.
    TfReset(_layerStack);

    // Tear down some of our datastructures in parallel, since it can take quite
    // a bit of time.
    WorkWithScopedParallelism([this]() {
            WorkDispatcher wd;
            wd.Run([this]() { _rootLayer.Reset(); });
            wd.Run([this]() { _sessionLayer.Reset(); });
            wd.Run([this]() { TfReset(_includedPayloads); });
            wd.Run([this]() { TfReset(_variantFallbackMap); });
            wd.Run([this]() { _primIndexCache.ClearInParallel(); });
            wd.Run([this]() { TfReset(_propertyIndexCache); });
            // Wait, since _primDependencies cannot be destroyed concurrently
            // with the prim indexes, since they both hold references to
            // layer stacks and the layer stack registry is not currently
            // prepared to handle concurrent expiry of layer stacks.
        });

    _primDependencies.reset();
    _layerStackCache.Reset();
}

////////////////////////////////////////////////////////////////////////
// Cache parameters.

const PcpLayerStackIdentifier&
PcpCache::GetLayerStackIdentifier() const
{
    return _layerStackIdentifier;
}

PcpLayerStackPtr
PcpCache::GetLayerStack() const
{
    return _layerStack;
}

bool
PcpCache::HasRootLayerStack(PcpLayerStackPtr const &layerStack) const
{
    return get_pointer(layerStack) == get_pointer(_layerStack);
}

PcpLayerStackPtr
PcpCache::FindLayerStack(const PcpLayerStackIdentifier &id) const
{
    return _layerStackCache->Find(id);
}

bool
PcpCache::UsesLayerStack(const PcpLayerStackPtr &layerStack) const
{
    return _layerStackCache->Contains(layerStack);
}

const PcpLayerStackPtrVector&
PcpCache::FindAllLayerStacksUsingLayer(const SdfLayerHandle& layer) const
{
    return _layerStackCache->FindAllUsingLayer(layer);
}

bool
PcpCache::IsUsd() const
{
    return _usd;
}

const std::string& 
PcpCache::GetFileFormatTarget() const
{
    return _fileFormatTarget;
}

PcpVariantFallbackMap
PcpCache::GetVariantFallbacks() const
{
    return _variantFallbackMap;
}

void
PcpCache::SetVariantFallbacks( const PcpVariantFallbackMap &map,
                                   PcpChanges* changes )
{
    if (_variantFallbackMap != map) {
        _variantFallbackMap = map;

        Pcp_CacheChangesHelper cacheChanges(changes);

        // We could scan to find prim indices that actually use the
        // affected variant sets, but for simplicity of implementing what
        // is a really uncommon operation, we just invalidate everything.
        cacheChanges->DidChangeSignificantly(this, SdfPath::AbsoluteRootPath());
    }
}

bool
PcpCache::IsPayloadIncluded(const SdfPath &path) const
{
    return _includedPayloads.find(path) != _includedPayloads.end();
}

PcpCache::PayloadSet const &
PcpCache::GetIncludedPayloads() const
{
    return _includedPayloads;
}

void
PcpCache::RequestPayloads( const SdfPathSet & pathsToInclude,
                           const SdfPathSet & pathsToExclude,
                           PcpChanges* changes )
{
    Pcp_CacheChangesHelper cacheChanges(changes);

    TF_FOR_ALL(path, pathsToInclude) {
        if (path->IsPrimPath()) {
            if (_includedPayloads.insert(*path).second) {
                cacheChanges->DidChangeSignificantly(this, *path);
            }
        }
        else {
            TF_CODING_ERROR("Path <%s> must be a prim path", path->GetText());
        }
    }
    TF_FOR_ALL(path, pathsToExclude) {
        if (path->IsPrimPath()) {
            if (pathsToInclude.find(*path) == pathsToInclude.end()) {
                if (_includedPayloads.erase(*path)) {
                    cacheChanges->DidChangeSignificantly(this, *path);
                }
            }
        }
        else {
            TF_CODING_ERROR("Path <%s> must be a prim path", path->GetText());
        }
    }
}

void 
PcpCache::RequestLayerMuting(const std::vector<std::string>& layersToMute,
                             const std::vector<std::string>& layersToUnmute,
                             PcpChanges* changes,
                             std::vector<std::string>* newLayersMuted,
                             std::vector<std::string>* newLayersUnmuted)
{
    TRACE_FUNCTION();

    ArResolverContextBinder binder(_layerStackIdentifier.pathResolverContext);

    std::vector<std::string> finalLayersToMute;
    for (const auto& layerToMute : layersToMute) {
        if (layerToMute.empty()) {
            continue;
        }

        if (SdfLayer::Find(layerToMute) == _rootLayer) {
            TF_CODING_ERROR("Cannot mute cache's root layer @%s@", 
                            layerToMute.c_str());
            continue;
        }

        finalLayersToMute.push_back(layerToMute);
    }

    std::vector<std::string> finalLayersToUnmute;
    for (const auto& layerToUnmute : layersToUnmute) {
        if (layerToUnmute.empty()) {
            continue;
        }

        if (std::find(layersToMute.begin(), layersToMute.end(), 
                      layerToUnmute) == layersToMute.end()) {
            finalLayersToUnmute.push_back(layerToUnmute);
        }
    }

    if (finalLayersToMute.empty() && finalLayersToUnmute.empty()) {
        return;
    }

    _layerStackCache->MuteAndUnmuteLayers(
        _rootLayer, &finalLayersToMute, &finalLayersToUnmute);

    Pcp_CacheChangesHelper cacheChanges(changes);

    cacheChanges->DidMuteAndUnmuteLayers(
        this, finalLayersToMute, finalLayersToUnmute);

    // The above won't handle cases where we've unmuted the root layer
    // of a reference or payload layer stack, since prim indexing will skip
    // computing those layer stacks altogether. So, find all prim indexes
    // that have the associated composition error and treat this as if
    // we're reloading the unmuted layer.
    if (!finalLayersToUnmute.empty()) {
        for (const auto& primIndexEntry : _primIndexCache) {
            const PcpPrimIndex& primIndex = primIndexEntry.second;
            if (!primIndex.IsValid()) {
                continue;
            }

            for (const auto& error : primIndex.GetLocalErrors()) {
                PcpErrorMutedAssetPathPtr typedError = 
                    std::dynamic_pointer_cast<PcpErrorMutedAssetPath>(error);
                if (!typedError) {
                    continue;
                }

                const bool assetWasUnmuted = std::find(
                    finalLayersToUnmute.begin(), finalLayersToUnmute.end(), 
                    typedError->resolvedAssetPath) != finalLayersToUnmute.end();
                if (assetWasUnmuted) {
                    cacheChanges->DidMaybeFixAsset(
                        this, typedError->site, typedError->sourceLayer, 
                        typedError->resolvedAssetPath);
                }
            }
        }
    }

    // update out newLayersMuted and newLayersUnmuted parameters
    if (newLayersMuted) {
        *newLayersMuted = std::move(finalLayersToMute);
    }
    if (newLayersUnmuted) {
        *newLayersUnmuted = std::move(finalLayersToUnmute);
    }
}

const std::vector<std::string>&
PcpCache:: GetMutedLayers() const
{
    return _layerStackCache->GetMutedLayers();
}

bool 
PcpCache::IsLayerMuted(const std::string& layerId) const
{
    return IsLayerMuted(_rootLayer, layerId);
}

bool 
PcpCache::IsLayerMuted(const SdfLayerHandle& anchorLayer,
                       const std::string& layerId,
                       std::string* canonicalMutedLayerId) const
{
    return _layerStackCache->IsLayerMuted(
        anchorLayer, layerId, canonicalMutedLayerId);
}

const PcpPrimIndexInputs &
PcpCache::GetPrimIndexInputs() const
{
    return _primIndexInputs;
}

PcpLayerStackRefPtr
PcpCache::ComputeLayerStack(const PcpLayerStackIdentifier &id,
                            PcpErrorVector *allErrors)
{
    PcpLayerStackRefPtr result =
        _layerStackCache->FindOrCreate(id, allErrors);

    // Retain the cache's root layer stack.
    if (!_layerStack && id == GetLayerStackIdentifier()) {
        _layerStack = result;
    }

    return result;
}

const PcpPrimIndex *
PcpCache::FindPrimIndex(const SdfPath & path) const
{
    return _GetPrimIndex(path);
}

void
PcpCache::ComputeRelationshipTargetPaths(const SdfPath & relPath, 
                                         SdfPathVector *paths,
                                         bool localOnly,
                                         const SdfSpecHandle &stopProperty,
                                         bool includeStopProperty,
                                         SdfPathVector *deletedPaths,
                                         PcpErrorVector *allErrors)
{
    TRACE_FUNCTION();

    if (!relPath.IsPropertyPath()) {
        TF_CODING_ERROR(
            "Path <%s> must be a relationship path", relPath.GetText());
        return;
    }

    auto computeTargets = [&](const PcpPropertyIndex &propIndex) {
        PcpTargetIndex targetIndex;
        PcpBuildFilteredTargetIndex( PcpSite(GetLayerStackIdentifier(), relPath),
                                    propIndex,
                                    SdfSpecTypeRelationship,
                                    localOnly, stopProperty, includeStopProperty,
                                    this, &targetIndex, deletedPaths,
                                    allErrors );
        paths->swap(targetIndex.paths);
    };

    if (IsUsd()) {
        // USD does not cache property indexes, but we can still build one
        // to get the relationship targets.
        PcpPropertyIndex propIndex;
        PcpBuildPropertyIndex(relPath, this, &propIndex, allErrors);
        computeTargets(propIndex);
    } else {
        computeTargets(ComputePropertyIndex(relPath, allErrors));
    }
}

void
PcpCache::ComputeAttributeConnectionPaths(const SdfPath & attrPath, 
                                          SdfPathVector *paths,
                                          bool localOnly,
                                          const SdfSpecHandle &stopProperty,
                                          bool includeStopProperty,
                                          SdfPathVector *deletedPaths,
                                          PcpErrorVector *allErrors)
{
    TRACE_FUNCTION();

    if (!attrPath.IsPropertyPath()) {
        TF_CODING_ERROR(
            "Path <%s> must be an attribute path", attrPath.GetText());
        return;
    }

    auto computeTargets = [&](const PcpPropertyIndex &propIndex) {
        PcpTargetIndex targetIndex;
        PcpBuildFilteredTargetIndex( PcpSite(GetLayerStackIdentifier(), attrPath),
                                    propIndex,
                                    SdfSpecTypeAttribute,
                                    localOnly, stopProperty, includeStopProperty,
                                    this, &targetIndex,  deletedPaths,
                                    allErrors );
        paths->swap(targetIndex.paths);
    };

    if (IsUsd()) {
        // USD does not cache property indexes, but we can still build one
        // to get the attribute connections.
        PcpPropertyIndex propIndex;
        PcpBuildPropertyIndex(attrPath, this, &propIndex, allErrors);
        computeTargets(propIndex);
    } else {
        computeTargets(ComputePropertyIndex(attrPath, allErrors));
    }
}

const PcpPropertyIndex *
PcpCache::FindPropertyIndex(const SdfPath & path) const
{
    return _GetPropertyIndex(path);
}

SdfLayerHandleSet
PcpCache::GetUsedLayers() const
{
    SdfLayerHandleSet rval = _primDependencies->GetUsedLayers();

    // Dependencies don't include the local layer stack, so manually add those
    // layers here.
    if (_layerStack) {
        const SdfLayerRefPtrVector& localLayers = _layerStack->GetLayers();
        rval.insert(localLayers.begin(), localLayers.end());
    }
    return rval;
}

size_t
PcpCache::GetUsedLayersRevision() const
{
    return _primDependencies->GetLayerStacksRevision();
}

SdfLayerHandleSet
PcpCache::GetUsedRootLayers() const
{
    SdfLayerHandleSet rval = _primDependencies->GetUsedRootLayers();

    // Dependencies don't include the local layer stack, so manually add the
    // local root layer here.
    rval.insert(_rootLayer);
    return rval;
}

PcpDependencyVector
PcpCache::FindSiteDependencies(
    const SdfLayerHandle& layer,
    const SdfPath& sitePath,
    PcpDependencyFlags depMask,
    bool recurseOnSite,
    bool recurseOnIndex,
    bool filterForExistingCachesOnly
    ) const
{
    PcpDependencyVector result;
    for (const auto& layerStack: FindAllLayerStacksUsingLayer(layer)) {
        PcpDependencyVector deps = FindSiteDependencies(
            layerStack, sitePath, depMask, recurseOnSite, recurseOnIndex,
            filterForExistingCachesOnly);
        for (PcpDependency dep: deps) {
            // Fold in any sublayer offset.
            if (const SdfLayerOffset *sublayerOffset =
                layerStack->GetLayerOffsetForLayer(layer)) {
                dep.mapFunc = dep.mapFunc.ComposeOffset(*sublayerOffset);
            }
            result.push_back(std::move(dep));
        }
    }
    return result;
}

namespace
{

template <class CacheFilterFn>
static void
_ProcessDependentNode(
    const PcpNodeRef& node, const SdfPath& localSitePath, 
    const CacheFilterFn& cacheFilterFn,
    PcpDependencyVector* deps)
{
    // Now that we have found a dependency on depPrimSitePath,
    // use path translation to get the corresponding depIndexPath.
    SdfPath depIndexPath;
    bool valid = false;
    if (node.GetArcType() == PcpArcTypeRelocate) {
        // Relocates require special handling.  Because
        // a relocate node's map function is always
        // identity, we must do our own prefix replacement
        // to step out of the relocate, then continue
        // with regular path translation. We must step out of all 
        // consecutive relocate nodes since they all only hold the 
        // identity mapping. Once we hit a non-relocates node, any
        // relocates above that will be accounted for in the map to 
        // root function
        PcpNodeRef parent = node.GetParentNode();
        while (parent.GetArcType() == PcpArcTypeRelocate) {
            parent = parent.GetParentNode();
        }
        depIndexPath = PcpTranslatePathFromNodeToRoot(
            parent,
            localSitePath.ReplacePrefix( node.GetPath(),
                                         parent.GetPath() ),
            &valid );
    } else {
        depIndexPath = PcpTranslatePathFromNodeToRoot(
            node, localSitePath, &valid);
    }

    if (valid && TF_VERIFY(!depIndexPath.IsEmpty()) &&
        cacheFilterFn(depIndexPath)) {
        deps->push_back(PcpDependency{
            depIndexPath, localSitePath,
            node.GetMapToRoot().Evaluate() });
    }
}

template <class CacheFilterFn>
static void
_ProcessCulledDependency(
    const PcpCulledDependency& dep, const SdfPath& localSitePath,
    const CacheFilterFn& cacheFilterFn,
    PcpDependencyVector* deps)
{
    // This function mirrors _ProcessDependentNode above, see comments
    // in that function for more details.
    SdfPath depIndexPath;
    bool valid = false;
    if (!dep.unrelocatedSitePath.IsEmpty()) {
        depIndexPath = PcpTranslatePathFromNodeToRootUsingFunction(
            dep.mapToRoot, 
            localSitePath.ReplacePrefix(dep.sitePath,
                                        dep.unrelocatedSitePath),
            &valid);
    }
    else {
        depIndexPath = PcpTranslatePathFromNodeToRootUsingFunction(
            dep.mapToRoot, localSitePath, &valid);
    }

    if (valid && TF_VERIFY(!depIndexPath.IsEmpty()) &&
        cacheFilterFn(depIndexPath)) {
        deps->push_back(PcpDependency{
            depIndexPath, localSitePath,
            dep.mapToRoot });
    }
}

} // end anonymous namespace

PcpDependencyVector
PcpCache::FindSiteDependencies(
    const PcpLayerStackPtr& siteLayerStack,
    const SdfPath& sitePath,
    PcpDependencyFlags depMask,
    bool recurseOnSite,
    bool recurseOnIndex,
    bool filterForExistingCachesOnly
    ) const
{
    TRACE_FUNCTION();

    PcpDependencyVector deps;

    //
    // Validate arguments.
    //
    if (!(depMask & (PcpDependencyTypeVirtual|PcpDependencyTypeNonVirtual))) {
        TF_CODING_ERROR("depMask must include at least one of "
                        "{PcpDependencyTypeVirtual, "
                        "PcpDependencyTypeNonVirtual}");
        return deps;
    }
    if (!(depMask & (PcpDependencyTypeRoot | PcpDependencyTypeDirect |
                     PcpDependencyTypeAncestral))) {
        TF_CODING_ERROR("depMask must include at least one of "
                        "{PcpDependencyTypeRoot, "
                        "PcpDependencyTypePurelyDirect, "
                        "PcpDependencyTypePartlyDirect, "
                        "PcpDependencyTypeAncestral}");
        return deps;
    }
    if ((depMask & PcpDependencyTypeRoot) &&
        !(depMask & PcpDependencyTypeNonVirtual)) {
        // Root deps are only ever non-virtual.
        TF_CODING_ERROR("depMask of PcpDependencyTypeRoot requires "
                        "PcpDependencyTypeNonVirtual");
        return deps;
    }
    if (siteLayerStack->_registry != _layerStackCache) {
        TF_CODING_ERROR("PcpLayerStack does not belong to this PcpCache");
        return deps;
    }

    // Filter function for dependencies to return.
    auto cacheFilterFn = [this, filterForExistingCachesOnly]
        (const SdfPath &indexPath) {
            if (!filterForExistingCachesOnly) {
                return true;
            } else if (indexPath.IsAbsoluteRootOrPrimPath()) {
                return bool(FindPrimIndex(indexPath));
            } else if (indexPath.IsPropertyPath()) {
                return bool(FindPropertyIndex(indexPath));
            } else {
                return false;
            }
        };

    // Dependency arcs expressed in scene description connect prim
    // paths, prim variant paths, and absolute paths only. Those arcs
    // imply dependency structure for children, such as properties.
    // To service dependency queries about those children, we must
    // examine structure at the enclosing prim/root level where deps
    // are expresed. Find the containing path.
    SdfPath tmpPath;
    const SdfPath *sitePrimPath = &sitePath;
    if (ARCH_UNLIKELY(!sitePath.IsPrimOrPrimVariantSelectionPath())) {
        tmpPath = (sitePath == SdfPath::AbsoluteRootPath()) ? sitePath :
            sitePath.GetPrimOrPrimVariantSelectionPath();
        sitePrimPath = &tmpPath;
    }

    // Handle the root dependency.
    // Sites containing variant selections are never root dependencies.
    if (depMask & PcpDependencyTypeRoot &&
        siteLayerStack == _layerStack &&
        !sitePath.ContainsPrimVariantSelection() &&
        cacheFilterFn(sitePath)) {
        deps.push_back(PcpDependency{
            sitePath, sitePath, PcpMapFunction::Identity()});
    }

    // Handle dependencies stored in _primDependencies.
    auto visitSiteFn = [&](const SdfPath &depPrimIndexPath,
                           const SdfPath &depPrimSitePath)
    {
        // Because arc dependencies are analyzed in terms of prims,
        // if we are querying deps for a property, and recurseOnSite
        // is true, we must guard against recursing into paths
        // that are siblings of the property and filter them out.
        if (depPrimSitePath != *sitePrimPath &&
            depPrimSitePath.HasPrefix(*sitePrimPath) &&
            !depPrimSitePath.HasPrefix(sitePath)) {
            return;
        }

        // If we have recursed above to an ancestor, include its direct
        // dependencies, since they are considered ancestral by descendants.
        const PcpDependencyFlags localMask =
            (depPrimSitePath != *sitePrimPath &&
             sitePrimPath->HasPrefix(depPrimSitePath))
            ? (depMask | PcpDependencyTypeDirect) : depMask;

        // If we have recursed below sitePath, use that site;
        // otherwise use the site the caller requested.
        const SdfPath localSitePath =
            (depPrimSitePath != *sitePrimPath &&
             depPrimSitePath.HasPrefix(*sitePrimPath))
            ? depPrimSitePath : sitePath;

        auto visitNodeFn = [&](const SdfPath &depPrimIndexPath,
                               const PcpNodeRef &node)
        {
            // Skip computing the node's dependency type if we aren't looking
            // for a specific type -- that computation can be expensive.
            if (localMask != PcpDependencyTypeAnyIncludingVirtual) {
                PcpDependencyFlags flags = PcpClassifyNodeDependency(node);
                if ((flags & localMask) != flags) { 
                    return;
                }
            }

            _ProcessDependentNode(node, localSitePath, cacheFilterFn, &deps);
        };

        auto visitCulledDepFn = [&](const SdfPath &depPrimIndexPath,
                                    const PcpCulledDependency &dep)
        {
            if (localMask != PcpDependencyTypeAnyIncludingVirtual) {
                PcpDependencyFlags flags = dep.flags;
                if ((flags & localMask) != flags) { 
                    return;
                }
            }

            _ProcessCulledDependency(dep, localSitePath, cacheFilterFn, &deps);
        };

        Pcp_ForEachDependentNode(depPrimSitePath, siteLayerStack,
            depPrimIndexPath, *this, visitNodeFn, visitCulledDepFn);
    };
    _primDependencies->ForEachDependencyOnSite(
        siteLayerStack, *sitePrimPath,
        /* includeAncestral = */ depMask & PcpDependencyTypeAncestral,
        recurseOnSite, visitSiteFn);

    // If recursing down namespace, we may have cache entries for
    // descendants that did not introduce new dependency arcs, and
    // therefore were not encountered above, but which nonetheless
    // represent dependent paths.  Add them if requested.
    if (recurseOnIndex) {
        TRACE_SCOPE("PcpCache::FindSiteDependencies - recurseOnIndex");
        SdfPathSet seenDeps;
        PcpDependencyVector expandedDeps;

        for(const PcpDependency &dep: deps) {
            const SdfPath & indexPath = dep.indexPath;

            auto it = seenDeps.upper_bound(indexPath);
            if (it != seenDeps.begin()) {
                --it;
                if (indexPath.HasPrefix(*it)) {
                    // Short circuit further expansion; expect we
                    // have already recursed below this path.
                    continue;
                }
            }

            seenDeps.insert(indexPath);
            expandedDeps.push_back(dep);
            // Recurse on child index entries.
            if (indexPath.IsAbsoluteRootOrPrimPath()) {
                auto primRange =
                    _primIndexCache.FindSubtreeRange(indexPath);
                if (primRange.first != primRange.second) {
                    // Skip initial entry, since we've already added it 
                    // to expandedDeps above.
                    ++primRange.first;
                }

                for (auto entryIter = primRange.first;
                     entryIter != primRange.second; ++entryIter) {
                    const SdfPath& subPath = entryIter->first;
                    const PcpPrimIndex& subPrimIndex = entryIter->second;
                    if (subPrimIndex.IsValid()) {
                        expandedDeps.push_back(PcpDependency{
                            subPath,
                            subPath.ReplacePrefix(indexPath, dep.sitePath),
                            dep.mapFunc});
                    }
                }
            }
            // Recurse on child property entries.
            const auto propRange =
                _propertyIndexCache.FindSubtreeRange(indexPath);
            for (auto entryIter = propRange.first;
                 entryIter != propRange.second; ++entryIter) {
                const SdfPath& subPath = entryIter->first;
                const PcpPropertyIndex& subPropIndex = entryIter->second;
                if (!subPropIndex.IsEmpty()) {
                    expandedDeps.push_back(PcpDependency{
                        subPath,
                        subPath.ReplacePrefix(indexPath, dep.sitePath),
                        dep.mapFunc});
                }
            }
        }
        std::swap(deps, expandedDeps);
    }

    return deps;
}

bool
PcpCache::CanHaveOpinionForSite(
    const SdfPath& localPcpSitePath,
    const SdfLayerHandle& layer,
    SdfPath* allowedPathInLayer) const
{
    // Get the prim index.
    if (const PcpPrimIndex* primIndex = _GetPrimIndex(localPcpSitePath)) {
        // We only want to check any layer stack for layer once.
        std::set<PcpLayerStackPtr> visited;

        // Iterate over all nodes.
        for (const PcpNodeRef &node: primIndex->GetNodeRange()) {
            // Ignore nodes that don't provide specs.
            if (node.CanContributeSpecs()) {
                // Check each layer stack that contributes specs only once.
                if (visited.insert(node.GetLayerStack()).second) {
                    // Check for layer.
                    TF_FOR_ALL(i, node.GetLayerStack()->GetLayers()) {
                        if (*i == layer) {
                            if (allowedPathInLayer) {
                                *allowedPathInLayer = node.GetPath();
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

std::vector<std::string>
PcpCache::GetInvalidSublayerIdentifiers() const
{
    TRACE_FUNCTION();

    std::set<std::string> result;

    std::vector<PcpLayerStackPtr> allLayerStacks =
        _layerStackCache->GetAllLayerStacks();

    TF_FOR_ALL(layerStack, allLayerStacks) {
        // Scan errors for a sublayer error.
        PcpErrorVector errs = (*layerStack)->GetLocalErrors();
        TF_FOR_ALL(e, errs) {
            if (PcpErrorInvalidSublayerPathPtr typedErr =
                std::dynamic_pointer_cast<PcpErrorInvalidSublayerPath>(*e)){
                result.insert(typedErr->sublayerPath);
            }
        }
    }

    return std::vector<std::string>( result.begin(), result.end() );
}

bool 
PcpCache::IsInvalidSublayerIdentifier(const std::string& identifier) const
{
    TRACE_FUNCTION();

    std::vector<std::string> layers = GetInvalidSublayerIdentifiers();
    std::vector<std::string>::const_iterator i =
        std::find(layers.begin(), layers.end(), identifier);
    return i != layers.end();
}

std::map<SdfPath, std::vector<std::string>, SdfPath::FastLessThan>
PcpCache::GetInvalidAssetPaths() const
{
    TRACE_FUNCTION();

    std::map<SdfPath, std::vector<std::string>, SdfPath::FastLessThan> result;

    TF_FOR_ALL(it, _primIndexCache) {
        const SdfPath& primPath = it->first;
        const PcpPrimIndex& primIndex = it->second;
        if (primIndex.IsValid()) {
            PcpErrorVector errors = primIndex.GetLocalErrors();
            for (const auto& e : errors) {
                if (PcpErrorInvalidAssetPathPtr typedErr =
                    std::dynamic_pointer_cast<PcpErrorInvalidAssetPath>(e)){
                    result[primPath].push_back(typedErr->resolvedAssetPath);
                }
            }
        }
    }

    return result;
}

bool
PcpCache::IsInvalidAssetPath(const std::string& resolvedAssetPath) const
{
    TRACE_FUNCTION();

    std::map<SdfPath, std::vector<std::string>, SdfPath::FastLessThan>
        pathMap = GetInvalidAssetPaths();
    TF_FOR_ALL(i, pathMap) {
        TF_FOR_ALL(j, i->second) {
            if (*j == resolvedAssetPath) {
                return true;
            }
        }
    }
    return false;
}

bool 
PcpCache::HasAnyDynamicFileFormatArgumentFieldDependencies() const
{
    return 
        _primDependencies->HasAnyDynamicFileFormatArgumentFieldDependencies();
}

bool 
PcpCache::HasAnyDynamicFileFormatArgumentAttributeDependencies() const
{
    return 
        _primDependencies->HasAnyDynamicFileFormatArgumentAttributeDependencies();
}

bool 
PcpCache::IsPossibleDynamicFileFormatArgumentField(
    const TfToken &field) const
{
    return _primDependencies->IsPossibleDynamicFileFormatArgumentField(field);
}

bool 
PcpCache::IsPossibleDynamicFileFormatArgumentAttribute(
    const TfToken &attributeName) const
{
    return _primDependencies->IsPossibleDynamicFileFormatArgumentAttribute(
        attributeName);
}

const PcpDynamicFileFormatDependencyData &
PcpCache::GetDynamicFileFormatArgumentDependencyData(
    const SdfPath &primIndexPath) const
{
    return _primDependencies->GetDynamicFileFormatArgumentDependencyData(
        primIndexPath);
}

const SdfPathVector&
PcpCache::GetPrimsUsingExpressionVariablesFromLayerStack(
    const PcpLayerStackPtr &layerStack) const
{
    return _primDependencies->GetPrimsUsingExpressionVariablesFromLayerStack(
        layerStack);
}

const std::unordered_set<std::string>& 
PcpCache::GetExpressionVariablesFromLayerStackUsedByPrim(
    const SdfPath &primIndexPath,
    const PcpLayerStackPtr &layerStack) const
{
    return _primDependencies->GetExpressionVariablesFromLayerStackUsedByPrim(
        primIndexPath, layerStack);
}

void
PcpCache::Apply(const PcpCacheChanges& changes, PcpLifeboat* lifeboat)
{
    TRACE_FUNCTION();

    // Check for special case of blowing everything.
    if (changes.didChangeSignificantly.count(SdfPath::AbsoluteRootPath())) {
        // Clear everything for scene graph objects.
        _primIndexCache.clear();
        _propertyIndexCache.clear();
        _primDependencies->RemoveAll(lifeboat);
    }
    else {
        // If layers may have changed, inform _primDependencies.
        if (changes.didMaybeChangeLayers) {
            _primDependencies->LayerStacksChanged();
        }

        // Blow prim and property indexes due to prim graph changes.
        TF_FOR_ALL(i, changes.didChangeSignificantly) {
            const SdfPath& path = *i;
            if (path.IsPrimPath()) {
                _RemovePrimAndPropertyCaches(path, lifeboat);
            }
            else {
                _RemovePropertyCaches(path, lifeboat);
            }
        }

        // Blow prim and property indexes due to prim graph changes.
        TF_FOR_ALL(i, changes.didChangePrims) {
            _RemovePrimCache(*i, lifeboat);
            _RemovePropertyCaches(*i, lifeboat);
        }

        // Blow property stacks and update spec dependencies on prims.
        auto updateSpecStacks = [this, &lifeboat, &changes](const SdfPath& path) {
            if (path.IsAbsoluteRootOrPrimPath()) {
                // We've possibly changed the prim spec stack.  Note that
                // we may have blown the prim index so check that it exists.
                if (PcpPrimIndex* primIndex = _GetPrimIndex(path)) {
                    Pcp_RescanForSpecs(primIndex, IsUsd(),
                                       /* updateHasSpecs */ true,
                                       &changes);

                    // If there are no specs left then we can discard the
                    // prim index.
                    bool anyNodeHasSpecs = false;
                    for (const PcpNodeRef &node: primIndex->GetNodeRange()) {
                        if (node.HasSpecs()) {
                            anyNodeHasSpecs = true;
                            break;
                        }
                    }
                    if (!anyNodeHasSpecs) {
                        _RemovePrimAndPropertyCaches(path, lifeboat);
                    }
                }
            }
            else if (path.IsPropertyPath()) {
                _RemovePropertyCache(path, lifeboat);
            }
            else if (path.IsTargetPath()) {
                // We have potentially added or removed a relationship target
                // spec.  This invalidates the property stack for any
                // relational attributes for this target.
                _RemovePropertyCaches(path, lifeboat);
            }
        };

        TF_FOR_ALL(i, changes.didChangeSpecs) {
            updateSpecStacks(*i);
        }

        TF_FOR_ALL(i, changes._didChangeSpecsInternal) {
            updateSpecStacks(*i);
        }

        // Ensure that all relevant paths that have been affected by
        // sublayer operations have their prim stacks updated
        TF_FOR_ALL(i, changes._didChangePrimSpecsAndChildrenInternal) {
            auto range = _primIndexCache.FindSubtreeRange(*i);
            for (auto i = range.first; i != range.second; ++i) {
                if (PcpPrimIndex* primIndex = _GetPrimIndex(i->first)) {
                    Pcp_RescanForSpecs(primIndex, IsUsd(),
                                       /* updateHasSpecs */ true,
                                       &changes);
                }
            }
        }

        // Fix the keys for any prim or property under any of the renamed
        // paths.
        // XXX: It'd be nice if this was a usd by just adjusting
        //      paths here and there.
        // First blow all caches under the new names.
        TF_FOR_ALL(i, changes.didChangePath) {
            if (!i->second.IsEmpty()) {
                _RemovePrimAndPropertyCaches(i->second, lifeboat);
            }
        }
        // XXX: Blow the caches at the old names.  We'd rather just
        //      adjust paths here and there in the prim graphs and the
        //      SdfPathTable keys, but the latter isn't possible yet
        //      and the former is inconvenient.
        TF_FOR_ALL(i, changes.didChangePath) {
            _RemovePrimAndPropertyCaches(i->first, lifeboat);
        }
    }

    // Fix up payload paths.  First remove everything we renamed then add
    // the new names.  This avoids any problems where we rename both from
    // and to a path, e.g. B -> C, A -> B.
    // XXX: This is a loop over both the changes and all included
    //      payloads because we have no way to find a prefix in a
    //      hash set of payload paths.  We could store SdfPathSet
    //      but at an increased cost when testing if any given
    //      path is in the set.  We'd have to benchmark to see if
    //      this is more costly or that would be.
    static const bool fixTargetPaths = true;
    std::vector<SdfPath> newIncludes;
    // Path changes are in the order in which they were processed so we know
    // the difference between a rename from B -> C followed by A -> B as opposed
    // to from A -> B, B -> C.
    TF_FOR_ALL(i, changes.didChangePath) {
        for (PayloadSet::iterator j = _includedPayloads.begin();
                j != _includedPayloads.end(); ) {
            // If the payload path has the old path as a prefix then remove
            // the payload path and add the payload path with the old path
            // prefix replaced by the new path.  We don't fix target paths
            // because there can't be any on a payload path.
            if (j->HasPrefix(i->first)) {
                newIncludes.push_back(j->ReplacePrefix(i->first, i->second,
                                                       !fixTargetPaths));
                _includedPayloads.erase(j++);
            }
            else {
                ++j;
            }
        }
        // Because we could have a chain of renames like A -> B, B -> C, we also
        // need to check the newIncludes. Any payloads prefixed by A will have
        // been removed from _includedPayloads and renamed B in newIncludes 
        // during the A -> B pass, so the B -> C pass needs to rename all the
        // B prefixed paths in newIncludes to complete the full rename.
        for (SdfPath &newInclude : newIncludes) {
            if (newInclude.HasPrefix(i->first)) {
                // The rename can happen in place.
                newInclude = newInclude.ReplacePrefix(i->first, i->second,
                                                       !fixTargetPaths);
            }
        }
    }
    _includedPayloads.insert(newIncludes.begin(), newIncludes.end());
}

void
PcpCache::Reload(PcpChanges* changes)
{
    TRACE_FUNCTION();

    if (!_layerStack) {
        return;
    }

    ArResolverContextBinder binder(_layerStackIdentifier.pathResolverContext);

    // Reload every invalid sublayer and asset we know about,
    // in any layer stack or prim index.
    std::vector<PcpLayerStackPtr> allLayerStacks =
        _layerStackCache->GetAllLayerStacks();
    TF_FOR_ALL(layerStack, allLayerStacks) {
        const PcpErrorVector errors = (*layerStack)->GetLocalErrors();
        for (const auto& e : errors) {
            if (PcpErrorInvalidSublayerPathPtr typedErr =
                std::dynamic_pointer_cast<PcpErrorInvalidSublayerPath>(e)) {
                changes->DidMaybeFixSublayer(this,
                                             typedErr->layer,
                                             typedErr->sublayerPath);
            }
        }
    }
    TF_FOR_ALL(it, _primIndexCache) {
        const PcpPrimIndex& primIndex = it->second;
        if (primIndex.IsValid()) {
            const PcpErrorVector errors = primIndex.GetLocalErrors();
            for (const auto& e : errors) {
                if (PcpErrorInvalidAssetPathPtr typedErr =
                    std::dynamic_pointer_cast<PcpErrorInvalidAssetPath>(e)) {
                    changes->DidMaybeFixAsset(this,
                                              typedErr->site,
                                              typedErr->sourceLayer,
                                              typedErr->resolvedAssetPath);
                }
            }
        }
    }

    // Reload every layer we've reached except the session layers (which we
    // never want to reload from disk).
    SdfLayerHandleSet layersToReload = GetUsedLayers();

    for (const SdfLayerHandle &layer : _layerStack->GetSessionLayers()) {
        layersToReload.erase(layer);
    }

    SdfLayer::ReloadLayers(layersToReload);
}

void
PcpCache::ReloadReferences(PcpChanges* changes, const SdfPath& primPath)
{
    TRACE_FUNCTION();

    ArResolverContextBinder binder(_layerStackIdentifier.pathResolverContext);

    // Traverse every PrimIndex at or under primPath to find
    // InvalidAssetPath errors, and collect the unique layer stacks used.
    std::set<PcpLayerStackPtr> layerStacksAtOrUnderPrim;
    const auto range = _primIndexCache.FindSubtreeRange(primPath);
    for (auto entryIter = range.first; entryIter != range.second; ++entryIter) {
        const auto& entry = *entryIter;
        const PcpPrimIndex& primIndex = entry.second;
        if (primIndex.IsValid()) {
            PcpErrorVector errors = primIndex.GetLocalErrors();
            for (const auto& e : errors) {
                if (PcpErrorInvalidAssetPathPtr typedErr =
                    std::dynamic_pointer_cast<PcpErrorInvalidAssetPath>(e))
                {
                    changes->DidMaybeFixAsset(this, typedErr->site,
                                              typedErr->sourceLayer,
                                              typedErr->resolvedAssetPath);
                }
            }
            for (const PcpNodeRef &node: primIndex.GetNodeRange()) {
                layerStacksAtOrUnderPrim.insert( node.GetSite().layerStack );
            }
        }
    }

    // Check each used layer stack (gathered above) for invalid sublayers.
    for (const PcpLayerStackPtr& layerStack: layerStacksAtOrUnderPrim) {
        // Scan errors for a sublayer error.
        PcpErrorVector errs = layerStack->GetLocalErrors();
        for (const PcpErrorBasePtr &err: errs) {
            if (PcpErrorInvalidSublayerPathPtr typedErr =
                std::dynamic_pointer_cast<PcpErrorInvalidSublayerPath>(err)){
                changes->DidMaybeFixSublayer(this, typedErr->layer,
                                             typedErr->sublayerPath);
            }
        }
    }

    // Reload every layer used by prims at or under primPath, except for
    // local layers.
    SdfLayerHandleSet layersToReload;
    for (const PcpLayerStackPtr& layerStack: layerStacksAtOrUnderPrim) {
        for (const auto& layer: layerStack->GetLayers()) {
            if (!_layerStack->HasLayer(layer)) {
                layersToReload.insert(layer);
            }
        }
    }

    SdfLayer::ReloadLayers(layersToReload);
}

void
PcpCache::_RemovePrimCache(const SdfPath& primPath, PcpLifeboat* lifeboat)
{
    _PrimIndexCache::iterator it = _primIndexCache.find(primPath);
    if (it != _primIndexCache.end()) {
        _primDependencies->Remove(it->second, lifeboat);
        PcpPrimIndex empty;
        it->second.Swap(empty);
    }
}

void
PcpCache::_RemovePrimAndPropertyCaches(const SdfPath& root,
                                       PcpLifeboat* lifeboat)
{
    std::pair<_PrimIndexCache::iterator, _PrimIndexCache::iterator> range =
        _primIndexCache.FindSubtreeRange(root);
    for (_PrimIndexCache::iterator i = range.first; i != range.second; ++i) {
        _primDependencies->Remove(i->second, lifeboat);
    }
    if (range.first != range.second) {
        _primIndexCache.erase(range.first);
    }

    // Remove all properties under any removed prim.
    _RemovePropertyCaches(root, lifeboat);
}

void 
PcpCache::_RemovePropertyCache(const SdfPath& root, PcpLifeboat* lifeboat)
{
    _PropertyIndexCache::iterator it = _propertyIndexCache.find(root);
    if (it != _propertyIndexCache.end()) {
        PcpPropertyIndex empty;
        it->second.Swap(empty);
    }
}

void
PcpCache::_RemovePropertyCaches(const SdfPath& root, PcpLifeboat* lifeboat)
{
    std::pair<_PropertyIndexCache::iterator,
              _PropertyIndexCache::iterator> range =
        _propertyIndexCache.FindSubtreeRange(root);

    if (range.first != range.second) {
        _propertyIndexCache.erase(range.first);
    }
}

////////////////////////////////////////////////////////////////////////
// Private helper methods.

void
PcpCache::_ForEachLayerStack(
    const TfFunctionRef<void(const PcpLayerStackPtr&)>& fn) const
{
    _layerStackCache->ForEachLayerStack(fn);
}

void
PcpCache::_ForEachPrimIndex(
    const TfFunctionRef<void(const PcpPrimIndex&)>& fn) const
{
    for (const auto& entry : _primIndexCache) {
        const PcpPrimIndex& primIndex = entry.second;
        if (primIndex.IsValid()) {
            fn(primIndex);
        }
    }
}

PcpPrimIndex*
PcpCache::_GetPrimIndex(const SdfPath& path)
{
    _PrimIndexCache::iterator i = _primIndexCache.find(path);
    if (i != _primIndexCache.end()) {
        PcpPrimIndex &primIndex = i->second;
        if (primIndex.IsValid()) {
            return &primIndex;
        }
    }
    return NULL;
}

const PcpPrimIndex*
PcpCache::_GetPrimIndex(const SdfPath& path) const
{
    _PrimIndexCache::const_iterator i = _primIndexCache.find(path);
    if (i != _primIndexCache.end()) {
        const PcpPrimIndex &primIndex = i->second;
        if (primIndex.IsValid()) {
            return &primIndex;
        }
    }
    return NULL;
}

struct PcpCache::_ParallelIndexer
{
    using This = _ParallelIndexer;

    explicit _ParallelIndexer(PcpCache *cache,
                              const PcpLayerStackPtr &layerStack)
        : _cache(cache)
        , _layerStack(layerStack)
        , _resolver(ArGetResolver()) {
        _isPublishing = false;
    }

    void Prepare(_UntypedIndexingChildrenPredicate childrenPred,
                 PcpPrimIndexInputs baseInputs,
                 PcpErrorVector *allErrors,
                 const ArResolverScopedCache* parentCache,
                 const char *mallocTag1,
                 const char *mallocTag2) {
        _childrenPredicate = childrenPred;
        _baseInputs = baseInputs;
        // Set the includedPayloadsMutex in _baseInputs.
        _baseInputs.IncludedPayloadsMutex(&_includedPayloadsMutex);
        _allErrors = allErrors;
        _parentCache = parentCache;
        _mallocTag1 = mallocTag1;
        _mallocTag2 = mallocTag2;

        // Clear the roots to compute.
        _toCompute.clear();
    }

    // Run the added work and wait for it to complete.
    void RunAndWait() {
        WorkWithScopedParallelism([this]() {
                Pcp_Dependencies::ConcurrentPopulationContext
                    populationContext(*_cache->_primDependencies);
                TF_FOR_ALL(i, _toCompute) {
                    _dispatcher.Run(&This::_ComputeIndex, this,
                                    i->first, i->second, /*checkCache=*/true);
                }
                _dispatcher.Wait();
                // Publish any remaining outputs.
                _PublishOutputs();
            });

        // Clear out results & working space.  If stuff is huge, dump it
        // asynchronously, otherwise clear in place to possibly reuse heap for
        // future calls.
        constexpr size_t MaxSize = 1024;
        _ClearMaybeAsync(_toCompute, _toCompute.size() >= MaxSize);
    }

    // Add an index to compute.
    void ComputeIndex(const PcpPrimIndex *parentIndex, const SdfPath &path) {
        TF_AXIOM(parentIndex || path == SdfPath::AbsoluteRootPath());
        _toCompute.push_back(make_pair(parentIndex, path));
    }

  private:

    template <class Container>
    void _ClearMaybeAsync(Container &c, bool async) {
        if (async) {
            WorkMoveDestroyAsync(c);
        }
        else {
            c.clear();
        }
    }        

    // This function is run in parallel by the _dispatcher.  It computes prim
    // indexes and publishes them to the cache.
    void _ComputeIndex(const PcpPrimIndex *parentIndex,
                       SdfPath path, bool checkCache) {
        TfAutoMallocTag2  tag(_mallocTag1, _mallocTag2);
        ArResolverScopedCache taskCache(_parentCache);

        // Check to see if we already have an index for this guy.  If we do,
        // don't bother computing it.
        const PcpPrimIndex *index = nullptr;
        if (checkCache) {
            tbb::spin_rw_mutex::scoped_lock
                lock(_primIndexCacheMutex, /*write=*/false);
            PcpCache::_PrimIndexCache::const_iterator
                i = _cache->_primIndexCache.find(path);
            if (i == _cache->_primIndexCache.end()) {
                // There is no cache entry for this path or any children.
                checkCache = false;
            } else if (i->second.IsValid()) {
                // There is a valid cache entry.
                index = &i->second;
            } else {
                // There is a cache entry but it is invalid.  There still
                // may be valid cache entries for children, so we must
                // continue to checkCache.  An example is when adding a
                // new empty spec to a layer stack already used by a
                // prim, causing a culled node to no longer be culled,
                // and the children to be unaffected.
            }
        }

        if (!index) {
            // We didn't find an index in the cache, so we must compute one.
            
            // The indexing function produces these outputs (which includes the
            // resulting prim index).  However, we require a stable memory
            // address where the final prim index will reside so we can spawn
            // child tasks that reliably refer to the index as its parent.  So
            // below, after we've done the indexing, we'll move the index to a
            // path table node that we will eventually store in the PcpCache's
            // _primIndexCache.  This means that the following code cannot use
            // outputs.primIndex reliably after this move has occurred.  Instead
            // the following code should use the 'index' local variable.
            PcpPrimIndexOutputs outputs;
            _PrimIndexCache::NodeHandle outputIndexNode;
            
            // Establish inputs.
            PcpPrimIndexInputs inputs = _baseInputs;
            inputs.parentIndex = parentIndex;

            TF_VERIFY(parentIndex || path == SdfPath::AbsoluteRootPath());
        
            // Run indexing.
            PcpComputePrimIndex(
                path, _layerStack, inputs, &outputs, &_resolver);

            // Append any errors.
            if (!outputs.allErrors.empty()) {
                // Append errors.
                tbb::spin_mutex::scoped_lock lock(_allErrorsMutex);
                _allErrors->insert(_allErrors->end(),
                                   outputs.allErrors.begin(),
                                   outputs.allErrors.end());
            }

            // Update payload set if necessary.
            PcpPrimIndexOutputs::PayloadState
                payloadState = outputs.payloadState;
            if (payloadState == PcpPrimIndexOutputs::IncludedByPredicate ||
                payloadState == PcpPrimIndexOutputs::ExcludedByPredicate) {
                tbb::spin_rw_mutex::scoped_lock lock(_includedPayloadsMutex);
                if (payloadState == PcpPrimIndexOutputs::IncludedByPredicate) {
                    _cache->_includedPayloads.insert(path);
                }
                else {
                    _cache->_includedPayloads.erase(path);
                }
            }

            // The following code uses the computed index, but we store it in a
            // path table node handle that we'll put the cache later.  This way
            // the memory address stays stable for child indexing tasks, etc.
            // Note that this means the following code CANNOT use
            // 'outputs.index', as it is moved-from.  It must use 'index'
            // instead.
            outputIndexNode = _PrimIndexCache::NodeHandle::New(
                path, std::move(outputs.primIndex));
            index = &outputIndexNode.GetMapped();
        
            // Arrange to publish to cache.

            // If we are still checking the cache but we had to compute an
            // index, it means the one in the cache for this path was invalid.
            // In that case, we need to replace the invalid one in the cache,
            // and fetch a new index pointer synchronously.
            if (checkCache) {
                index = _PublishOneOutput(
                    {std::move(outputIndexNode), std::move(outputs)},
                    /*allowInvalid=*/true);
            }
            // Otherwise arrange to publish, but only do so if another thread
            // isn't already working on it.
            else {
                // Add this to the publishing queue.
                _toPublish.push(
                    {std::move(outputIndexNode), std::move(outputs)});
                // If another thread is already publishing, just let it handle
                // the job.  Otherwise try to take the publishing state from
                // false -> true, and if we do so, we'll do the publishing.
                bool isPublishing =
                    _isPublishing.load(std::memory_order_relaxed);
                if (!isPublishing &&
                    _isPublishing.compare_exchange_strong(isPublishing, true)) {
                    // We took _isPublishing to true, so publish.
                    _PublishOutputs();
                    _isPublishing = false;
                }
            }
        }   

        // Invoke the client's predicate to see if we should do children.
        TfTokenVector namesToCompose;
        if (_childrenPredicate(*index, &namesToCompose)) {
            // Compute the children paths and add new tasks for them.
            TfTokenVector names;
            PcpTokenSet prohibitedNames;
            index->ComputePrimChildNames(&names, &prohibitedNames);
            for (const auto& name : names) {
                if (!namesToCompose.empty() &&
                    std::find(namesToCompose.begin(), namesToCompose.end(), 
                              name) == namesToCompose.end()) {
                    continue;
                }

                _dispatcher.Run([this, index, path, name, checkCache]() {
                    _ComputeIndex(index, path.AppendChild(name), checkCache);
                });
            }
        }
        
    }

    PcpPrimIndex const *
    _PublishOneOutput(std::pair<_PrimIndexCache::NodeHandle,
                      PcpPrimIndexOutputs> &&outputItem,
                      bool allowInvalid) {
        tbb::spin_rw_mutex::scoped_lock lock(_primIndexCacheMutex);
        auto iresult = _cache->_primIndexCache.insert(
            std::move(outputItem.first));
        if (!iresult.second) {
            TF_VERIFY(allowInvalid && !iresult.first->second.IsValid(),
                      "PrimIndex <%s> already exists in cache",
                      iresult.first->first.GetAsString().c_str());
            // Replace the invalid index.
            iresult.first->second =
                std::move(outputItem.first.GetMutableMapped());
        }
        PcpPrimIndex *mutableIndex = &iresult.first->second;
        lock.release();
        _cache->_primDependencies->Add(
            *mutableIndex, 
            std::move(outputItem.second.culledDependencies),
            std::move(outputItem.second.dynamicFileFormatDependency),
            std::move(outputItem.second.expressionVariablesDependency));
        return mutableIndex;
    }
                     

    void _PublishOutputs() {
        TRACE_FUNCTION();
        // Publish.
        std::pair<_PrimIndexCache::NodeHandle, PcpPrimIndexOutputs> outputItem;
        while (_toPublish.try_pop(outputItem)) {
            _PublishOneOutput(std::move(outputItem), /*allowInvalid=*/false);
        }
    }
    
    // Fixed inputs.
    PcpCache * const _cache;
    const PcpLayerStackPtr _layerStack;
    ArResolver& _resolver;

    // Utils.
    tbb::spin_rw_mutex _primIndexCacheMutex;
    tbb::spin_rw_mutex _includedPayloadsMutex;
    WorkDispatcher _dispatcher;

    // Varying inputs.
    _UntypedIndexingChildrenPredicate _childrenPredicate;
    PcpPrimIndexInputs _baseInputs;
    PcpErrorVector *_allErrors;
    tbb::spin_mutex _allErrorsMutex;
    const ArResolverScopedCache* _parentCache;
    char const *_mallocTag1;
    char const *_mallocTag2;
    vector<pair<const PcpPrimIndex *, SdfPath> > _toCompute;
    tbb::concurrent_queue<
        std::pair<_PrimIndexCache::NodeHandle, PcpPrimIndexOutputs>
        > _toPublish;
    std::atomic<bool> _isPublishing;
};

void
PcpCache::_ComputePrimIndexesInParallel(
    const SdfPathVector &roots,
    PcpErrorVector *allErrors,
    _UntypedIndexingChildrenPredicate childrenPred,
    _UntypedIndexingPayloadPredicate payloadPred,
    const char *mallocTag1,
    const char *mallocTag2)
{
    if (!IsUsd()) {
        TF_CODING_ERROR("Computing prim indexes in parallel only supported "
                        "for USD caches.");
        return;
    }

    TF_PY_ALLOW_THREADS_IN_SCOPE();

    ArResolverScopedCache parentCache;
    TfAutoMallocTag2 tag(mallocTag1, mallocTag2);

    if (!_layerStack)
        ComputeLayerStack(GetLayerStackIdentifier(), allErrors);

    if (!_parallelIndexer) {
        _parallelIndexer.reset(new _ParallelIndexer(this, _layerStack));
    }
    
    _ParallelIndexer * const indexer = _parallelIndexer.get();

    // General strategy: Compute indexes recursively starting from roots, in
    // parallel.  When we've computed an index, ask the children predicate if we
    // should continue to compute its children indexes.  If so, we add all the
    // children as new tasks for threads to pick up.
    //
    // Once all the indexes are computed, add them to the cache and add their
    // dependencies to the dependencies structures.

    PcpPrimIndexInputs inputs = GetPrimIndexInputs();
    inputs.IncludePayloadPredicate(payloadPred);

    indexer->Prepare(childrenPred, inputs, allErrors, &parentCache,
                     mallocTag1, mallocTag2);
    
    for (const auto& rootPath : roots) {
        // Obtain the parent index, if this is not the absolute root.  Note that
        // the call to ComputePrimIndex below is not concurrency safe.
        const PcpPrimIndex *parentIndex =
            rootPath == SdfPath::AbsoluteRootPath() ? nullptr :
            &_ComputePrimIndexWithCompatibleInputs(
                rootPath.GetParentPath(), inputs, allErrors);
        indexer->ComputeIndex(parentIndex, rootPath);
    }

    // Do the indexing and wait for it to complete.
    indexer->RunAndWait();
}

const PcpPrimIndex &
PcpCache::ComputePrimIndex(const SdfPath & path, PcpErrorVector *allErrors) {
    return _ComputePrimIndexWithCompatibleInputs(
        path, GetPrimIndexInputs(), allErrors);
}

const PcpPrimIndex &
PcpCache::_ComputePrimIndexWithCompatibleInputs(
    const SdfPath & path, const PcpPrimIndexInputs &inputs,
    PcpErrorVector *allErrors)
{
    // NOTE:TRACE_FUNCTION() is too much overhead here.

    // Check for a cache hit. Default constructed PcpPrimIndex objects
    // may live in the SdfPathTable for paths that haven't yet been computed,
    // so we have to explicitly check for that.
    _PrimIndexCache::const_iterator i = _primIndexCache.find(path);
    if (i != _primIndexCache.end() && i->second.IsValid()) {
        return i->second;
    }

    TRACE_FUNCTION();

    if (!_layerStack) {
        ComputeLayerStack(GetLayerStackIdentifier(), allErrors);
    }

    // Run the prim indexing algorithm.
    PcpPrimIndexOutputs outputs;
    PcpComputePrimIndex(path, _layerStack, inputs, &outputs);
    allErrors->insert(
        allErrors->end(),
        outputs.allErrors.begin(),
        outputs.allErrors.end());

    // Add dependencies.
    _primDependencies->Add(
        outputs.primIndex, 
        std::move(outputs.culledDependencies),
        std::move(outputs.dynamicFileFormatDependency),
        std::move(outputs.expressionVariablesDependency));

    // Update _includedPayloads if we included a discovered payload.
    if (outputs.payloadState == PcpPrimIndexOutputs::IncludedByPredicate) {
        _includedPayloads.insert(path);
    }
    if (outputs.payloadState == PcpPrimIndexOutputs::ExcludedByPredicate) {
        _includedPayloads.erase(path);
    }

    // Save the prim index.
    PcpPrimIndex &cacheEntry = _primIndexCache[path];
    cacheEntry.Swap(outputs.primIndex);

    return cacheEntry;
}

PcpPropertyIndex*
PcpCache::_GetPropertyIndex(const SdfPath& path)
{
    _PropertyIndexCache::iterator i = _propertyIndexCache.find(path);
    if (i != _propertyIndexCache.end() && !i->second.IsEmpty()) {
        return &i->second;
    }

    return NULL;
}

const PcpPropertyIndex*
PcpCache::_GetPropertyIndex(const SdfPath& path) const
{
    _PropertyIndexCache::const_iterator i = _propertyIndexCache.find(path);
    if (i != _propertyIndexCache.end() && !i->second.IsEmpty()) {
        return &i->second;
    }
    return NULL;
}

const PcpPropertyIndex &
PcpCache::ComputePropertyIndex(const SdfPath & path, PcpErrorVector *allErrors)
{
    TRACE_FUNCTION();

    static PcpPropertyIndex nullIndex;
    if (!path.IsPropertyPath()) {
        TF_CODING_ERROR("Path <%s> must be a property path", path.GetText());
        return nullIndex;
    }
    if (_usd) {
        // Disable computation and cache of property indexes in USD mode.
        // Although PcpBuildPropertyIndex does support this computation in
        // USD mode, we do not want to pay the cost of caching these.
        //
        // XXX: Maybe we shouldn't explicitly disallow this, but let consumers
        //      decide if they want this; if they don't, they should just
        //      avoid calling ComputePropertyIndex?
        TF_CODING_ERROR("PcpCache will not compute a cached property index in "
                        "USD mode; use PcpBuildPropertyIndex() instead.  Path "
                        "was <%s>", path.GetText());
        return nullIndex;
    }

    // Check for a cache hit. Default constructed PcpPrimIndex objects
    // may live in the SdfPathTable for paths that haven't yet been computed,
    // so we have to explicitly check for that.
    PcpPropertyIndex &cacheEntry = _propertyIndexCache[path];
    if (cacheEntry.IsEmpty()) {
        PcpBuildPropertyIndex(path, this, &cacheEntry, allErrors);
    }
    return cacheEntry;
}

////////////////////////////////////////////////////////////////////////
// Diagnostics

void 
PcpCache::PrintStatistics() const
{
    Pcp_PrintCacheStatistics(this, std::cout);
}

PXR_NAMESPACE_CLOSE_SCOPE
