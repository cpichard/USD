//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdImaging/delegate.h"

#include "pxr/usdImaging/usdImaging/adapterRegistry.h"
#include "pxr/usdImaging/usdImaging/coordSysAdapter.h"
#include "pxr/usdImaging/usdImaging/debugCodes.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/instanceAdapter.h"
#include "pxr/usdImaging/usdImaging/materialAdapter.h"
#include "pxr/usdImaging/usdImaging/primAdapter.h"
#include "pxr/usdImaging/usdImaging/tokens.h"

#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/imaging/hd/basisCurvesTopology.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/enums.h"
#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/meshTopology.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/points.h"
#include "pxr/imaging/hd/primGather.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContext.h"

#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/kind/registry.h"

#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/modelAPI.h"

#include "pxr/usd/usdLux/lightAPI.h"
#include "pxr/usd/usdLux/lightFilter.h"

#include "pxr/base/work/loops.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/pyLock.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/ostreamMethods.h"
#include "pxr/base/tf/stl.h"
#include "pxr/base/tf/type.h"

#include <functional>
#include <limits>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (lightFilterType)
    (LightAPI)
);

// This environment variable matches a set of similar ones in
// primAdapter.cpp, controlling other attribute caches.
TF_DEFINE_ENV_SETTING(USDIMAGING_ENABLE_DRAWMODE_CACHE, 1,
                      "Enable a cache for model:drawMode.");
static bool _IsEnabledDrawModeCache() {
    static bool _v = TfGetEnvSetting(USDIMAGING_ENABLE_DRAWMODE_CACHE) == 1;
    return _v;
}

// -------------------------------------------------------------------------- //
// Delegate Implementation.
// -------------------------------------------------------------------------- //
constexpr int UsdImagingDelegate::ALL_INSTANCES;

UsdImagingDelegate::UsdImagingDelegate(
        HdRenderIndex *parentIndex,
        SdfPath const& delegateID)
    : HdSceneDelegate(parentIndex, delegateID)
    , _primvarDescCache()
    , _rootXf(1.0)
    , _rootIsVisible(true)
    , _time(std::numeric_limits<double>::infinity())
    , _cameraPathForSampling()
    , _refineLevelFallback(0)
    , _reprFallback()
    , _cullStyleFallback(HdCullStyleDontCare)
    , _timeVaryingPrimCacheValid(false)
    , _xformCache(GetTime())
    , _materialBindingImplData(parentIndex->GetRenderDelegate()->
                               GetMaterialBindingPurpose())
    , _materialBindingCache(GetTime(), &_materialBindingImplData)
    , _visCache(GetTime())
    , _purposeCache() // note that purpose is uniform, so no GetTime()
    , _drawModeCache(GetTime())
    , _inheritedPrimvarCache()
    , _pointInstancerIndicesCache(GetTime())
    , _nonlinearSampleCountCache(GetTime())
    , _blurScaleCache(GetTime())
    , _displayRender(true)
    , _displayProxy(true)
    , _displayGuides(true)
    , _enableUsdDrawModes(true)
    , _hasDrawModeAdapter( UsdImagingAdapterRegistry::GetInstance()
                           .HasAdapter(UsdImagingAdapterKeyTokens
                                       ->drawModeAdapterKey) )
    , _sceneMaterialsEnabled(true)
    , _sceneLightsEnabled(true)
    , _appWindowPolicy(CameraUtilMatchVertically)
    , _coordSysEnabled(parentIndex
                       ->IsSprimTypeSupported(HdPrimTypeTokens->coordSys))
    , _displayUnloadedPrimsWithBounds(false)
{
}

UsdImagingDelegate::~UsdImagingDelegate()
{
    TfNotice::Revoke(_objectsChangedNoticeKey);

    if (_stage) {
        // Remove all prims conteributed by this delegate
        // from the render index.
        //
        // Even though this delegate is going out of scope
        // the render index may not be.  So, need to make
        // sure we properly remove all prims from the
        // render index.
        //
        // Note: This is not going through the adapters
        // as we are destroying the whole delegate.  It is
        // assumed that adapters are not shared between delegates.
        GetRenderIndex().RemoveSubtree(GetDelegateID(), this);
    }
}


bool
UsdImagingDelegate::_IsDrawModeApplied(UsdPrim const& prim)
{
    // Optionally draw unloaded prims as bounds.
    if (_displayUnloadedPrimsWithBounds && !prim.IsLoaded()) {
        return true;
    }

    // Early out if the prim isn't part of the model hierarchy.
    if (!prim.IsModel()) {
        return false;
    }

    // Compute the inherited drawMode.
    TfToken drawMode = _GetModelDrawMode(prim);
    // If draw mode is "default", no draw mode is applied.
    if (drawMode == UsdGeomTokens->default_) {
        return false;
    }

    // Check if model:applyDrawMode is explicitly set.
    UsdGeomModelAPI geomModelAPI(prim);
    if (geomModelAPI) {
        UsdAttribute applyAttr = geomModelAPI.GetModelApplyDrawModeAttr();
        if (applyAttr.HasAuthoredValue()) {
            bool applyDrawMode = false;
            applyAttr.Get(&applyDrawMode);
            return applyDrawMode;
        }
    }

    // If a prim is kind = "component", it gets an implicit fallback of
    // "model:applyDrawMode = 1", even if the API is not applied.
    // Otherwise, the fallback is "0", as defined in the schema.
    if (UsdModelAPI(prim).IsKind(KindTokens->component)) {
        return true;
    }

    return false;
}


TfToken
UsdImagingDelegate::_GetModelDrawMode(UsdPrim const& prim)
{
    HD_TRACE_FUNCTION();

    // Optionally draw unloaded prims as bounds.
    if (_displayUnloadedPrimsWithBounds && !prim.IsLoaded()) {
        return UsdGeomTokens->bounds;
    }

    if (_IsEnabledDrawModeCache())
        return _drawModeCache.GetValue(prim);
    else
        return UsdImaging_DrawModeStrategy::ComputeDrawMode(prim);
}


UsdImagingPrimAdapterSharedPtr const& 
UsdImagingDelegate::_AdapterLookup(UsdPrim const& prim, bool ignoreInstancing)
{
    // Future Work:
    //  * Only enable plugins on demand.
    //
    //  * Implement a more robust prim typename mapping. This could be a
    //    secondary map from TfType->token to avoid TfType locks in background
    //    threads.

    TfToken adapterKey;

    if (!ignoreInstancing && prim.IsInstance()) {
        adapterKey = UsdImagingAdapterKeyTokens->instanceAdapterKey;
    } else if (_displayUnloadedPrimsWithBounds && !prim.IsLoaded()) {
        adapterKey = UsdImagingAdapterKeyTokens->drawModeAdapterKey;
    } else if (_hasDrawModeAdapter && _enableUsdDrawModes &&
               _IsDrawModeApplied(prim)) {
        adapterKey = UsdImagingAdapterKeyTokens->drawModeAdapterKey;
    } else {
        adapterKey = prim.GetPrimTypeInfo().GetSchemaTypeName();
    }

    const UsdImagingPrimAdapterSharedPtr &adapter = _AdapterLookup(adapterKey);

    // If we couldn't find an adapter and the prim has an applied UsdLuxLightAPI, 
    // look up the adapter for LightAPI and treat the prim as a light.
    // XXX: Note that this only works for prims that aren't renderable through
    // another adapter. So something like a UsdGeomMesh with a LightAPI applied
    // will not return a light adapter. Support for this is expected to be added
    // in the future.
    if (!adapter && prim.HasAPI<UsdLuxLightAPI>()) {
        return _AdapterLookup(_tokens->LightAPI);
    }
    return adapter;
}

UsdImagingPrimAdapterSharedPtr const& 
UsdImagingDelegate::_AdapterLookup(TfToken const& adapterKey)
{
    _AdapterMap::const_iterator it = _adapterMap.find(adapterKey);
    if (it != _adapterMap.end())
        return it->second;

    UsdImagingAdapterRegistry& reg = UsdImagingAdapterRegistry::GetInstance();
    UsdImagingPrimAdapterSharedPtr adapter(reg.ConstructAdapter(adapterKey));

    // For prims that have no PrimAdapter, adapter will be NULL.
    // If the adapter type isn't supported by the render index,
    // we force the adapter to be null.
    if (adapter) {
        UsdImagingIndexProxy indexProxy(this, nullptr);
        if (adapter->IsSupported(&indexProxy)) {
            adapter->SetDelegate(this);
        } else {
            TF_WARN("Selected hydra renderer doesn't support prim type '%s'",
                    adapterKey.GetText());
            adapter.reset();
        }
    }

    // NULL adapters are also cached, to avoid redundant lookups.
    return _adapterMap.insert(
        std::make_pair(adapterKey, adapter)).first->second;
}

UsdImagingDelegate::_HdPrimInfo *
UsdImagingDelegate::_GetHdPrimInfo(const SdfPath &cachePath)
{
    _HdPrimInfoMap::iterator it = _hdPrimInfoMap.find(cachePath);
    if (it == _hdPrimInfoMap.end()) {
        return nullptr;
    }
    return &(it->second);
}

Usd_PrimFlagsConjunction 
UsdImagingDelegate::_GetDisplayPredicate() const
{
    return _displayUnloadedPrimsWithBounds ?
            UsdPrimIsActive && UsdPrimIsDefined && !UsdPrimIsAbstract :
            UsdPrimDefaultPredicate;
}

Usd_PrimFlagsConjunction
UsdImagingDelegate::_GetDisplayPredicateForPrototypes() const
{
    return _displayUnloadedPrimsWithBounds ?
        UsdPrimIsActive && UsdPrimHasDefiningSpecifier && !UsdPrimIsAbstract :
        UsdPrimIsActive && UsdPrimHasDefiningSpecifier && !UsdPrimIsAbstract
            && UsdPrimIsLoaded;
}

// -------------------------------------------------------------------------- //
// Parallel Dispatch
// -------------------------------------------------------------------------- //

class UsdImagingDelegate::_Worker {
private:
    SdfPathVector _tasks;
    UsdImagingDelegate *_delegate;

public:
    _Worker(UsdImagingDelegate *delegate)
        : _delegate(delegate)
    {
    }

    void AddTask(SdfPath const& cachePath) {
        _tasks.push_back(cachePath);
    }

    template <class ForwardIter>
    typename std::enable_if<!std::is_integral<ForwardIter>::value>::type
    AddTasks(ForwardIter begin, ForwardIter end) {
        _tasks.insert(_tasks.end(), begin, end);
    }

    size_t GetTaskCount() {
        return _tasks.size();
    }

    // Disables value cache mutations for all imaging delegates that have
    // added tasks to this worker.
    void DisablePrimvarDescCacheMutations() {
        _delegate->_primvarDescCache.DisableMutation();
    }

    // Enables value cache mutations for all imaging delegates that have
    // added tasks to this worker.
    void EnablePrimvarDescCacheMutations() {
        _delegate->_primvarDescCache.EnableMutation();
    }

    // Invalidate the time varying prim cache.
    void InvalidateTimeVaryingPrimCache() {
        _delegate->_timeVaryingPrimCacheValid = false;
    }

    // Populates prim variability and initial state.
    // Used as a parallel callback method for use with WorkParallelForN.
    void UpdateVariability(size_t start, size_t end) {
        for (size_t i = start; i < end; i++) {
            UsdImagingIndexProxy indexProxy(_delegate, nullptr);
            SdfPath const& cachePath = _tasks[i];

            _HdPrimInfo *primInfo = _delegate->_GetHdPrimInfo(cachePath);
            if (TF_VERIFY(primInfo, "%s\n", cachePath.GetText())) {
                UsdImagingPrimAdapterSharedPtr const& adapter=primInfo->adapter;
                if (TF_VERIFY(adapter, "%s\n", cachePath.GetText())) {
                    adapter->TrackVariability(primInfo->usdPrim,
                                              cachePath,
                                              &primInfo->timeVaryingBits);
                }
            }
        }
    }

    // Updates prim data on time change.
    // Used as a parallel callback method for use with WorkParallelForN.
    void UpdateForTime(size_t start, size_t end) {
        for (size_t i = start; i < end; i++) {
            UsdTimeCode const& time = _delegate->_time;
            SdfPath const& cachePath = _tasks[i];

            _HdPrimInfo *primInfo = _delegate->_GetHdPrimInfo(cachePath);
            if (!primInfo) {
                // Note: if something was invalidated and then deleted,
                // it can still be in the _dirtyCachePaths list. This isn't
                // an error, but there's no work to do.
                continue;
            }
            UsdImagingPrimAdapterSharedPtr const& adapter=primInfo->adapter;
            if (TF_VERIFY(adapter, "%s\n", cachePath.GetText())) {
                adapter->UpdateForTime(primInfo->usdPrim,
                                       cachePath,
                                       time,
                                       primInfo->dirtyBits);

                // Prim is now clean
                primInfo->dirtyBits = 0;
            }
        }
    }
};

void 
UsdImagingDelegate::_AddTask(
    UsdImagingDelegate::_Worker *worker, SdfPath const& cachePath)
{
    worker->AddTask(cachePath);
}

// -------------------------------------------------------------------------- //
// Population & Update
// -------------------------------------------------------------------------- //

void
UsdImagingDelegate::SyncAll(bool includeUnvarying)
{
    UsdImagingDelegate::_Worker worker(this);

    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo    = it->second;

        if (includeUnvarying) {
            primInfo.dirtyBits |= HdChangeTracker::AllDirty;
        } else if (primInfo.dirtyBits == HdChangeTracker::Clean) {
            continue;
        }

        // In this case, the path is coming from our internal state, so it is
        // not prefixed with the delegate ID.
        UsdImagingPrimAdapterSharedPtr adapter = primInfo.adapter;

        if (TF_VERIFY(adapter, "%s\n", cachePath.GetText())) {
            TF_DEBUG(USDIMAGING_UPDATES).Msg(
                      "[Sync] PREP: <%s> dirtyFlags: 0x%x [%s]\n",
                      cachePath.GetText(), 
                      primInfo.dirtyBits,
                      HdChangeTracker::StringifyDirtyBits(
                                                   primInfo.dirtyBits).c_str());

            worker.AddTask(cachePath);
        }
    }

    _ExecuteWorkForTimeUpdate(&worker);
}

void
UsdImagingDelegate::Sync(HdSyncRequestVector* request)
{
    TRACE_FUNCTION();

    if (_dirtyCachePaths.empty()) {
        return;
    }

    UsdImagingDelegate::_Worker worker(this);

    if (TfDebug::IsEnabled(USDIMAGING_UPDATES)) {
        for (SdfPath const& cachePath : _dirtyCachePaths) {
            _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
            if (!primInfo) {
                // Note: if something was invalidated and then deleted,
                // it can still be in the _dirtyCachePaths list. This isn't
                // an error, but there's no work to do.
                continue;
            }
            TF_DEBUG(USDIMAGING_UPDATES).Msg(
                    "[Sync] PREP: <%s> dirtyFlags: 0x%x [%s]\n",
                    cachePath.GetText(), 
                    primInfo->dirtyBits,
                    HdChangeTracker::StringifyDirtyBits(primInfo->dirtyBits).c_str());
        }
    }

    worker.AddTasks(_dirtyCachePaths.begin(), _dirtyCachePaths.end());
    _dirtyCachePaths.clear();

    _ExecuteWorkForTimeUpdate(&worker);
}

void
UsdImagingDelegate::Populate(UsdPrim const& rootPrim)
{
    SdfPathVector empty;
    Populate(rootPrim, empty);
}

void
UsdImagingDelegate::Populate(UsdPrim const& rootPrim, 
                             SdfPathVector const& excludedPrimPaths,
                             SdfPathVector const &invisedPrimPaths)
{
    HD_TRACE_FUNCTION();

    if (!_CanPopulate(rootPrim))
        return;

    _SetStateForPopulation(rootPrim, excludedPrimPaths, invisedPrimPaths);

    UsdImagingDelegate::_Worker worker(this);
    UsdImagingIndexProxy indexProxy(this, &worker);

    indexProxy.Repopulate(rootPrim.GetPath());

    _Populate(&indexProxy);
    _ExecuteWorkForVariabilityUpdate(&worker);
}

bool 
UsdImagingDelegate::_CanPopulate(UsdPrim const& rootPrim) const
{
    // Currently, Populate is only allowed to be called once, but we could relax
    // this restriction if there is a need to do so.
    //
    // If we change this, we must also revoke the objectsChangedNoticeKey.
    if (!TF_VERIFY(!_stage, "Attempted to call Populate more than once"))
        return false;

    if (!rootPrim) {
        TF_CODING_ERROR("Expired rootPrim \n");
        return false;
    }

    return true;
}

void 
UsdImagingDelegate::_SetStateForPopulation(
    UsdPrim const& rootPrim,
    SdfPathVector const& excludedPrimPaths,
    SdfPathVector const& invisedPrimPaths)
{
    if (_stage)
        return;

    // Hold onto the stage from which we will be drawing. The delegate will keep
    // the stage alive, holding it by strong reference.
    _stage = rootPrim.GetStage();
    _rootPrimPath = rootPrim.GetPath();
    _excludedPrimPaths = excludedPrimPaths;
    _invisedPrimPaths = invisedPrimPaths;

    // Set the root path of the inherited transform cache.
    // XXX: Ideally, we'd like to deprecate the inherited cache's SetRootPath(),
    // but the root prim is defined as having identity transform over all time,
    // even when its transform within the full stage is animated; and transform
    // overrides are defined as relative to the root prim.  This means resolving
    // transforms without involving the inherited cache is impossible.
    //
    // If the transform override mechanism is deprecated in favor of a USD
    // session layer, we could do something nicer here.
    _xformCache.SetRootPath(_rootPrimPath);

    // Start listening for change notices from this stage.
    UsdImagingDelegatePtr self = TfCreateWeakPtr(this);
    _objectsChangedNoticeKey = 
        TfNotice::Register(self, &This::_OnUsdObjectsChanged, _stage);
}

namespace {
    struct _PopulateMaterialBindingCache {
        UsdPrim primToBind;
        UsdImaging_MaterialBindingCache const* materialBindingCache;
        void operator()() const {
            // Just calling GetValue will populate the cache for this prim and
            // potentially all ancestors.
            materialBindingCache->GetValue(primToBind);
        }
    };
};

namespace
{

class _NoticeBatchingContext
{
public:
    _NoticeBatchingContext(HdRenderIndex *renderIndex)
    : _renderIndex(renderIndex)
    {
        _renderIndex->SceneIndexEmulationNoticeBatchBegin();
    }

    ~_NoticeBatchingContext()
    {
        _renderIndex->SceneIndexEmulationNoticeBatchEnd();
    }

private:
    HdRenderIndex * const _renderIndex;
};

} // namespace anonymous

void
UsdImagingDelegate::_Populate(UsdImagingIndexProxy* proxy)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPathVector const& usdPathsToRepopulate =
        proxy->_GetUsdPathsToRepopulate();
    if (usdPathsToRepopulate.empty()) {
        return;
    }

    // Force initialization of SchemaRegistry (doing this in parallel causes all
    // threads to block).
    UsdSchemaRegistry::GetInstance();

    // Build a TfHashSet of excluded prims for fast rejection.
    TfHashSet<SdfPath, SdfPath::Hash> excludedSet;
    TF_FOR_ALL(pathIt, _excludedPrimPaths) {
        excludedSet.insert(*pathIt);
    }

    TF_DEBUG(USDIMAGING_CHANGES)
        .Msg("[Repopulate] Populating <%s> on stage %s\n",
             _rootPrimPath.GetString().c_str(),
             _stage->GetRootLayer()->GetDisplayName().c_str());

    // Batch all population-driven scene index notices (PrimsAdded) until this
    // goes out of scope so that downstream consumers which may want
    // immediate information about prims need not be concerned with the order
    // in which prims are populated.
    _NoticeBatchingContext batchingContext(&GetRenderIndex());

    WorkDispatcher bindingDispatcher;

    // For each root that has been scheduled for repopulation
    std::vector<std::pair<UsdPrim, UsdImagingPrimAdapterSharedPtr> > leafPaths;
    leafPaths.reserve(usdPathsToRepopulate.size());

    std::vector<UsdPrim> primsToPopulateWithoutInstancing;

    for (SdfPath const& usdPath: usdPathsToRepopulate) {

        // _Populate should never be called on prototype prims or prims in
        // prototypes.
        UsdPrim prim = _GetUsdPrim(usdPath);
        if (prim && (prim.IsPrototype() || prim.IsInPrototype())) {
            continue;
        }

        // Discover and insert all renderable prims into the worker for later
        // execution.
        TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Root path: <%s>\n",
                            usdPath.GetText());

        const UsdPrimRange range(prim, _GetDisplayPredicate());

        for (auto iter = range.begin(); iter != range.end(); ++iter) {
            if (!iter->GetPath().HasPrefix(_rootPrimPath)) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "not under root prim path <%s>\n",
                            iter->GetPath().GetText(),
                            _rootPrimPath.GetText());
                continue;
            }
            if (excludedSet.find(iter->GetPath()) != excludedSet.end()) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "due to exclusion list\n",
                            iter->GetPath().GetText());
                continue;
            }
            if (UsdImagingPrimAdapter::ShouldCullSubtree(*iter)) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "due to prim type <%s>\n",
                            iter->GetPath().GetText(),
                            iter->GetTypeName().GetText());
                continue;
            }

            if (iter->IsInstance()) {
                // Check if the adapter of the instanced prim wants to
                // ignore instancing.
                if (UsdImagingPrimAdapterSharedPtr adapter =
                    _AdapterLookup(*iter, /*ignoreInstancing*/ true)) {

                    if (adapter->ShouldIgnoreNativeInstanceSubtrees()) {
                        TF_DEBUG(USDIMAGING_CHANGES).Msg(
                            "[Repopulate] Ignoring instancing of subtree "
                            "at <%s>\n", iter->GetPath().GetText());

                        primsToPopulateWithoutInstancing.push_back(*iter);
                        iter.PruneChildren();
                        continue;
                    }
                }
            }

            if (UsdImagingPrimAdapterSharedPtr adapter = _AdapterLookup(*iter)) {

                if (adapter->ShouldIgnoreNativeInstanceSubtrees()) {
                    TF_DEBUG(USDIMAGING_CHANGES).Msg(
                        "[Repopulate] Ignoring instancing of subtree "
                        "at <%s>\n", iter->GetPath().GetText());

                    primsToPopulateWithoutInstancing.push_back(*iter);
                    iter.PruneChildren();
                    continue;
                }

                // Schedule the prim for population and discovery
                // of material bindings.
                //
                // If we are using full networks, we will populate the 
                // binding cache that has the strategy to compute the correct
                // bindings.
                _PopulateMaterialBindingCache wu = 
                    { *iter, &_materialBindingCache};
                 bindingDispatcher.Run(wu);
                
                leafPaths.push_back(std::make_pair(*iter, adapter));
                if (adapter->ShouldCullChildren()) {
                   TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned "
                                    "children of <%s> due to adapter\n",
                            iter->GetPath().GetText());
                   iter.PruneChildren();
                }
            }
        }
    }

    // Populate subtrees for adapters that ignore instancing.
    for (const UsdPrim& prim : primsToPopulateWithoutInstancing) {
        TF_DEBUG(USDIMAGING_CHANGES).Msg(
            "[Repopulate] Root path (no instancing): <%s>\n",
            prim.GetPath().GetText());

        const UsdPrimRange range(
            prim, UsdTraverseInstanceProxies(_GetDisplayPredicate()));

        for (auto iter = range.begin(); iter != range.end(); ++iter) {
            if (!iter->GetPath().HasPrefix(_rootPrimPath)) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "not under root prim path <%s>\n",
                            iter->GetPath().GetText(),
                            _rootPrimPath.GetText());
                continue;
            }
            if (excludedSet.find(iter->GetPath()) != excludedSet.end()) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "due to exclusion list\n",
                            iter->GetPath().GetText());
                continue;
            }
            if (UsdImagingPrimAdapter::ShouldCullSubtree(*iter)) {
                iter.PruneChildren();
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned at <%s> "
                            "due to prim type <%s>\n",
                            iter->GetPath().GetText(),
                            iter->GetTypeName().GetText());
                continue;
            }

            if (UsdImagingPrimAdapterSharedPtr adapter =
                _AdapterLookup(*iter, /*ignoreInstancing*/ true)) {
                // Schedule the prim for population and discovery
                // of material bindings.
                //
                // If we are using full networks, we will populate the 
                // binding cache that has the strategy to compute the correct
                // bindings.
                _PopulateMaterialBindingCache wu = 
                    { *iter, &_materialBindingCache};
                 bindingDispatcher.Run(wu);
                
                leafPaths.push_back(std::make_pair(*iter, adapter));
                if (adapter->ShouldCullChildren()) {
                   TF_DEBUG(USDIMAGING_CHANGES).Msg("[Repopulate] Pruned "
                                    "children of <%s> due to adapter\n",
                            iter->GetPath().GetText());
                   iter.PruneChildren();
                }
            }
        }
    }

    // Populate the RenderIndex while we're still discovering material bindings.
    TF_FOR_ALL(it, leafPaths) {
        it->second->Populate(it->first, proxy);
    }

    // In the event that Population finishes before look binding cache 
    // population, we must block here to ensure it isn't running in the
    // background, since we might mutate the look binding cache later.
    bindingDispatcher.Wait();
}

void 
UsdImagingDelegate::_ExecuteWorkForVariabilityUpdate(_Worker* worker)
{
    HD_TRACE_FUNCTION();

    TF_DEBUG(USDIMAGING_CHANGES)
        .Msg("[Repopulate] %zu variability tasks in worker\n", 
             worker->GetTaskCount());

    if (worker->GetTaskCount() > 0) {
        worker->InvalidateTimeVaryingPrimCache();
    }

    worker->DisablePrimvarDescCacheMutations();
    {
        // Release the GIL to ensure that threaded work won't deadlock if
        // they also need the GIL.
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        WorkParallelForN(
            worker->GetTaskCount(), 
            std::bind(&UsdImagingDelegate::_Worker::UpdateVariability, 
                      worker, std::placeholders::_1, std::placeholders::_2));
    }
    worker->EnablePrimvarDescCacheMutations();
}

void
UsdImagingDelegate::_ExecuteWorkForTimeUpdate(_Worker* worker)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    worker->DisablePrimvarDescCacheMutations();
    {
        // Release the GIL to ensure that threaded work won't deadlock if
        // they also need the GIL.
        TF_PY_ALLOW_THREADS_IN_SCOPE();
        WorkParallelForN(
            worker->GetTaskCount(), 
            std::bind(&UsdImagingDelegate::_Worker::UpdateForTime, 
                      worker, std::placeholders::_1, std::placeholders::_2));
    }
    worker->EnablePrimvarDescCacheMutations();
}

void
UsdImagingDelegate::SetTime(UsdTimeCode time)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // XXX: Many clients rely on SetTime(currentTime) to apply pending
    // scene edits. If we fix them to call ApplyPendingUpdates(), we can
    // remove this.
    ApplyPendingUpdates();

    // Early out if the time code is the same.
    if (_time == time) {
        return;
    }

    TF_DEBUG(USDIMAGING_UPDATES).Msg("[Update] Update for time (%f)\n",
        time.GetValue());

    _time = time;
    _xformCache.SetTime(_time);
    _visCache.SetTime(_time);
    _pointInstancerIndicesCache.SetTime(_time);
    _nonlinearSampleCountCache.SetTime(_time);
    _blurScaleCache.SetTime(_time);

    // No need to set time on the look binding cache here, since we know we're
    // only querying relationships.

    UsdImagingIndexProxy indexProxy(this, nullptr);

    // Mark varying attributes as dirty.
    if (_timeVaryingPrimCacheValid) {
        for (SdfPath const& path : _timeVaryingPrimCache) {
            _HdPrimInfoMap::iterator it = _hdPrimInfoMap.find(path);
            if (it == _hdPrimInfoMap.end()) {
                continue;
            }
            it->second.adapter->MarkDirty(it->second.usdPrim,
                                        path,
                                        it->second.timeVaryingBits,
                                        &indexProxy);
        }
    } else {
        _timeVaryingPrimCache.clear();
        for (auto const& pair : _hdPrimInfoMap) {
            const SdfPath &cachePath    = pair.first;
            const _HdPrimInfo &primInfo = pair.second;

            if (primInfo.timeVaryingBits != HdChangeTracker::Clean) {
                _timeVaryingPrimCache.push_back(cachePath);
                primInfo.adapter->MarkDirty(primInfo.usdPrim,
                                            cachePath,
                                            primInfo.timeVaryingBits,
                                            &indexProxy);
            }
        }
        _timeVaryingPrimCacheValid = true;
    }
}

void 
UsdImagingDelegate::SetTimes(const std::vector<UsdImagingDelegate*>& delegates,
                             const std::vector<UsdTimeCode>& times)
{
    if (delegates.size() != times.size()) {
        TF_CODING_ERROR("Mismatched parameters");
        return;
    }

    if (delegates.empty()) {
        return;
    }

    // Collect work from the batch of delegates into a single worker.
    // This has to be done single-threaded due to potential mutations
    // to the render index that is shared among these delegates.
    for (size_t i = 0; i < delegates.size(); ++i) {
        delegates[i]->SetTime(times[i]);
    }
}

UsdTimeCode
UsdImagingDelegate::GetTimeWithOffset(float offset) const
{
    return _time.IsNumeric() ? UsdTimeCode(_time.GetValue() + offset) : _time;
}

// -------------------------------------------------------------------------- //
// Change Processing 
// -------------------------------------------------------------------------- //

void
UsdImagingDelegate::_GatherDependencies(SdfPath const& subtree,
                                        SdfPathVector *affectedCachePaths)
{
    if (affectedCachePaths == nullptr) {
        return;
    }

    HD_TRACE_FUNCTION();

    // Binary search for the first path in the subtree.
    _DependencyMap::const_iterator start =
        _dependencyInfo.lower_bound(subtree);

    // If we couldn't find any paths in the subtree, early out.
    if (start == _dependencyInfo.end() || !start->first.HasPrefix(subtree)) {
        return;
    }

    // std::upper_bound makes O(N) iterator increments and O(log N) HasPrefix
    // calls, because _DependencyMap::iterator is not a
    // LegacyRandomAccessIterator. Test to see if there are any children at all
    // before making this expensive call.
    _DependencyMap::const_iterator end = start;
    end++;
    if (end != _dependencyInfo.cend() && end->first.HasPrefix(subtree)) {
        // Binary search for the first path not in the subtree, starting at
        // "end".
        end = std::upper_bound(
            end, _dependencyInfo.cend(), subtree,
            [](SdfPath const& lhs, _DependencyMap::value_type const& rhs) {
                return !rhs.first.HasPrefix(lhs);
            });
    }

    SdfPathVector affectedPaths;
    for (_DependencyMap::const_iterator it = start; it != end; ++it) {
        affectedPaths.push_back(it->second);
    }
    // De-duplicate cache paths, in case a cache path has multiple
    // usd dependencies within subtree.
    std::sort(affectedPaths.begin(), affectedPaths.end());
    std::unique_copy(affectedPaths.begin(), affectedPaths.end(),
                     std::back_inserter(*affectedCachePaths));
}

void
UsdImagingDelegate::_GatherDependencies(
    SdfPath const& subtree,
    _FlattenedDependenciesCacheMap const &cache,
    SdfPathVector *affectedCachePaths)
{
    if (affectedCachePaths == nullptr) {
        return;
    }

    const auto it = cache.find(subtree);
    if (it != cache.end()) {
        TF_DEBUG(USDIMAGING_CHANGES).Msg(
            "[_GatherDependencies] Found entry in flattened cache for %s with "
            "%lu paths\n", subtree.GetText(), it->second.size());

        *affectedCachePaths = it->second;
        return;
    }

    TF_DEBUG(USDIMAGING_CHANGES).Msg(
        "[_GatherDependencies] No entry in flattened cache for %s\n ",
        subtree.GetText());

    _GatherDependencies(subtree, affectedCachePaths);
}

void
UsdImagingDelegate::ApplyPendingUpdates()
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // Early out if there are no updates.
    if (_usdPathsToResync.empty() && _usdPathsToUpdate.empty()) {
        return;
    }

    TF_DEBUG(USDIMAGING_UPDATES).Msg("[Update] Update for scene edits\n");

    // Need to invalidate all caches if any stage objects have changed. This
    // invalidation is overly conservative, but correct.
    _xformCache.Clear();
    _materialBindingImplData.ClearCaches();
    _materialBindingCache.Clear();
    _visCache.Clear();
    _purposeCache.Clear();
    _drawModeCache.Clear();
    _coordSysBindingCache.Clear();
    _inheritedPrimvarCache.Clear();
    _pointInstancerIndicesCache.Clear();
    _nonlinearSampleCountCache.Clear();
    _blurScaleCache.Clear();

    UsdImagingDelegate::_Worker worker(this);
    UsdImagingIndexProxy indexProxy(this, &worker);
    SdfPathSet allTrackedVariabilityPaths;

    if (!_usdPathsToResync.empty()) {
        // Make a copy of _usdPathsToResync but uniqued with a
        // prefix-check, which removes all elements that are prefixed by
        // other elements.
        SdfPathVector usdPathsToResync;
        usdPathsToResync.reserve(_usdPathsToResync.size());
        std::sort(_usdPathsToResync.begin(), _usdPathsToResync.end());
        std::unique_copy(_usdPathsToResync.begin(), _usdPathsToResync.end(),
                         std::back_inserter(usdPathsToResync),
                         [](SdfPath const &l, SdfPath const &r) {
                             return r.HasPrefix(l);
                         });
        _usdPathsToResync.clear();

        // Pre-cache USD subtree to Hydra prim cache paths in parallel.
        // This avoids redundant serial calls to _GatherDependencies during
        // Resync and Refresh handling below.
        // Note: We don't use the same cache for the "update" processing that
        //       follows because we process removals from the resync/refresh
        //       handling that may result in the cache being out-of-sync with
        //       the _dependencyInfo map.
        //
        _FlattenedDependenciesCacheMap resyncDependenciesCache;
        WorkDispatcher resyncPathsCacheDispatcher;
        for (const SdfPath& usdPath : usdPathsToResync) {
            TF_DEBUG(USDIMAGING_CHANGES).Msg(
                "[usdPathsToResync] Updating cache map for %s\n",
                usdPath.GetText());
            auto pair = resyncDependenciesCache.insert(
                std::make_pair(usdPath, SdfPathVector()));
            if (!pair.second) {
                // No insertion happened, path has been inserted
                continue;
            }
            auto& affectedCachePaths = pair.first->second;
            resyncPathsCacheDispatcher.Run(
                [this, &usdPath, &affectedCachePaths]() {
                    _GatherDependencies(usdPath, &affectedCachePaths);
            });
        }
        resyncPathsCacheDispatcher.Wait();

        for (SdfPath const& usdPath: usdPathsToResync) {
            if (usdPath.IsPropertyPath()) {
                _RefreshUsdObject(usdPath, TfTokenVector(),
                                  resyncDependenciesCache,
                                  &indexProxy, &allTrackedVariabilityPaths);
            } else if (usdPath.IsTargetPath()) {
                // TargetPaths are their own path type, when they change, resync
                // the relationship at which they're rooted; i.e. per-target
                // invalidation is not supported.
                _RefreshUsdObject(usdPath.GetParentPath(), TfTokenVector(),
                                  resyncDependenciesCache,
                                  &indexProxy, &allTrackedVariabilityPaths);
            } else if (usdPath.IsAbsoluteRootOrPrimPath()) {
                _ResyncUsdPrim(usdPath, resyncDependenciesCache, &indexProxy);
            } else {
                TF_WARN("Unexpected path type to resync: <%s>",
                        usdPath.GetText());
            }
        }
    }

    // Remove any objects that were queued for removal to ensure RefreshObject
    // doesn't apply changes to removed objects.
    indexProxy._ProcessRemovals();

    if (!_usdPathsToUpdate.empty()) {
        _PathsToUpdateMap usdPathsToUpdate;
        std::swap(usdPathsToUpdate, _usdPathsToUpdate);

        // Pre-cache dependencies in parallel. See earlier note.
        WorkDispatcher updatePathsCacheDispatcher;
        _FlattenedDependenciesCacheMap updateDependenciesCache;

        for (const auto& pathIt: usdPathsToUpdate) {
            const auto& usdPath = pathIt.first;
            TF_DEBUG(USDIMAGING_CHANGES).Msg(
                "[usdPathsToUpdate] Updating cache map for %s\n",
                usdPath.GetText());
            auto pair = updateDependenciesCache.insert(
                std::make_pair(usdPath, SdfPathVector()));
            if (!pair.second) {
                // No insertion happened, path has been inserted
                continue;
            }
            auto& affectedCachePaths = pair.first->second;
            updatePathsCacheDispatcher.Run(
                [this, &usdPath, &affectedCachePaths]() {
                    _GatherDependencies(usdPath, &affectedCachePaths);
            });
        }
        updatePathsCacheDispatcher.Wait();

        TF_FOR_ALL(pathIt, usdPathsToUpdate) {
            const SdfPath& usdPath = pathIt->first;
            const TfTokenVector& changedPrimInfoFields = pathIt->second;

            if (usdPath.IsPropertyPath() || usdPath.IsAbsoluteRootOrPrimPath()){
                // Note that changedPrimInfoFields will be empty if the
                // path is a property path.
                const bool resyncNeeded = 
                    _RefreshUsdObject(usdPath, changedPrimInfoFields,
                        updateDependenciesCache,
                        &indexProxy, &allTrackedVariabilityPaths);

                // If any objects were removed as a result of the refresh (if it
                // internally decided to resync), they must be ejected now,
                // before the next call to _RefreshObject.
                if (resyncNeeded) {
                    indexProxy._ProcessRemovals();

                    // Clear the cache since it'd be out-of-sync with the
                    // _dependencyInfo map.
                    updateDependenciesCache.clear();
                }
            } else {
                TF_RUNTIME_ERROR("Unexpected path type to update: <%s>",
                        usdPath.GetText());
            }
        }
    }

    // If any changes called Repopulate() on the indexProxy, we need to
    // repopulate them before any updates. If the list is empty, _Populate is a
    // no-op.
    indexProxy._UniqueifyPathsToRepopulate();
    _Populate(&indexProxy);
    _ExecuteWorkForVariabilityUpdate(&worker);

    // Mark all dirty collection prims
    const SdfPathSet& pathsDirtiedByCollections = _collectionCache.GetDirtyPaths();
    if (!pathsDirtiedByCollections.empty()) {
        TRACE_SCOPE("Mark dirty collection members");
        for (const SdfPath& dirtyPath : pathsDirtiedByCollections) {
            TF_DEBUG(USDIMAGING_CHANGES).Msg("[Update]: invalidate collection member prim %s\n", dirtyPath.GetText());
            _HdPrimInfo* primInfo = _GetHdPrimInfo(dirtyPath);
            if (primInfo && primInfo->usdPrim.IsValid() &&
                TF_VERIFY(primInfo->adapter, "%s", dirtyPath.GetText())) {
                UsdImagingPrimAdapterSharedPtr& adapter = primInfo->adapter;
                adapter->MarkCollectionsDirty(primInfo->usdPrim, dirtyPath, &indexProxy);
            }
        }
        _collectionCache.ClearDirtyPaths();
    }
}

using PathRange = UsdNotice::ObjectsChanged::PathRange;

// Helper function to check if a path entry has attribute connection did change
// notice
bool
_HasConnectionChanged(const SdfPath &path, const PathRange &pathRange)
{
    PathRange::const_iterator itr = pathRange.find(path);
    if (itr != pathRange.end()) {
        for (const SdfChangeList::Entry *entry : itr.GetBase()->second) {
            if (entry->flags.didChangeAttributeConnection) {
                return true;
            }
        }
    }
    return false;
}

// Helper function to check if a path entry has relationship target did change
// notice
bool
_HasRelationshipTargetsChanged(const SdfPath &path, const PathRange &pathRange)
{
    PathRange::const_iterator itr = pathRange.find(path);
    if (itr != pathRange.end()) {
        for (const SdfChangeList::Entry *entry : itr.GetBase()->second) {
            if (entry->flags.didChangeRelationshipTargets) {
                return true;
            }
        }
    }
    return false;
}

void 
UsdImagingDelegate::_OnUsdObjectsChanged(
    UsdNotice::ObjectsChanged const& notice,
    UsdStageWeakPtr const& sender)
{
    if (!sender || !TF_VERIFY(sender == _stage))
        return;
    TF_DEBUG(USDIMAGING_CHANGES).Msg("[Objects Changed] Notice recieved "
                            "from stage with root layer @%s@\n",
                        sender->GetRootLayer()->GetIdentifier().c_str());

    // If there was a connection changed inside a shade graph, this also
    // requires dumping all cached data since we need to rebuild the shader.

    // These paths are subtree-roots representing entire subtrees that may have
    // changed. In this case, we must dump all cached data below these points
    // and repopulate those trees.
    const PathRange pathsToResync = notice.GetResyncedPaths();
    for (const auto& path : pathsToResync) {
        if (path.IsPrimPropertyPath() &&
                (_HasConnectionChanged(path, pathsToResync) ||
                 _HasRelationshipTargetsChanged(path, pathsToResync))) {
            // Resync the prim path instead of the property path:
            _usdPathsToResync.emplace_back(path.GetPrimPath());
        } else {
            _usdPathsToResync.emplace_back(path);
        }
    }

    // These paths represent the root of subtrees containing asset path values
    // that may now resolve to different locations than previously, even though
    // the authored values themselves have not changed. We currently do not the
    // precise locations of asset path values we have pulled on, so we can't do
    // a sparse invalidation. Instead, we treat this as a full resync to ensure
    // anything that may depend on affected asset paths gets repopulated.
    const PathRange assetPathsToResync =
        notice.GetResolvedAssetPathsResyncedPaths();
    for (const auto& path : assetPathsToResync) {
        _usdPathsToResync.emplace_back(path);
    }

    // These paths represent objects which have been modified in a 
    // non-structural way, for example setting a value. These paths may be paths
    // to prims or properties, in which case we should sparsely invalidate
    // cached data associated with the path.
    const PathRange pathsToUpdate = notice.GetChangedInfoOnlyPaths();
    for (const auto &path : pathsToUpdate) {
        if (path.IsAbsoluteRootOrPrimPath()) {
            // Ignore all changes to prims that have not changed any field
            // values, since these changes cannot affect any composed values 
            // consumed by the adapters.
            const TfTokenVector changedFields = notice.GetChangedFields(path);
            if (!changedFields.empty()) {
                TfTokenVector &changedPrimInfoFields = _usdPathsToUpdate[path];
                changedPrimInfoFields.insert(changedPrimInfoFields.end(),
                        changedFields.begin(), changedFields.end());
            }
        } else if (path.IsPropertyPath()) {
            _usdPathsToUpdate.emplace(path, TfTokenVector());
            if (_HasConnectionChanged(path, pathsToUpdate)) {
                // Resync the prim path as well:
                _usdPathsToResync.emplace_back(path.GetPrimPath());
            }
        }
    }

    if (TfDebug::IsEnabled(USDIMAGING_CHANGES)) {
        TF_FOR_ALL(it, pathsToResync) {
            TF_DEBUG(USDIMAGING_CHANGES).Msg(" - Resync queued: %s\n",
                        it->GetText());
        }
        TF_FOR_ALL(it, pathsToUpdate) {
            // For diagonstic clarity, filter out paths we decided to ignore
            if (_usdPathsToUpdate.find(*it) != _usdPathsToUpdate.end()) {
                TF_DEBUG(USDIMAGING_CHANGES).Msg(" - Refresh queued: %s\n",
                                                 it->GetText());
            }
        }
    }
}

/// XXX: We explicitly check if a prim adapter is a coord sys adapter for some
/// of the behavior exceptions in _ResyncUsdPrim. While it may be nice to use a
/// more extensible adapter function to determine behaviors in _ResynceUsdPrim,
/// it's expected that this code will be replaced soon so specifically checking
/// for coord system adapters makes more sense in the interim.
static 
bool _IsCoordSysAdapter(const UsdImagingPrimAdapterSharedPtr &adapter)
{
    return dynamic_cast<UsdImagingCoordSysAdapter *>(adapter.get());
}

// We also need to check if a prim adapter is a material adapter to ensure we
// perform the resync correctly in _ResyncUsdPrim.
static
bool _IsMaterialAdapter(const UsdImagingPrimAdapterSharedPtr& adapter)
{
    return dynamic_cast<UsdImagingMaterialAdapter*>(adapter.get());
}

void 
UsdImagingDelegate::_ResyncUsdPrim(
    SdfPath const& usdPath,
    _FlattenedDependenciesCacheMap const &cache,
    UsdImagingIndexProxy* proxy,
    bool repopulateFromRoot) 
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    TF_DEBUG(USDIMAGING_CHANGES).Msg("[Resync Prim]: <%s>\n",
            usdPath.GetText());

    // This function is confusing, so an explainer:
    //
    // In general, the USD prims that get mapped to hydra prims are leaf prims.
    // In a typical scene, you'll have (for example) a tree full of xforms,
    // grouped together semantically, with meshes as the leaves.  If you have
    // materials, they too will be leaves.  So if you populate a hydra prim,
    // you can be assured there are no hydra prims down the subtree, and
    // likewise there will be no hydra prims among your ancestors.
    //
    // There's a class of USD prims that don't get hydra prims, but modify
    // their parents: for example, UsdGeomSubset modifies UsdGeomMesh, and
    // UsdShadeShader modifies UsdShadeMaterial.  There's also an exception to
    // the rule in the form of Point Instancer prototypes: since they are
    // populated by reference, they can be populated below a point instancer
    // even though the point instancer is supposed to be a leaf node in hydra.
    //
    // There's an additional exception with regards to the targets of coord 
    // system bindings. Coord systems target and are dependent on Xform prims 
    // that can be anywhere within the hierarchy of the tree. The hydra prims 
    // for coordinate systems that are dependent on the resynced path or any
    // of its descendants are always individually resynced.
    //
    // The resync function has three phases, with each phase dropping through
    // to the next:
    //
    // (1a) If the resync path points to a hydra prim, forward the Resync call.
    // (1b) If an ancestor of the resync path points to a hydra prim that is not
    //      a coordinate system, the resync path must be one of the cases 
    //      mentioned above: subset, shader, point instancer prototype/etc.  
    //      In all of these cases, the appropriate thing is to resync the 
    //      ancestor.
    // 
    //  -- If case (1) applies, proceed to (2a), otherwise (2b) --
    //
    // (2a) If the resync target is a child of a "leaf" hydra prim, check 
    //      if it's a parent of any coordinate system or material prims. If so,
    //      we need to resync the child prims as they're independent from their
    //      parents. From here, we're done so return.
    // (2b) Otherwise, since the resync target isn't a child of a "leaf" hydra 
    //      prim, check if it's a parent of any hydra prims.  If so, we need to 
    //      remove the old prims and repopulate them and any new prims.  We do 
    //      this by finding all existing hydra prims below "usdPath", and 
    //      calling ProcessPrimResync().  This will either re-add them or remove 
    //      them, based on whether the USD prim still exists.  Also: traverse
    //      "usdPath" looking for imageable prims that *have not* been
    //      populated; add them.
    //
    // Certain hierarchy-affecting operations like model:drawMode changes
    // require we re-populate from the top of the subtree whose "drawMode"
    // attribute changed; if repopulateFromRoot is true, we additionally
    // add "usdPath" to repopulation.  _UniqueifyPathsToRepopulate will remove
    // the individual paths from that subtree that were added by
    // ProcessPrimResync.
    //
    //  -- If case (1) and (2) don't apply, proceed --
    //
    //  (3) The resync path has no hydra prims populated above or below it,
    //  meaning it's a totally new subtree.  Populate it from the root.

    // If the resync target is below a currently populated prim that is not a
    // coordinate system, forward the resync notice to that prim.  
    // In general, prims can't be populated below other prims, and in the 
    // exceptional cases (instancer prototypes, geom subsets, etc) we handle 
    // things in the parent prim adapter.
    bool foundPruningPrim = false;
    for (SdfPath currentPath = usdPath;
         !foundPruningPrim && currentPath != SdfPath::AbsoluteRootPath();
         currentPath = currentPath.GetParentPath()) {
        auto const& range = _dependencyInfo.equal_range(currentPath);
        for (auto it = range.first; it != range.second; ++it) {
            SdfPath const& cachePath = it->second;
            if (currentPath == usdPath) {
                TF_DEBUG(USDIMAGING_CHANGES)
                    .Msg("  - affected prim: <%s>\n",
                            cachePath.GetText());
            } else {
                TF_DEBUG(USDIMAGING_CHANGES)
                    .Msg("  - affected ancestor prim: <%s>\n",
                            cachePath.GetText());
            }
            _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
            if (primInfo != nullptr &&
                TF_VERIFY(primInfo->adapter != nullptr)) {
                // Skip any coord system prims as they're synced independently
                // from prims in their hierarchy.
                if (!_IsCoordSysAdapter(primInfo->adapter)) {
                    // Process the prim and mark that we found a prim that 
                    // "prunes" the need to process any non coordinate system
                    // or material prims below it.
                    primInfo->adapter->ProcessPrimResync(cachePath, proxy);
                    foundPruningPrim = true;
                }
            }
        }
    }

    // Whether the resync target is below a populated prim or not, we still 
    // search the resync subtree for affected prims. In the case where the 
    // target is below a populated prim, we're only looking for affected 
    // dependent prims that are related to coordinate systems or materials.
    // Coordinate systems are independent of their parent prims so we resync
    // each coordinate system in the subtree individually.  Referenced Material
    // prims under a populated prim, e.g., a point instancer, also need to be
    // resynced.
    // 
    // In the case where the resync target is not below a populated prim, we 
    // search the resync subtree for all affected prims. If there are any 
    // affected dependent prims, this subtree has been populated and we can 
    // resync affected prims individually. If we do this, we also need to walk 
    // the subtree and check for new prims.
    SdfPathVector affectedCachePaths;
    _GatherDependencies(usdPath, cache, &affectedCachePaths);
    if (affectedCachePaths.size() > 0) {
        for (SdfPath const& affectedCachePath : affectedCachePaths) {

            TF_DEBUG(USDIMAGING_CHANGES).Msg("  - affected child prim: <%s>\n",
                    affectedCachePath.GetText());
            _HdPrimInfo *primInfo = _GetHdPrimInfo(affectedCachePath);
            if (primInfo != nullptr &&
                TF_VERIFY(primInfo->adapter != nullptr)) {

                // Note: ProcessPrimResync will remove the prim from the 
                // index, similar to ProcessPrimRemoval, but then additionally
                // call proxy->Repopulate() on itself. In the case of
                // "repopulateFromRoot", this is redundant with us repopulating
                // the whole subtree below, but change processing will
                // remove the redundancy.  It's important to call
                // ProcessPrimResync to add Repopulate calls for objects not
                // under "usdPath" (such as sibling native instances).
                if (!foundPruningPrim || 
                        _IsCoordSysAdapter(primInfo->adapter) ||
                        _IsMaterialAdapter(primInfo->adapter)) {
                    primInfo->adapter->ProcessPrimResync(
                        affectedCachePath, proxy);
                }
            }
        }
        if (foundPruningPrim) {
            // Because we resynced a prim that handles its descendants we don't
            // need to walk the subtree for new prims.
            return;
        }
        if (repopulateFromRoot) {
            TF_DEBUG(USDIMAGING_CHANGES).Msg("  (repopulating from root)\n");
            proxy->Repopulate(usdPath);
        } else {
            // Build a TfHashSet of excluded prims for fast rejection.
            TfHashSet<SdfPath, SdfPath::Hash> excludedSet;
            TF_FOR_ALL(pathIt, _excludedPrimPaths) {
                excludedSet.insert(*pathIt);
            }
            // If we resynced prims individually, walk the subtree for new prims
            UsdPrimRange range(_stage->GetPrimAtPath(usdPath),
                _GetDisplayPredicate());
            for (auto iter = range.begin(); iter != range.end(); ++iter) {

                auto const& depRange =
                    _dependencyInfo.equal_range(iter->GetPath());
                // If we've populated this subtree already, skip it.
                if (depRange.first != depRange.second) {
                    iter.PruneChildren();
                    continue;
                }
                if (excludedSet.find(iter->GetPath()) != excludedSet.end()) {
                    iter.PruneChildren();
                    TF_DEBUG(USDIMAGING_CHANGES).Msg("[Resync Prim] Pruned at <%s> "
                                "due to exclusion list\n",
                                iter->GetPath().GetText());
                    continue;
                }
                // Check if this prim (& subtree) should be pruned based on
                // prim type.
                if (UsdImagingPrimAdapter::ShouldCullSubtree(*iter)) {
                    iter.PruneChildren();
                    TF_DEBUG(USDIMAGING_CHANGES).Msg("[Resync Prim]: "
                        "[Re]population of subtree <%s> pruned by prim type "
                        "(%s)\n",
                        iter->GetPath().GetText(),
                        iter->GetTypeName().GetText());
                    continue;
                }
                // If this prim has an adapter, hand this subtree over to
                // delegate population.
                UsdImagingPrimAdapterSharedPtr adapter = _AdapterLookup(*iter);
                if (adapter != nullptr) {
                    TF_DEBUG(USDIMAGING_CHANGES).Msg(
                        "[Resync Prim]: Populating <%s>\n",
                        iter->GetPath().GetText());
                    proxy->Repopulate(iter->GetPath());
                    iter.PruneChildren();
                }
            }
        }
        return;
    }

    // Otherwise, this is a totally new branch of the scene, so populate
    // from the resync target path. Note: we need to verify that usdPath exists,
    // since we can get resync notices for prims that were deleted.
    if (_stage->GetPrimAtPath(usdPath)) {
        TF_DEBUG(USDIMAGING_CHANGES).Msg("  - affected new prim: <%s>\n",
            usdPath.GetText());
        proxy->Repopulate(usdPath);
    } else {
        TF_DEBUG(USDIMAGING_CHANGES).Msg("  - affected deleted prim: <%s>\n",
            usdPath.GetText());
    }
}

bool 
UsdImagingDelegate::_RefreshUsdObject(
    SdfPath const& usdPath, 
    TfTokenVector const& changedInfoFields,
    _FlattenedDependenciesCacheMap const &cache,
    UsdImagingIndexProxy* proxy,
    SdfPathSet* allTrackedVariabilityPaths) 
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    TF_DEBUG(USDIMAGING_CHANGES).Msg("[Refresh Object]: %s %s\n",
            usdPath.GetText(), TfStringify(changedInfoFields).c_str());

    bool resyncNeeded = false;
    SdfPathVector affectedCachePaths;

    if (usdPath.IsAbsoluteRootOrPrimPath()) {
        auto range = _dependencyInfo.equal_range(usdPath);
        for (auto it = range.first; it != range.second; ++it) {
            SdfPath cachePath = it->second;
            if (_GetHdPrimInfo(cachePath)) {
                affectedCachePaths.push_back(cachePath);
            }
        }
    } else if (usdPath.IsPropertyPath()) {
        SdfPath const& usdPrimPath = usdPath.GetPrimPath();
        TfToken const& attrName = usdPath.GetNameToken();
        UsdPrim usdPrim = _stage->GetPrimAtPath(usdPrimPath);

        // If either model:drawMode or model:applyDrawMode changes, we need to
        // repopulate the whole subtree starting at the owning prim.
        // If the binding has changed we need to make sure we are resyncing
        // the prim so the material gets an opportunity to populate itself.
        // This is very conservative but it is correct.
        if (attrName == UsdGeomTokens->modelDrawMode ||
            attrName == UsdGeomTokens->modelApplyDrawMode ||
            UsdShadeMaterialBindingAPI::CanContainPropertyName(attrName)) {
            _ResyncUsdPrim(usdPrimPath, cache, proxy, true);
            resyncNeeded = true;
        }

        // If we're sync'ing a non-inherited property on a parent prim, we 
        // should fall through this function without updating anything. 
        // The following if-statement should ensure this.
        
        // XXX: We must always scan for prefixed children, due to rprim fan-out 
        // from plugins (such as the PointInstancer).
        else if (attrName == UsdGeomTokens->visibility ||
            attrName == UsdGeomTokens->purpose ||
            attrName == UsdGeomTokens->motionNonlinearSampleCount ||
            UsdGeomXformable::IsTransformationAffectedByAttrNamed(attrName)) {
            // Because these are inherited attributes, we must update all
            // children.
            _GatherDependencies(usdPrimPath, cache, &affectedCachePaths);
        } else if (UsdGeomPrimvarsAPI::CanContainPropertyName(attrName)) {
            // Primvars can be inherited, so we need to invalidate everything
            // downstream.  Technically, only constant primvars on non-leaf
            // prims are inherited, but we can't check the interpolation mode
            // if (e.g.) the primvar has been blocked, and calling
            // _GatherDependencies on a leaf prim won't invoke any extra work
            // vs the equal_range below...
            _GatherDependencies(usdPrimPath, cache, &affectedCachePaths);
        } else if (UsdCollectionAPI::CanContainPropertyName(attrName)) {
            // XXX Performance: Collections used for material bindings
            // can refer to prims at arbitrary locations in the scene.
            // Accordingly, we conservatively invalidate everything.
            // If we preserved _materialBindingCache rather than
            // blowing it in _ProcessChangesForTimeUpdate(), we could
            // potentially use it to analyze affected paths and
            // perform more narrow invalidation.
            TF_DEBUG(USDIMAGING_CHANGES).Msg("[Refresh Object]: "
                "Collection property <%s> modified; conservatively "
                "invalidating all prims to ensure that we discover "
                "material binding changes.\n", usdPath.GetText());

            TF_FOR_ALL(it, _hdPrimInfoMap) {
                affectedCachePaths.push_back(it->first);
            }
        } else if (UsdShadeCoordSysAPI::CanContainPropertyName(attrName)) {
            TF_DEBUG(USDIMAGING_CHANGES).Msg("[Refresh Object]: "
                "HdCoordSys bindings affected for %s\n", usdPath.GetText());
            // Coordinate system bindings apply to all descendent gprims.
            _ResyncUsdPrim(usdPrimPath, cache, proxy, true);
            resyncNeeded = true;

        } else if (usdPrim && (usdPrim.IsA<UsdShadeShader>() || 
                               usdPrim.IsA<UsdShadeNodeGraph>())) {
            // Shader edits get forwarded to parent material. Note if the
            // material is native instanced, we need to stop the traversal
            // at the prototype, since the corresponding instance prim will be
            // of type material but the prototype is typeless.
            while (usdPrim &&
                   !usdPrim.IsA<UsdShadeMaterial>() &&
                   !usdPrim.IsPrototype()) {
                usdPrim = usdPrim.GetParent();
            }
            if (usdPrim) {
                TF_DEBUG(USDIMAGING_CHANGES).Msg("[Refresh Object]: "
                    "Shader property <%s> modified; updating material <%s>.\n",
                    usdPath.GetText(), usdPrim.GetPath().GetText());
                auto range = _dependencyInfo.equal_range(usdPrim.GetPath());
                for (auto it = range.first; it != range.second; ++it) {
                    SdfPath cachePath = it->second;
                    if (_GetHdPrimInfo(cachePath)) {
                        affectedCachePaths.push_back(cachePath);
                    }
                }
            }
        } else {
            // Only include non-inherited properties for prims that we are
            // explicitly tracking in the render index.
            auto range = _dependencyInfo.equal_range(usdPrimPath);
            for (auto it = range.first; it != range.second; ++it) {
                SdfPath cachePath = it->second;
                if (_GetHdPrimInfo(cachePath)) {
                    affectedCachePaths.push_back(cachePath);
                }
            }
        }
    }

    // PERFORMANCE: We could execute this in parallel, for large numbers of
    // prims.
    for (SdfPath const& affectedCachePath: affectedCachePaths) {

        _HdPrimInfo *primInfo = _GetHdPrimInfo(affectedCachePath);

        TF_DEBUG(USDIMAGING_CHANGES).Msg("  - affected prim: <%s>\n",
                affectedCachePath.GetText());

        // Due to the ResyncPrim condition when AllDirty is returned below, we
        // may or may not find an associated primInfo for every prim in
        // affectedPrims. If we find no primInfo, the prim that was previously
        // affected by this refresh no longer exists and can be ignored.
        //
        // It is also possible that we find a primInfo, but the prim it refers
        // to has been deleted from the stage and is no longer valid. Such a
        // prim may end up in the affectedPrims during the refresh of a
        // collection that previously pointed directly to a prim that has
        // been deleted. The primInfo for this prim will still be in the index
        // because we haven't had the index process removals yet.
        if (primInfo != nullptr &&
            primInfo->usdPrim.IsValid() &&
            TF_VERIFY(primInfo->adapter, "%s", affectedCachePath.GetText())) {
            UsdImagingPrimAdapterSharedPtr &adapter = primInfo->adapter;

            // For the dirty bits that we've been told changed, go re-discover
            // variability and stage the associated data.
            HdDirtyBits dirtyBits = HdChangeTracker::Clean;
            if (usdPath.IsAbsoluteRootOrPrimPath()) {
                dirtyBits = adapter->ProcessPrimChange(
                    primInfo->usdPrim, affectedCachePath, changedInfoFields);
            } else if (usdPath.IsPropertyPath()) {
                dirtyBits = adapter->ProcessPropertyChange(
                    primInfo->usdPrim, affectedCachePath, usdPath.GetNameToken());
            } else {
                TF_VERIFY(false, "Unexpected path: <%s>", usdPath.GetText());
            }

            if (dirtyBits == HdChangeTracker::Clean) {
                // Do nothing
            } else if (dirtyBits != HdChangeTracker::AllDirty) {
                // Update variability, if we haven't already done so for this
                // prim while refreshing another prim that affects this one.
                if (allTrackedVariabilityPaths->find(affectedCachePath) ==
                    allTrackedVariabilityPaths->end()) {
                    _timeVaryingPrimCacheValid = false;
                    primInfo->timeVaryingBits = HdChangeTracker::Clean;
                    adapter->TrackVariability(primInfo->usdPrim,
                                              affectedCachePath,
                                              &primInfo->timeVaryingBits);
                    allTrackedVariabilityPaths->insert(affectedCachePath);
                }

                // Propagate the dirty bits back out to the change tracker.
                HdDirtyBits combinedBits =
                    dirtyBits | primInfo->timeVaryingBits;
                if (combinedBits != HdChangeTracker::Clean) {
                    adapter->MarkDirty(primInfo->usdPrim, affectedCachePath,
                                       combinedBits, proxy);
                }
            } else {
                // If we want to resync the hydra prim, generate a fake resync
                // notice for the usd prim in its primInfo.
                _ResyncUsdPrim(primInfo->usdPrim.GetPath(), cache, proxy);
                resyncNeeded = true;
            }
        }
    }

    return resyncNeeded;
}

// -------------------------------------------------------------------------- //
// Data Collection
// -------------------------------------------------------------------------- //

void
UsdImagingDelegate::_UpdateSingleValue(SdfPath const& cachePath,
                                       int requestBits)
{
    // XXX: potential race condition? UpdateSingleValue may be called from
    // multiple thread on a same path. we should probably need a guard here,
    // or in adapter
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo, "%s\n", cachePath.GetText()) &&
        TF_VERIFY(primInfo->adapter, "%s\n", cachePath.GetText())) {
        UsdImagingPrimAdapterSharedPtr &adapter = primInfo->adapter;
        adapter->UpdateForTime(primInfo->usdPrim, cachePath,
                               _time, requestBits);
    }
}

void
UsdImagingDelegate::_MarkRenderTagsDirty()
{
    UsdImagingIndexProxy indexProxy(this, nullptr);

    for (_HdPrimInfoMap::iterator it = _hdPrimInfoMap.begin();
            it != _hdPrimInfoMap.end();
            ++it) {
        const SdfPath& cachePath = it->first;
        _HdPrimInfo& primInfo = it->second;

        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkRenderTagDirty(primInfo.usdPrim,
                                                 cachePath,
                                                 &indexProxy);
        }
    }
}

void
UsdImagingDelegate::SetDisplayRender(const bool displayRender)
{
    if (_displayRender == displayRender) {
        return;
    }

    _displayRender = displayRender;

    // _displayRender changes a prims render tag.
    // So we need to make sure all prims render tags get re-evaluated.
    // XXX: Should be smarter and only invalidate prims whose
    // purpose == UsdGeomTokens->render.
    // Look at GetRenderTag for complexity with this.
    _MarkRenderTagsDirty();
}

void
UsdImagingDelegate::SetDisplayProxy(const bool displayProxy)
{
    if (_displayProxy == displayProxy) {
        return;
    }

    _displayProxy = displayProxy;

    // _displayProxy changes a prims render tag.
    // So we need to make sure all prims render tags get re-evaluated.
    // XXX: Should be smarter and only invalidate prims whose
    // purpose == UsdGeomTokens->proxy.
    // Look at GetRenderTag for complexity with this.
    _MarkRenderTagsDirty();
}

void
UsdImagingDelegate::SetDisplayGuides(const bool displayGuides)
{
    if (_displayGuides == displayGuides) {
        return;
    }

    _displayGuides = displayGuides;

    // _displayGuides changes a prims render tag.
    // So we need to make sure all prims render tags get re-evaluated.
    // XXX: Should be smarter and only invalidate prims whose
    // purpose == UsdGeomTokens->guide.
    // Look at GetRenderTag for complexity with this.
    _MarkRenderTagsDirty();
}

void
UsdImagingDelegate::SetUsdDrawModesEnabled(bool enableUsdDrawModes)
{
    if (_enableUsdDrawModes != enableUsdDrawModes) {
        if (_hdPrimInfoMap.size() > 0) {
            TF_CODING_ERROR("SetUsdDrawModesEnabled() was called after "
                            "population; this is currently unsupported...");
        } else {
            _enableUsdDrawModes = enableUsdDrawModes;
        }
    }
}

void
UsdImagingDelegate::SetSceneMaterialsEnabled(bool enable)
{
    if (_sceneMaterialsEnabled != enable)
    {
        _sceneMaterialsEnabled = enable;

        UsdImagingIndexProxy indexProxy(this, nullptr);

        // XXX: Need to unfortunately go through all prim info entries to
        // propagate dirtyness to gprims.
        for (auto& pair : _hdPrimInfoMap) {
            const SdfPath &cachePath = pair.first;
            _HdPrimInfo &primInfo = pair.second;
            if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
                primInfo.adapter->MarkMaterialDirty(primInfo.usdPrim,
                                                    cachePath, &indexProxy);
            }
        }
    }
}

void
UsdImagingDelegate::SetSceneLightsEnabled(bool enable)
{
    if (_sceneLightsEnabled != enable)
    {
        _sceneLightsEnabled = enable;

        UsdImagingIndexProxy indexProxy(this, nullptr);

        // XXX: Need to unfortunately go through all prim info entries to
        // propagate dirtyness to gprims.
        for (auto& pair : _hdPrimInfoMap) {
            const SdfPath &cachePath = pair.first;
            _HdPrimInfo &primInfo = pair.second;
            if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
                primInfo.adapter->MarkDirty(primInfo.usdPrim, cachePath, 
                    HdLight::DirtyParams | HdLight::DirtyResource, &indexProxy);
            }
        }
    }
}

void
UsdImagingDelegate::SetWindowPolicy(CameraUtilConformWindowPolicy policy)
{
    if (_appWindowPolicy != policy) {
        _appWindowPolicy = policy;

        UsdImagingIndexProxy indexProxy(this, nullptr);

        // XXX: Need to unfortunately go through all prim info entries to
        // propagate dirtyness to all the scene cameras.
        for (auto& pair : _hdPrimInfoMap) {
            const SdfPath &cachePath = pair.first;
            _HdPrimInfo &primInfo = pair.second;
             if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
                primInfo.adapter->MarkWindowPolicyDirty(primInfo.usdPrim,
                                                    cachePath, &indexProxy);
            }
        }
    }
}

void
UsdImagingDelegate::SetDisplayUnloadedPrimsWithBounds(bool displayUnloaded)
{
    if (_hdPrimInfoMap.size() > 0) {
        TF_CODING_ERROR("SetDisplayUnloadedPrimsWithBounds() was "
                        "called after population; this is currently "
                        "unsupported.");
    } else if (!_hasDrawModeAdapter) {
        TF_CODING_ERROR("This delegate does not have draw mode "
                        "adapter; unloaded prims cannot be displayed "
                        "with bounds.");
    } else {
        _displayUnloadedPrimsWithBounds = displayUnloaded;
    }
}

GfInterval 
UsdImagingDelegate::GetCurrentTimeSamplingInterval()
{
    TRACE_FUNCTION();

    float shutterOpen = 0.0f;
    float shutterClose = 0.0f;

    if (!_cameraPathForSampling.IsEmpty()) {
        const VtValue vShutterOpen = GetCameraParamValue(
            _cameraPathForSampling, 
            HdCameraTokens->shutterOpen);
        if (vShutterOpen.IsHolding<double>()) {
            shutterOpen = vShutterOpen.Get<double>();
        }

        const VtValue vShutterClose = GetCameraParamValue(
            _cameraPathForSampling, 
            HdCameraTokens->shutterClose);
        if (vShutterClose.IsHolding<double>()) {
            shutterClose = vShutterClose.Get<double>();
        }
    }

    return GfInterval(
        GetTimeWithOffset(shutterOpen).GetValue(), 
        GetTimeWithOffset(shutterClose).GetValue());
}

void
UsdImagingDelegate::SetCameraForSampling(SdfPath const& usdPath)
{
    _cameraPathForSampling = usdPath;
}

/*virtual*/
TfToken
UsdImagingDelegate::GetRenderTag(SdfPath const& id)
{
    TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    // Check the purpose of the rpim
    TfToken purpose = UsdGeomTokens->default_;

    if (TF_VERIFY(primInfo)) {
        purpose = primInfo->adapter->GetPurpose(primInfo->usdPrim, cachePath,
                    TfToken());
    }

    if (purpose == UsdGeomTokens->default_) {
        // Simple mapping so all render tags in multiple delegates match
        purpose = HdRenderTagTokens->geometry;
    } else if ((purpose == UsdGeomTokens->render && !_displayRender) ||
            (purpose == UsdGeomTokens->proxy && !_displayProxy) ||
            (purpose == UsdGeomTokens->guide && !_displayGuides)) {
        // When guides are disabled on the delegate we move the
        // guide prims to the hidden command buffer
        purpose = HdRenderTagTokens->hidden;
    }

    TF_DEBUG(USDIMAGING_COLLECTIONS).Msg("GetRenderTag %s -> %s \n",
                                cachePath.GetText(),
                                purpose.GetText());
    return purpose;
}

/*virtual*/
HdBasisCurvesTopology 
UsdImagingDelegate::GetBasisCurvesTopology(SdfPath const& id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        VtValue topology = primInfo->adapter->GetTopology(
            primInfo->usdPrim, 
            cachePath, 
            _time);
        if (topology.IsHolding<HdBasisCurvesTopology>()) {
            return topology.Get<HdBasisCurvesTopology>();
        }
    }

    return HdBasisCurvesTopology();
}

/*virtual*/
HdMeshTopology 
UsdImagingDelegate::GetMeshTopology(SdfPath const& id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        VtValue topology = primInfo->adapter->GetTopology(
            primInfo->usdPrim, 
            cachePath, 
            _time);
        if (topology.IsHolding<HdMeshTopology>()) {
            HdMeshTopology meshTopology = topology.Get<HdMeshTopology>();
            HdGeomSubsets geomSubsets;
            for(const HdGeomSubset& subset : meshTopology.GetGeomSubsets()) {
                geomSubsets.push_back(HdGeomSubset {
                    subset.type,
                    ConvertCachePathToIndexPath(subset.id),
                    ConvertCachePathToIndexPath(subset.materialId),
                    subset.indices
                });
            }
            meshTopology.SetGeomSubsets(geomSubsets);
            return meshTopology;
        }
    }

    return HdMeshTopology();
}

/*virtual*/
UsdImagingDelegate::SubdivTags 
UsdImagingDelegate::GetSubdivTags(SdfPath const& id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter
            ->GetSubdivTags(primInfo->usdPrim, cachePath, _time);
    }
    return PxOsdSubdivTags();
}

/*virtual*/
GfRange3d 
UsdImagingDelegate::GetExtent(SdfPath const& id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetExtent(
            primInfo->usdPrim, cachePath, _time);
    }
    return GfRange3d();
}

/*virtual*/ 
bool 
UsdImagingDelegate::GetDoubleSided(SdfPath const& id)
{
    bool doubleSided = false;
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo)) {
        doubleSided = primInfo->adapter->GetDoubleSided(
            primInfo->usdPrim, cachePath, _time);
    }
    
    return doubleSided;
}

/*virtual*/
HdCullStyle
UsdImagingDelegate::GetCullStyle(SdfPath const &id)
{
    // Cull style works a bit weirdly. Most adapters aren't
    // expected to use cullstyle, so: if it's there, use it, but otherwise
    // just use the fallback value.
    HdCullStyle cullStyle = HdCullStyleDontCare;

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        cullStyle = primInfo->adapter->GetCullStyle(
            primInfo->usdPrim, 
            cachePath,
            _time);
    }

    if (cullStyle == HdCullStyleDontCare) {
        cullStyle = _cullStyleFallback;
    }

    return cullStyle;
}

/*virtual*/ 
HdDisplayStyle 
UsdImagingDelegate::GetDisplayStyle(SdfPath const& id) 
{
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    int level = 0;
    if (TfMapLookup(_refineLevelMap, cachePath, &level))
        return HdDisplayStyle(level);
    return HdDisplayStyle(GetRefineLevelFallback());
}

/*virtual*/ 
HdModelDrawMode 
UsdImagingDelegate::GetModelDrawMode(SdfPath const& id) 
{
    HdModelDrawMode model;
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo)) {
        model = primInfo->adapter->GetFullModelDrawMode(primInfo->usdPrim);
    }
    
    return model;
}

void
UsdImagingDelegate::SetRefineLevelFallback(int level) 
{ 
    if (level == _refineLevelFallback || !_ValidateRefineLevel(level))
        return;
    _refineLevelFallback = level; 

    UsdImagingIndexProxy indexProxy(this, nullptr);

    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;

        // Dont mark prims with explicit refine levels as dirty.
        if (_refineLevelMap.find(cachePath) == _refineLevelMap.end()) {
            if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
                primInfo.adapter->MarkRefineLevelDirty(primInfo.usdPrim,
                                                       cachePath,
                                                       &indexProxy);
            }
        }
    }
}

void
UsdImagingDelegate::SetRefineLevel(SdfPath const& usdPath, int level) 
{ 
    if (!_ValidateRefineLevel(level))
        return;
    
    _RefineLevelMap::iterator it = _refineLevelMap.find(usdPath);
    if (it != _refineLevelMap.end()) {
        if (it->second == level)
            return;
        it->second = level;
    } else {
        // XXX(UsdImagingPaths): _refineLevelMap is keyed by cachePath,
        // not usdPath.
        _refineLevelMap[usdPath] = level;
        // Avoid triggering changes if the new level is the same as the
        // fallback.
        if (level == _refineLevelFallback)
            return;
    }

    UsdImagingIndexProxy indexProxy(this, nullptr);

    // XXX(UsdImagingPaths): We use the usdPath directly as the cachePath
    // here, but we should consult the adapter for this.
    SdfPath const& cachePath = usdPath;
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo, "%s", cachePath.GetText()) &&
        TF_VERIFY(primInfo->adapter, "%s", cachePath.GetText())) {
        primInfo->adapter->MarkRefineLevelDirty(primInfo->usdPrim,
                                                cachePath, &indexProxy);
    }
}

void
UsdImagingDelegate::ClearRefineLevel(SdfPath const& usdPath) 
{ 
    _RefineLevelMap::iterator it = _refineLevelMap.find(usdPath);
    if (it == _refineLevelMap.end())
        return;

    int oldLevel = it->second;
    _refineLevelMap.erase(it);
    if (oldLevel != _refineLevelFallback) {
        UsdImagingIndexProxy indexProxy(this, nullptr);

        // XXX(UsdImagingPaths): We use the usdPath directly as the cachePath
        // here, but we should consult the adapter for this.
        SdfPath const& cachePath = usdPath; // XXX ?!?
        _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
        if (TF_VERIFY(primInfo, "%s", cachePath.GetText()) &&
            TF_VERIFY(primInfo->adapter, "%s", cachePath.GetText())) {
            primInfo->adapter->MarkRefineLevelDirty(primInfo->usdPrim,
                                                    cachePath, &indexProxy);
        }
    }
}

void
UsdImagingDelegate::SetReprFallback(HdReprSelector const &repr)
{
    HD_TRACE_FUNCTION();

    if (_reprFallback == repr) {
        return;
    }
    _reprFallback = repr;

    UsdImagingIndexProxy indexProxy(this, nullptr);

    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;

        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkReprDirty(primInfo.usdPrim,
                                            cachePath, &indexProxy);
        }
    }
}

void
UsdImagingDelegate::SetCullStyleFallback(HdCullStyle cullStyle)
{
    HD_TRACE_FUNCTION();

    if (_cullStyleFallback == cullStyle) {
        return;
    }
    _cullStyleFallback = cullStyle;

    UsdImagingIndexProxy indexProxy(this, nullptr);

    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;

        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkCullStyleDirty(primInfo.usdPrim,
                                                 cachePath, &indexProxy);
        }
    }
}

void
UsdImagingDelegate::SetRootTransform(GfMatrix4d const& xf)
{
    HD_TRACE_FUNCTION();

    // TODO: do IsClose check.
    if (xf == _rootXf)
        return;

    _rootXf = xf;

    UsdImagingIndexProxy indexProxy(this, nullptr);

    // Mark dirty.
    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;
        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkTransformDirty(primInfo.usdPrim,
                                                 cachePath, &indexProxy);
        }
    }
}

void 
UsdImagingDelegate::SetInvisedPrimPaths(SdfPathVector const &invisedPaths)
{
    HD_TRACE_FUNCTION();

    if (_invisedPrimPaths == invisedPaths) {
        return;
    }

    SdfPathSet sortedNewInvisedPaths(invisedPaths.begin(), invisedPaths.end());
    SdfPathSet sortedExistingInvisedPaths(_invisedPrimPaths.begin(), 
                                          _invisedPrimPaths.end());
    SdfPathVector changingInvisPaths;
    std::set_symmetric_difference(sortedNewInvisedPaths.begin(), 
                                  sortedNewInvisedPaths.end(),
                                  sortedExistingInvisedPaths.begin(),
                                  sortedExistingInvisedPaths.end(),
                                  std::back_inserter(changingInvisPaths));

    SdfPath::RemoveDescendentPaths(&changingInvisPaths);
    TF_FOR_ALL(changingInvisPathIt, changingInvisPaths) {
        const SdfPath &usdSubtreeRoot = *changingInvisPathIt;
        const UsdPrim &usdPrim = _GetUsdPrim(usdSubtreeRoot);
        if (!usdPrim) {
            TF_CODING_ERROR("Could not find prim at path <%s>.", 
                            usdSubtreeRoot.GetText());
            continue;
        }

        TF_DEBUG(USDIMAGING_CHANGES).Msg("[Vis/Invis Prim] <%s>\n",
                                         usdSubtreeRoot.GetText());
        SdfPath visAttr =
            usdSubtreeRoot.AppendProperty(UsdGeomTokens->visibility);
        _usdPathsToUpdate.emplace(visAttr, TfTokenVector());
    }

    _invisedPrimPaths = invisedPaths;

    // process instance visibility.
    // this call is needed because we use _RefreshObject to repopulate
    // vis-ed/invis-ed instanced prims (accumulated in _usdPathsToUpdate)
    ApplyPendingUpdates();
}

void 
UsdImagingDelegate::SetRigidXformOverrides(
    RigidXformOverridesMap const &rigidXformOverrides)
{
    HD_TRACE_FUNCTION();

    if (_rigidXformOverrides == rigidXformOverrides) {
        return;
    }
    
    UsdImaging_XformCache::ValueOverridesMap overridesToUpdate;

    // Compute the set of overrides to update and update their values in the 
    // inherited xform cache.
    TF_FOR_ALL(it, rigidXformOverrides) {
        const SdfPath &overridePath = it->first;
        const UsdPrim &overridePrim = _GetUsdPrim(overridePath);

        RigidXformOverridesMap::const_iterator existingIt = 
            _rigidXformOverrides.find(overridePath);

        // If the existing value matches the incoming value, then skip the 
        // update.
        if (existingIt != _rigidXformOverrides.end()) {
            if (existingIt->second != it->second) {                
                overridesToUpdate[overridePrim] = it->second;
            }

            // Erase the entry in the existing map. At the end of the loop
            // the existing overrides map should contain only the overrides 
            // to be removed.
            _rigidXformOverrides.erase(overridePath);
        } else {
            // In this case, we're adding a new override.
            overridesToUpdate[overridePrim] = it->second;
        }
    }

    // Now, _rigidXformOverrides has the set of overrides to remove. 
    std::vector<UsdPrim> overridesToRemove;
    TF_FOR_ALL(it, _rigidXformOverrides) {
        UsdPrim overridePrim = _GetUsdPrim(it->first);
        overridesToRemove.push_back(overridePrim);
    }

    SdfPathVector dirtySubtreeRoots;
    _xformCache.UpdateValueOverrides(overridesToUpdate, overridesToRemove,
        &dirtySubtreeRoots);

    SdfPath::RemoveDescendentPaths(&dirtySubtreeRoots);

    // Mark dirty.
    TF_FOR_ALL(it, dirtySubtreeRoots) {
        const SdfPath &subtreeRoot = *it;
        const UsdPrim &usdPrim = _GetUsdPrim(subtreeRoot);
        if (!usdPrim) {
            TF_CODING_ERROR("Could not find prim at path <%s>.", 
                subtreeRoot.GetText());
            continue;
        }

        TF_DEBUG(USDIMAGING_CHANGES).Msg("[RigidXform override] <%s>\n",
                                         subtreeRoot.GetText());

        SdfPath xformAttr =
            subtreeRoot.AppendProperty(UsdGeomTokens->xformOpOrder);
        _usdPathsToUpdate.emplace(xformAttr, TfTokenVector());
    }

    _rigidXformOverrides = rigidXformOverrides;

    // process transforms.
    // this call is needed because we use _RefreshObject to repopulate
    // vis-ed/invis-ed instanced prims (accumulated in _usdPathsToUpdate)
    ApplyPendingUpdates();
}

void
UsdImagingDelegate::SetRootVisibility(bool isVisible)
{
    if (isVisible == _rootIsVisible) {
        return;
    }
    _rootIsVisible = isVisible;

    UsdImagingIndexProxy indexProxy(this, nullptr);

    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;

        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkVisibilityDirty(primInfo.usdPrim,
                                                  cachePath,
                                                  &indexProxy);
        }
    }
}

void
UsdImagingDelegate::SetRootInstancerId(SdfPath const& instancerId)
{
    if (instancerId == _rootInstancerId) {
        return;
    }
    _rootInstancerId = instancerId;

    UsdImagingIndexProxy indexProxy(this, nullptr);

    // Mark dirty.
    TF_FOR_ALL(it, _hdPrimInfoMap) {
        const SdfPath &cachePath = it->first;
        _HdPrimInfo &primInfo = it->second;
        if (TF_VERIFY(primInfo.adapter, "%s", cachePath.GetText())) {
            primInfo.adapter->MarkDirty(primInfo.usdPrim,
                                        cachePath,
                                        HdChangeTracker::DirtyInstancer,
                                        &indexProxy);
        }
    }
}

SdfPath 
UsdImagingDelegate::GetScenePrimPath(SdfPath const& rprimId,
                                     int instanceIndex,
                                     HdInstancerContext *instancerContext)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(rprimId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (!primInfo || !primInfo->adapter) {
        TF_WARN("GetScenePrimPath: Couldn't find rprim <%s>",
                rprimId.GetText());
        return cachePath;
    }

    SdfPath protoPath = primInfo->adapter->GetScenePrimPath(
        cachePath, instanceIndex, instancerContext);

    if (TfDebug::IsEnabled(USDIMAGING_SELECTION)) {
        std::stringstream ic;
        if (instancerContext) {
            for (auto const& pair : *instancerContext) {
                ic << pair.first << ": " << pair.second << ",";
            }
        } else {
            ic << "no instancerContext";
        }
        TF_DEBUG(USDIMAGING_SELECTION).Msg(
            "GetScenePrimPath(%s, %d) = %s [%s]\n",
            cachePath.GetText(), instanceIndex, protoPath.GetText(),
            ic.str().c_str());
    }

    return protoPath;
}

SdfPathVector
UsdImagingDelegate::GetScenePrimPaths(SdfPath const& rprimId,
                      std::vector<int> instanceIndices,
                      std::vector<HdInstancerContext> *instancerContexts)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(rprimId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (!primInfo || !primInfo->adapter) {
        TF_WARN("GetScenePrimPaths: Couldn't find rprim <%s>",
                rprimId.GetText());
        return SdfPathVector(instanceIndices.size(), cachePath);
    }

    return primInfo->adapter->GetScenePrimPaths(
        cachePath, instanceIndices, instancerContexts);
}

bool
UsdImagingDelegate::PopulateSelection(
              HdSelection::HighlightMode const& highlightMode,
              SdfPath const &usdPath,
              int instanceIndex,
              HdSelectionSharedPtr const &result)
{
    HD_TRACE_FUNCTION();

    // Since it is technically possible to call PopulateSelection() before
    // Populate(), we guard access to _stage.  Ideally this would be a TF_VERIFY
    // but some clients need to be fixed first.
    if (!_stage) {
        return false;
    }

    // Process any pending path resyncs/updates first to ensure all
    // adapters are up-to-date.
    // XXX: This should be removed from here.  There were some invalidation
    // bugs in older usdview UI; we should check and see if that's still the
    // case; if so, we can find a better place to call ApplyPendingUpdates.
    ApplyPendingUpdates();

    TF_DEBUG(USDIMAGING_SELECTION).Msg("Prim selection: %s\n",
                                       usdPath.GetText());

    UsdPrim selectedPrim = _stage->GetPrimAtPath(usdPath);
    if (!selectedPrim) {
        return false;
    }

    // If the USD prim is an instance proxy, we won't find any dependent prims,
    // since the instance proxy is a virtual prim and technically the subtree
    // stops at the instance.  We need to call _GatherDependencies from the
    // instance, so we walk up the hierarchy.
    // Note: the pseudoroot can't be an instance proxy, so we won't walk into
    // the void here...
    UsdPrim usdPrim = selectedPrim;
    while (usdPrim && usdPrim.IsInstanceProxy()){
        usdPrim = usdPrim.GetParent();
    }
    SdfPath rootPath = usdPrim.GetPath();

    SdfPathVector affectedCachePaths;
    _GatherDependencies(rootPath, &affectedCachePaths);

    std::sort(affectedCachePaths.begin(), affectedCachePaths.end());
    auto last = std::unique(affectedCachePaths.begin(),
                            affectedCachePaths.end(),
                            [](SdfPath const &l, SdfPath const &r) {
                                return r.HasPrefix(l);
                            });
    affectedCachePaths.erase(last, affectedCachePaths.end());

    // Loop through gathered prims and add them to the selection set
    bool added = false;
    for (size_t i = 0; i < affectedCachePaths.size(); ++i) {
        SdfPath const& affectedCachePath = affectedCachePaths[i];

        _HdPrimInfo *primInfo = _GetHdPrimInfo(affectedCachePath);
        if (primInfo == nullptr) {
            TF_CODING_ERROR("Couldn't find primInfo for cache path %s",
                affectedCachePath.GetText());
            continue;
        }
        if (!TF_VERIFY(primInfo->adapter, "%s",affectedCachePath.GetText())) {
            continue;
        }

        UsdImagingPrimAdapterSharedPtr const &adapter = primInfo->adapter;

        TF_DEBUG(USDIMAGING_SELECTION).Msg("- affected hydra prim: %s\n",
                affectedCachePath.GetText());

        added |= adapter->PopulateSelection(highlightMode, affectedCachePath,
                selectedPrim, instanceIndex, VtIntArray(), result);
    }
    return added;
}

/*virtual*/
GfMatrix4d 
UsdImagingDelegate::GetTransform(SdfPath const& id)
{
    TRACE_FUNCTION();

    GfMatrix4d ctm(1.0);
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetTransform(
            primInfo->usdPrim, 
            cachePath, 
            _time);
    }
    return ctm;
}

/*virtual*/
size_t
UsdImagingDelegate::SampleTransform(SdfPath const & id, 
                                    size_t maxNumSamples,
                                    float *sampleTimes, 
                                    GfMatrix4d *sampleValues)
{
    TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->SampleTransform(
            primInfo->usdPrim, cachePath, 
            _time, maxNumSamples,
            sampleTimes, sampleValues);
    }
    return 0;
}

bool
UsdImagingDelegate::IsInInvisedPaths(SdfPath const &usdPath) const
{
    for (const auto& it: _invisedPrimPaths) {
        if (usdPath.HasPrefix(it)) {
            return true;
        }
    }
    return false;
}

/*virtual*/
bool
UsdImagingDelegate::GetVisible(SdfPath const& id)
{
    TRACE_FUNCTION();

    // Root visibility overrides prim visibility.
    if (!_rootIsVisible) {
        return false;
    }

    SdfPath cachePath = ConvertIndexPathToCachePath(id);

    // For instance protos (not IsPrimPath), visibility is
    // controlled by instanceIndices.
    if (cachePath.IsPrimPath() && IsInInvisedPaths(cachePath)) {
        return false;
    }

    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetVisible(
            primInfo->usdPrim, 
            cachePath, 
            _time);
    }
    return false;
}

/*virtual*/ 
VtValue 
UsdImagingDelegate::Get(SdfPath const& id, TfToken const& key)
{
    return _Get(id, key, nullptr);
}

/*virtual*/ 
VtValue 
UsdImagingDelegate::GetIndexedPrimvar(SdfPath const& id, 
                                      TfToken const& key, 
                                      VtIntArray *outIndices)
{
    return _Get(id, key, outIndices);
}

VtValue 
UsdImagingDelegate::_Get(SdfPath const& id, TfToken const& key, 
                         VtIntArray *outIndices)
{
    HD_TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    VtValue value;
    if (outIndices) {
        outIndices->clear();
    }

    _HdPrimInfo const *primInfo = _GetHdPrimInfo(cachePath);
    if (!TF_VERIFY(primInfo)) {
        return value;
    }

    UsdPrim const & prim = primInfo->usdPrim;
    if (!TF_VERIFY(prim)) {
        return value;
    }
    value = primInfo->adapter->Get(prim, cachePath, key, _time, outIndices);

    // We generally don't want Vec2d arrays, convert to vec2f.
    if (value.IsHolding<VtVec2dArray>()) {
        value = VtValue::Cast<VtVec2fArray>(value);
    }

    return value;
}

HdIdVectorSharedPtr
UsdImagingDelegate::GetCoordSysBindings(SdfPath const& id)
{
    if (!_coordSysEnabled) {
        return nullptr;
    }
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (!TF_VERIFY(primInfo) || !TF_VERIFY(primInfo->usdPrim)) {
        return nullptr;
    }

    // CachePaths for the bindings are stored in the idVecPtr, convert 
    // them into IndexPaths before returning.
    HdIdVectorSharedPtr bindingPathVector =
        _coordSysBindingCache.GetValue(primInfo->usdPrim).idVecPtr;
    if (bindingPathVector) {
        HdIdVectorSharedPtr indexVec = HdIdVectorSharedPtr(new SdfPathVector());
        for (SdfPath bindingCachePath : *bindingPathVector) {
            indexVec->push_back(ConvertCachePathToIndexPath(bindingCachePath));
        }
        return indexVec;
    }
    return bindingPathVector;
}

/*virtual*/
size_t
UsdImagingDelegate::SamplePrimvar(SdfPath const& id,
                                  TfToken const& key,
                                  size_t maxNumSamples,
                                  float *sampleTimes, 
                                  VtValue *sampleValues)
{
    return _SamplePrimvar(id, key, maxNumSamples, sampleTimes, sampleValues, 
                          nullptr);
}

/*virtual*/
size_t
UsdImagingDelegate::SampleIndexedPrimvar(SdfPath const& id,
                                  TfToken const& key,
                                  size_t maxNumSamples,
                                  float *sampleTimes, 
                                  VtValue *sampleValues,
                                  VtIntArray *sampleIndices)
{
    return _SamplePrimvar(id, key, maxNumSamples, sampleTimes, sampleValues, 
                          sampleIndices);
}

size_t
UsdImagingDelegate::_SamplePrimvar(SdfPath const& id,
                                  TfToken const& key,
                                  size_t maxNumSamples,
                                  float *sampleTimes, 
                                  VtValue *sampleValues,
                                  VtIntArray *sampleIndices)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        if (sampleIndices) {
            sampleIndices[0] = VtIntArray(0);
        }
        // Retrieve the multi-sampled result.
        size_t nSamples = primInfo->adapter
            ->SamplePrimvar(primInfo->usdPrim, cachePath, key,
                            _time, maxNumSamples, sampleTimes, sampleValues, 
                            sampleIndices);
        return nSamples;
    }
    return 0;
}

/*virtual*/
HdReprSelector
UsdImagingDelegate::GetReprSelector(SdfPath const &id)
{
    return _reprFallback;
}

/*virtual*/
VtArray<TfToken>
UsdImagingDelegate::GetCategories(SdfPath const &id)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    
    // XXX: We must not ask the collection cache about instancer prototypes.
    // When instancer prototypes had property paths, the collection cache
    // would return an empty list for them. Now that they have prim paths,
    // the collection cache will return the list of collections inherited by
    // the prototype from the first instance. If subsequent instances belong to
    // a conflicting set of collections, we get incorrect results. Since the
    // collection cache has no way to identify prototype paths, we must do the
    // check here where we have access to the adapter. Instances will receive
    // the correct list of collections via GetInstanceCategories().
    //
    // Note the IsChildPath clause below. This will produce categories for
    // point instancer prims (but not native instancers).
    // Targeting prototype prims under the point instancer prim (but not the
    // point instancer prim itself) is not supported.
    //
    _HdPrimInfo* primInfo = _GetHdPrimInfo(cachePath);
    if (primInfo &&
        primInfo->adapter &&
        primInfo->adapter->IsInstancerAdapter() &&
        primInfo->adapter->IsChildPath(cachePath)) {
        return { };
    }
    return _collectionCache.ComputeCollectionsContainingPath(cachePath);
}

/*virtual*/
std::vector<VtArray<TfToken>>
UsdImagingDelegate::GetInstanceCategories(SdfPath const &instancerId)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(instancerId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        // GetInstanceCategories is implemented by the native instancing
        // adapter, but *not* the point instancer adapter.
        // GetCategories will return the categories for light linking
        // collections that target the point instancer prim. See above.
        //
        return primInfo->adapter
            ->GetInstanceCategories(primInfo->usdPrim);
    }
    return std::vector<VtArray<TfToken>>();
}

// -------------------------------------------------------------------------- //
// Primvar Support Methods
// -------------------------------------------------------------------------- //

HdPrimvarDescriptorVector
UsdImagingDelegate::GetPrimvarDescriptors(SdfPath const& id,
                                          HdInterpolation interpolation)
{
    HD_TRACE_FUNCTION();
    SdfPath cachePath = ConvertIndexPathToCachePath(id);

    HdPrimvarDescriptorVector allPrimvars;
    if (!_primvarDescCache.FindPrimvars(cachePath, &allPrimvars)) {

        // NOTE: One possible reason for not having an entry in the primvar
        //       cache is if something downstream is querying for this during
        //       the traditional "population" phase. In that case, allow it to
        //       fill that cache by calling UpdateForTime early.
        //
        _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
        if (TF_VERIFY(primInfo)) {
            primInfo->adapter->UpdateForTime(
                primInfo->usdPrim, cachePath,
                GetTime(),
                HdChangeTracker::Clean
                    // primvars + built-ins tracked as primvars
                    | HdChangeTracker::DirtyPrimvar 
                    | HdChangeTracker::DirtyPoints
                    | HdChangeTracker::DirtyNormals
                    | HdChangeTracker::DirtyWidths);
            _primvarDescCache.FindPrimvars(cachePath, &allPrimvars);
        }
    }

    // Filter to only primvars of the right interpolation.
    // Note: it's valid to have no authored primvars (they could be computed)
    HdPrimvarDescriptorVector primvars;
    for (HdPrimvarDescriptor const& pv: allPrimvars) {
        // Filter the stored primvars to just ones of the requested type.
        if (pv.interpolation == interpolation) {
            primvars.push_back(pv);
        }
    }
    return primvars;
}

/*virtual*/
VtIntArray
UsdImagingDelegate::GetInstanceIndices(SdfPath const &instancerId,
                                       SdfPath const &prototypeId)
{
    HD_TRACE_FUNCTION();

    SdfPath prototypeCachePath = ConvertIndexPathToCachePath(prototypeId);
    VtValue indices;

    SdfPath cachePath = ConvertIndexPathToCachePath(instancerId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo)) {
        indices = primInfo->adapter->GetInstanceIndices(
            primInfo->usdPrim, cachePath, prototypeCachePath, _time);
    }

    if (indices.IsEmpty()) {
        TF_WARN("Empty InstanceIndices (%s, %s)\n",
                instancerId.GetText(), prototypeId.GetText());
        return VtIntArray();
    }

    return indices.Get<VtIntArray>();
}

/*virtual*/
GfMatrix4d
UsdImagingDelegate::GetInstancerTransform(SdfPath const &instancerId)
{
    TRACE_FUNCTION();

    GfMatrix4d ctm(1.0);
    SdfPath cachePath = ConvertIndexPathToCachePath(instancerId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        ctm = primInfo->adapter->GetInstancerTransform(
            primInfo->usdPrim, 
            cachePath, 
            _time);
    }
    return ctm;
}

/*virtual*/
size_t
UsdImagingDelegate::SampleInstancerTransform(SdfPath const &instancerId,
                                             size_t maxSampleCount,
                                             float *sampleTimes,
                                             GfMatrix4d *sampleValues)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(instancerId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter
            ->SampleInstancerTransform(primInfo->usdPrim, cachePath,
                                       _time, maxSampleCount, 
                                       sampleTimes, sampleValues);
    }
    return 0;
}

/*virtual*/
SdfPath
UsdImagingDelegate::GetInstancerId(SdfPath const &primId)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(primId);
    SdfPath pathValue;

    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        pathValue =
            primInfo->adapter->GetInstancerId(primInfo->usdPrim, cachePath);
    }

    pathValue = ConvertCachePathToIndexPath(pathValue);

    if (pathValue.IsEmpty()) {
        return _rootInstancerId;
    }

    return pathValue;
}

/*virtual*/
SdfPathVector
UsdImagingDelegate::GetInstancerPrototypes(SdfPath const &instancerId)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(instancerId);
    SdfPathVector protos;

    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        protos =
            primInfo->adapter->GetInstancerPrototypes(primInfo->usdPrim, cachePath);
    }

    for (size_t i = 0; i < protos.size(); ++i) {
        protos[i] = ConvertCachePathToIndexPath(protos[i]);
    }
    return protos;
}

/*virtual*/ 
SdfPath 
UsdImagingDelegate::GetMaterialId(SdfPath const &rprimId)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(rprimId);
    SdfPath pathValue;

    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo, "No primInfo for <%s> <%s>",
        rprimId.GetText(), cachePath.GetText())) {
        pathValue = primInfo->adapter->GetMaterialId(
            primInfo->usdPrim, cachePath, _time);
    }

    return ConvertCachePathToIndexPath(pathValue);
}

VtValue
UsdImagingDelegate::GetMaterialResource(SdfPath const &materialId)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(materialId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (!TF_VERIFY(primInfo)) {
        return VtValue();
    }

    return primInfo->adapter->GetMaterialResource(
        primInfo->usdPrim, cachePath, _time);
}

VtValue 
UsdImagingDelegate::GetLightParamValue(SdfPath const &id, 
                                       TfToken const &paramName)
{
    if (!TF_VERIFY(id != SdfPath())) {
        return VtValue();
    }

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo* primInfo = _GetHdPrimInfo(cachePath);
    if (!TF_VERIFY(primInfo)) {
        return VtValue();
    }

    return primInfo->adapter->GetLightParamValue(primInfo->usdPrim,
        cachePath, paramName, _time);
}

VtValue 
UsdImagingDelegate::GetCameraParamValue(SdfPath const &id, 
                                        TfToken const &paramName)
{
    TRACE_FUNCTION();

    if (paramName == HdCameraTokens->windowPolicy) {
        // Hydra expects the window policy to be authored on the camera.
        // Since UsdGeomCamera doesn't have this property, we store the app
        // state via SetWindowPolicy (see above).
        return VtValue(_appWindowPolicy);
    }

    SdfPath cachePath = ConvertIndexPathToCachePath(id);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->Get(
            primInfo->usdPrim, 
            cachePath, 
            paramName,
            _time,
            nullptr);
    }
    return VtValue();
}

HdVolumeFieldDescriptorVector
UsdImagingDelegate::GetVolumeFieldDescriptors(SdfPath const &volumeId)
{
    // PERFORMANCE: We should schedule this to be updated during Sync, rather
    // than pulling values on demand.
    SdfPath cachePath = ConvertIndexPathToCachePath(volumeId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter
            ->GetVolumeFieldDescriptors(primInfo->usdPrim, cachePath, _time);
    }

    return HdVolumeFieldDescriptorVector();
}

TfTokenVector
UsdImagingDelegate::GetExtComputationSceneInputNames(
    SdfPath const& computationId)
{
    HD_TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter
            ->GetExtComputationSceneInputNames(cachePath);
    }
    return TfTokenVector();
}

HdExtComputationInputDescriptorVector
UsdImagingDelegate::GetExtComputationInputDescriptors(
    SdfPath const& computationId)
{
    HD_TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter
            ->GetExtComputationInputs(primInfo->usdPrim, cachePath, 
                                      nullptr /* instancerContext */);
    }
    return HdExtComputationInputDescriptorVector();
}

HdExtComputationOutputDescriptorVector
UsdImagingDelegate::GetExtComputationOutputDescriptors(
    SdfPath const& computationId)
{
    HD_TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetExtComputationOutputs(
                   primInfo->usdPrim, cachePath, 
                   nullptr /* instancerContext */); 
    }
    return HdExtComputationOutputDescriptorVector();
}

HdExtComputationPrimvarDescriptorVector
UsdImagingDelegate::GetExtComputationPrimvarDescriptors(
    SdfPath const& computationId,
    HdInterpolation interpolation)
{
    HD_TRACE_FUNCTION();
    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);

    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetExtComputationPrimvars(
                   primInfo->usdPrim, cachePath, interpolation,
                   nullptr /* instancerContext */); 
    }

    return HdExtComputationPrimvarDescriptorVector();

}

VtValue
UsdImagingDelegate::GetExtComputationInput(SdfPath const& computationId,
                                           TfToken const& input)
{
    TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetExtComputationInput(
                       primInfo->usdPrim, cachePath, input, GetTime(),
                       nullptr /* instancerContext */); 
    }

    return VtValue();
}

size_t
UsdImagingDelegate::SampleExtComputationInput(SdfPath const& computationId,
                                              TfToken const& input,
                                              size_t maxSampleCount,
                                              float *sampleTimes,
                                              VtValue *sampleValues)
{
    TRACE_FUNCTION();

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->SampleExtComputationInput(
            primInfo->usdPrim, cachePath, input, GetTime(),
            nullptr /* instancerContext */, maxSampleCount, sampleTimes,
            sampleValues);
    }

    return 0;
}

std::string
UsdImagingDelegate::GetExtComputationKernel(SdfPath const& computationId)
{
    TRACE_FUNCTION();
    
    if (computationId.IsEmpty()) {
        return std::string();
    }

    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);
    if (TF_VERIFY(primInfo)) {
        return primInfo->adapter->GetExtComputationKernel(
                       primInfo->usdPrim, cachePath,
                       nullptr /* instancerContext */); 
    }

    return std::string();
}

void
UsdImagingDelegate::InvokeExtComputation(SdfPath const& computationId,
                                         HdExtComputationContext *context)
{
    SdfPath cachePath = ConvertIndexPathToCachePath(computationId);
    _HdPrimInfo *primInfo = _GetHdPrimInfo(cachePath);

    if (TF_VERIFY(primInfo, "%s\n", cachePath.GetText()) &&
        TF_VERIFY(primInfo->adapter, "%s\n", cachePath.GetText())) {
        primInfo->adapter->InvokeComputation(cachePath, context);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

