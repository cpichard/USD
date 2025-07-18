//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usdGeom/bboxCache.h"

#include "pxr/usd/kind/registry.h"

#include "pxr/usd/usdGeom/boundable.h"
#include "pxr/usd/usdGeom/debugCodes.h"
#include "pxr/usd/usdGeom/modelAPI.h"
#include "pxr/usd/usdGeom/pointBased.h"
#include "pxr/usd/usdGeom/xform.h"

#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/primRange.h"

#include "pxr/base/trace/trace.h"

#include "pxr/base/work/withScopedParallelism.h"

#include "pxr/base/tf/hash.h"
#include "pxr/base/tf/pyLock.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"

#include <tbb/enumerable_thread_specific.h>
#include <algorithm>
#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// For code that knows at compile time whether we need to apply a transform.
// Such code is told through a template parameter TransformType whether to
// apply a transform (TransformType = GfMatrix4d) or not (TransformType =
// _IdentityTransform).
class _IdentityTransform
{
};

// Overloads to transform GfBBox3d.
inline void
_Transform(GfBBox3d * const bbox, const _IdentityTransform &m)
{
}

inline void
_Transform(GfBBox3d * const bbox, const GfMatrix4d &m)
{
    bbox->Transform(m);
}

}

// Thread-local Xform cache.
// This should be replaced with (TBD) multi-threaded XformCache::Prepopulate
typedef tbb::enumerable_thread_specific<UsdGeomXformCache> _ThreadXformCache;

// -------------------------------------------------------------------------- //
// _BBoxTask
// -------------------------------------------------------------------------- //
class UsdGeomBBoxCache::_BBoxTask {
    UsdGeomBBoxCache::_PrimContext _primContext;
    GfMatrix4d _inverseComponentCtm;
    UsdGeomBBoxCache* _owner;
    _ThreadXformCache* _xfCaches;
public:
    _BBoxTask() : _owner(nullptr), _xfCaches(nullptr) {}
    _BBoxTask(const _PrimContext& primContext, 
              const GfMatrix4d &inverseComponentCtm,
              UsdGeomBBoxCache* owner, _ThreadXformCache* xfCaches)
        : _primContext(primContext)
        , _inverseComponentCtm(inverseComponentCtm)
        , _owner(owner)
        , _xfCaches(xfCaches)
    {
    }
    explicit operator bool() const {
        return _owner;
    }
    void operator()() const {
        // Do not save state here; all state should be accumulated externally.
        _owner->_ResolvePrim(const_cast<_BBoxTask const *>(this), _primContext, _inverseComponentCtm);
    }
    _ThreadXformCache* GetXformCaches() const { return _xfCaches; }
};

// -------------------------------------------------------------------------- //
// _PrototypeBBoxResolver
//
// If a prototype prim has instances nested within it, resolving its bbox
// will depend on the prototypes for those instances being resolved first.
// These dependencies form an acyclic graph where a given prototype may depend
// on and be a dependency for one or more prototypes.
//
// This helper object tracks those dependencies as tasks are dispatched
// and completed.
// -------------------------------------------------------------------------- //
class UsdGeomBBoxCache::_PrototypeBBoxResolver
{
private:
    UsdGeomBBoxCache* _owner;

    struct _PrototypeTask
    {
        _PrototypeTask()
            : numDependencies(0) { }

        _PrototypeTask(const _PrototypeTask &other)
            : dependentPrototypes(other.dependentPrototypes)
        {
            numDependencies.store(other.numDependencies.load());
        }

        _PrototypeTask(_PrototypeTask &&other)
            : dependentPrototypes(std::move(other.dependentPrototypes))
        {
            numDependencies.store(other.numDependencies.load());
        }

        // Number of dependencies -- prototype prims that must be resolved
        // before this prototype can be resolved.
        std::atomic<size_t> numDependencies;

        // List of prototype prims that depend on this prototype.
        std::vector<_PrimContext> dependentPrototypes;
    };

    typedef TfHashMap<_PrimContext, _PrototypeTask, _PrimContextHash>
        _PrototypeTaskMap;

public:
    _PrototypeBBoxResolver(UsdGeomBBoxCache* bboxCache)
        : _owner(bboxCache)
    {
    }

    void Resolve(const std::vector<_PrimContext> &prototypePrimContexts)
    {
        TRACE_FUNCTION();

        _PrototypeTaskMap prototypeTasks;
        for (const auto& prototypePrim : prototypePrimContexts) {
            _PopulateTasksForPrototype(prototypePrim, &prototypeTasks);
        }

        // Using the owner's xform cache won't provide a benefit
        // because the prototypes are separate parts of the scenegraph
        // that won't be traversed when resolving other bounding boxes.
        _ThreadXformCache xfCache;

        for (const auto& t : prototypeTasks) {
            if (t.second.numDependencies == 0) {
                _owner->_dispatcher.Run(
                    &_PrototypeBBoxResolver::_ExecuteTaskForPrototype,
                    this, t.first, &prototypeTasks, &xfCache,
                    &_owner->_dispatcher);
            }
        }
        _owner->_dispatcher.Wait();
    }

private:
    void _PopulateTasksForPrototype(const _PrimContext& prototypePrim,
                                 _PrototypeTaskMap* prototypeTasks)
    {
        std::pair<_PrototypeTaskMap::iterator, bool> prototypeTaskStatus =
            prototypeTasks->insert(std::make_pair(
                    prototypePrim, _PrototypeTask()));
        if (!prototypeTaskStatus.second) {
            return;
        }

        std::vector<_PrimContext> requiredPrototypes;
        _owner->_FindOrCreateEntriesForPrim(prototypePrim, &requiredPrototypes);

        {
            // In order to resolve the bounding box for prototypePrim, we need
            // to compute the bounding boxes for all prototypes for nested
            // instances.
            _PrototypeTask& prototypeTaskData = 
                prototypeTaskStatus.first->second;
            prototypeTaskData.numDependencies = requiredPrototypes.size();
        }

        // Recursively populate the task map for the prototypes needed for
        // nested instances.
        for (const auto& reqPrototype : requiredPrototypes) {
            _PopulateTasksForPrototype(reqPrototype, prototypeTasks);
            (*prototypeTasks)[reqPrototype].dependentPrototypes.push_back(
                prototypePrim);
        }
    }

    void _ExecuteTaskForPrototype(const _PrimContext& prototype,
                               _PrototypeTaskMap* prototypeTasks,
                               _ThreadXformCache* xfCaches,
                               WorkDispatcher* dispatcher)
    {
        UsdGeomBBoxCache::_BBoxTask(
            prototype, GfMatrix4d(1.0), _owner, xfCaches)();
        
        // Update all of the prototype prims that depended on the completed
        // prototype and dispatch new tasks for those whose dependencies have
        // been resolved.  We're guaranteed that all the entries were populated
        // by _PopulateTasksForPrototype, so we don't check the result of
        // 'find()'.
        const _PrototypeTask& prototypeData =
            prototypeTasks->find(prototype)->second;
        for (const auto& dependentPrototype : 
                 prototypeData.dependentPrototypes) {
            _PrototypeTask& dependentPrototypeData =
                prototypeTasks->find(dependentPrototype)->second;
            if (dependentPrototypeData.numDependencies
                .fetch_sub(1) == 1){
                dispatcher->Run(
                    &_PrototypeBBoxResolver::_ExecuteTaskForPrototype,
                    this, dependentPrototype, prototypeTasks, xfCaches,
                    dispatcher);
            }
        }
    }

};

// -------------------------------------------------------------------------- //
// Helper functions for managing query objects
// -------------------------------------------------------------------------- //

namespace
{
// Enumeration of queries stored for each cached entry that varies
// over time.
enum _Queries {
    Extent = 0,

    // Note: code in _ResolvePrim relies on ExtentsHint being last.
    ExtentsHint,
    NumQueries
};
}

#define DEFINE_QUERY_ACCESSOR(Name, Schema)                             \
static const UsdAttributeQuery&                                         \
_GetOrCreate##Name##Query(const UsdPrim& prim, UsdAttributeQuery* q)    \
{                                                                       \
    if (!*q) {                                                       \
        if (Schema s = Schema(prim)) {                                  \
            UsdAttribute attr = s.Get##Name##Attr();                    \
            if (TF_VERIFY(attr, "Unable to get attribute '%s' on prim " \
                          "at path <%s>", #Name,                        \
                          prim.GetPath().GetText())) {                  \
                *q = UsdAttributeQuery(attr);                           \
            }                                                           \
        }                                                               \
    }                                                                   \
    return *q;                                                          \
}

DEFINE_QUERY_ACCESSOR(Extent, UsdGeomBoundable);
DEFINE_QUERY_ACCESSOR(Visibility, UsdGeomImageable);

// ExtentsHint is a custom attribute so we need an additional check
// to see if the attribute exists.

static const UsdAttributeQuery&
_GetOrCreateExtentsHintQuery(UsdGeomModelAPI& geomModel, UsdAttributeQuery* q)
{
    if (!*q) {
        UsdAttribute extentsHintAttr = geomModel.GetExtentsHintAttr();
        if (extentsHintAttr) {
            *q = UsdAttributeQuery(extentsHintAttr);
        }
    }
    return *q;
}

// -------------------------------------------------------------------------- //
// UsdGeomBBoxCache Public API
// -------------------------------------------------------------------------- //

UsdGeomBBoxCache::UsdGeomBBoxCache(
    UsdTimeCode time, TfTokenVector includedPurposes,
    bool useExtentsHint, bool ignoreVisibility)
    : _time(time)
    , _includedPurposes(includedPurposes)
    , _ctmCache(time)
    , _useExtentsHint(useExtentsHint)
    , _ignoreVisibility(ignoreVisibility)
{
}

UsdGeomBBoxCache::UsdGeomBBoxCache(UsdGeomBBoxCache const &other)
    : _time(other._time)
    , _baseTime(other._baseTime)
    , _includedPurposes(other._includedPurposes)
    , _ctmCache(other._ctmCache)
    , _bboxCache(other._bboxCache)
    , _useExtentsHint(other._useExtentsHint)
    , _ignoreVisibility(other._ignoreVisibility)
{
}

UsdGeomBBoxCache &
UsdGeomBBoxCache::operator=(UsdGeomBBoxCache const &other)
{
    if (this == &other)
        return *this;
    _time = other._time;
    _baseTime = other._baseTime;
    _includedPurposes = other._includedPurposes;
    _ctmCache = other._ctmCache;
    _bboxCache = other._bboxCache;
    _useExtentsHint = other._useExtentsHint;
    _ignoreVisibility = other._ignoreVisibility;
    return *this;
}

GfBBox3d
UsdGeomBBoxCache::ComputeWorldBound(const UsdPrim& prim)
{
    GfBBox3d bbox;

    if (!prim) {
        TF_CODING_ERROR("Invalid prim: %s", UsdDescribe(prim).c_str());
        return bbox;
    }

    _PurposeToBBoxMap bboxes;
    if (!_Resolve(prim, &bboxes))
        return bbox;

    bbox = _GetCombinedBBoxForIncludedPurposes(bboxes);

    GfMatrix4d ctm = _ctmCache.GetLocalToWorldTransform(prim);
    bbox.Transform(ctm);

    return bbox;
}

GfBBox3d
UsdGeomBBoxCache::ComputeRelativeBound(const UsdPrim& prim,
                                       const UsdPrim &relativeToAncestorPrim)
{
    GfBBox3d bbox;
    if (!prim) {
        TF_CODING_ERROR("Invalid prim: %s", UsdDescribe(prim).c_str());
        return bbox;
    }

    _PurposeToBBoxMap bboxes;
    if (!_Resolve(prim, &bboxes))
        return bbox;

    bbox = _GetCombinedBBoxForIncludedPurposes(bboxes);

    GfMatrix4d primCtm = _ctmCache.GetLocalToWorldTransform(prim);
    GfMatrix4d ancestorCtm =
        _ctmCache.GetLocalToWorldTransform(relativeToAncestorPrim);
    GfMatrix4d relativeCtm = primCtm * ancestorCtm.GetInverse();

    bbox.Transform(relativeCtm);

    return bbox;
}

GfBBox3d
UsdGeomBBoxCache::ComputeLocalBound(const UsdPrim& prim)
{
    GfBBox3d bbox;

    if (!prim) {
        TF_CODING_ERROR("Invalid prim: %s", UsdDescribe(prim).c_str());
        return bbox;
    }

    _PurposeToBBoxMap bboxes;
    if (!_Resolve(prim, &bboxes))
        return bbox;

    bbox = _GetCombinedBBoxForIncludedPurposes(bboxes);

    // The value of resetsXformStack does not affect the local bound.
    bool resetsXformStack = false;
    bbox.Transform(_ctmCache.GetLocalTransformation(prim, &resetsXformStack));

    return bbox;
}

GfBBox3d
UsdGeomBBoxCache::ComputeUntransformedBound(const UsdPrim& prim)
{
    GfBBox3d empty;

    if (!prim) {
        TF_CODING_ERROR("Invalid prim: %s", UsdDescribe(prim).c_str());
        return empty;
    }

    _PurposeToBBoxMap bboxes;
    if (!_Resolve(prim, &bboxes))
        return empty;

    return _GetCombinedBBoxForIncludedPurposes(bboxes);
}

template<typename TransformType>
GfBBox3d
UsdGeomBBoxCache::_ComputeBoundWithOverridesHelper(
    const UsdPrim &prim,
    const SdfPathSet &pathsToSkip,
    const TransformType &primOverride,
    const TfHashMap<SdfPath, GfMatrix4d, SdfPath::Hash> &ctmOverrides)
{
    GfBBox3d empty;

    if (!prim) {
        TF_CODING_ERROR("Invalid prim: %s", UsdDescribe(prim).c_str());
        return empty;
    }

    // Use a path table to populate a hash map containing all ancestors of the
    // paths in pathsToSkip.
    SdfPathTable<bool> ancestorsOfPathsToSkip;
    for (const SdfPath &p : pathsToSkip) {
        ancestorsOfPathsToSkip[p.GetParentPath()] = true;
    }

    // Use a path table to populate a hash map containing all ancestors of the
    // paths in ctmOverrides.
    SdfPathTable<bool> ancestorsOfOverrides;
    for (const auto &override : ctmOverrides) {
        ancestorsOfOverrides[override.first.GetParentPath()] = true;
    }

    GfBBox3d result;
    UsdPrimRange range(prim);
    for (auto it = range.begin(); it != range.end(); ++it) {
        const UsdPrim &p = *it;
        const SdfPath &primPath = p.GetPath();

        // If this is one of the paths to be skipped, then prune subtree and
        // continue traversal.
        if (pathsToSkip.count(primPath)) {
            it.PruneChildren();
            continue;
        }

        // If this is an ancestor of a path that's skipped, then we must
        // continue the traversal down to find prims whose bounds can be
        // included.
        if (ancestorsOfPathsToSkip.find(primPath) !=
                ancestorsOfPathsToSkip.end()) {
            continue;
        }

        // Check if any of the descendants of the prim have transform overrides.
        // If yes, we need to continue the traversal down to find prims whose
        // bounds can be included.
        if (ancestorsOfOverrides.find(primPath) != ancestorsOfOverrides.end()) {
            continue;
        }

        // Check to see if any of the ancestors of the prim or the prim itself
        // has an xform override.
        SdfPath pathWithOverride = primPath;
        bool foundAncestorWithOverride = false;
        TfHashMap<SdfPath, GfMatrix4d, SdfPath::Hash>::const_iterator
            overrideIter;
        while (pathWithOverride != prim.GetPath()) {
            overrideIter = ctmOverrides.find(pathWithOverride);
            if (overrideIter != ctmOverrides.end()) {
                // We're only interested in the nearest override since we
                // have the override CTMs in the given prim's space.
                foundAncestorWithOverride = true;
                break;
            }
            pathWithOverride = pathWithOverride.GetParentPath();
        }

        GfBBox3d bbox;
        if (!foundAncestorWithOverride) {
            bbox = ComputeRelativeBound(p, prim);
            _Transform(&bbox, primOverride);
        } else {
            // Compute bound relative to the path for which we know the
            // corrected prim-relative CTM.
            bbox = ComputeRelativeBound(p,
                prim.GetStage()->GetPrimAtPath(overrideIter->first));

            // Apply the override CTM.
            const GfMatrix4d &overrideXform = overrideIter->second;
            bbox.Transform(overrideXform);
        }

        result = GfBBox3d::Combine(result, bbox);
        it.PruneChildren();
    }

    return result;
}

GfBBox3d
UsdGeomBBoxCache::ComputeUntransformedBound(
    const UsdPrim &prim,
    const SdfPathSet &pathsToSkip,
    const TfHashMap<SdfPath, GfMatrix4d, SdfPath::Hash> &ctmOverrides)
{
    return _ComputeBoundWithOverridesHelper(
        prim,
        pathsToSkip,
        _IdentityTransform(),
        ctmOverrides);
}

GfBBox3d
UsdGeomBBoxCache::ComputeWorldBoundWithOverrides(
    const UsdPrim &prim,
    const SdfPathSet &pathsToSkip,
    const GfMatrix4d &primOverride,
    const TfHashMap<SdfPath, GfMatrix4d, SdfPath::Hash> &ctmOverrides)
{
    return _ComputeBoundWithOverridesHelper(
        prim,
        pathsToSkip,
        primOverride,
        ctmOverrides);
}

bool
UsdGeomBBoxCache::_ComputePointInstanceBoundsHelper(
    const UsdGeomPointInstancer &instancer,
    int64_t const *instanceIdBegin,
    size_t numIds,
    GfMatrix4d const &xform,
    GfBBox3d *result)
{
    UsdTimeCode time = GetTime(), baseTime = GetBaseTime();
    VtIntArray protoIndices;
    if (!instancer.GetProtoIndicesAttr().Get(&protoIndices, time)) {
        TF_WARN("%s -- no prototype indices",
                instancer.GetPrim().GetPath().GetText());
        return false;
    }
    VtIntArray const &cprotoIndices = protoIndices;

    const UsdRelationship prototypes = instancer.GetPrototypesRel();
    SdfPathVector protoPaths;
    if (!prototypes.GetTargets(&protoPaths) || protoPaths.empty()) {
        TF_WARN("%s -- no prototypes", instancer.GetPrim().GetPath().GetText());
        return false;
    }

    // verify that all the protoIndices are in bounds.
    for (auto protoIndex: cprotoIndices) {
        if (protoIndex < 0 ||
            static_cast<size_t>(protoIndex) >= protoPaths.size()) {
            TF_WARN("%s -- invalid prototype index: %d. Should be in [0, %zu)",
                    instancer.GetPrim().GetPath().GetText(),
                    protoIndex,
                    protoPaths.size());
            return false;
        }
    }

    // Note that we do NOT apply any masking when computing the instance
    // transforms. This is so that for a particular instance we can determine
    // both its transform and its prototype. Otherwise, the instanceTransforms
    // array would have masked instances culled out and we would lose the
    // mapping to the prototypes.
    // Masked instances will be culled before being applied to the extent below.
    VtMatrix4dArray instanceTransforms;
    if (!instancer.ComputeInstanceTransformsAtTime(
            &instanceTransforms,
            time,
            baseTime,
            UsdGeomPointInstancer::IncludeProtoXform,
            UsdGeomPointInstancer::IgnoreMask)) {
        TF_WARN("%s -- could not compute instance transforms",
                instancer.GetPrim().GetPath().GetText());
        return false;
    }
    VtMatrix4dArray const &cinstanceTransforms = instanceTransforms;

    const UsdStagePtr stage = instancer.GetPrim().GetStage();
    
    for (int64_t const *iid = instanceIdBegin, * const iend = iid + numIds;
         iid != iend; ++iid) {

        const int protoIndex = cprotoIndices[*iid];
        const SdfPath& protoPath = protoPaths[protoIndex];
        const UsdPrim& protoPrim = stage->GetPrimAtPath(protoPath);

        // Get the prototype bounding box and apply the instance transform and
        // the caller's transform.
        GfBBox3d &thisBounds = *result++;
        thisBounds = ComputeUntransformedBound(protoPrim);
        thisBounds.Transform(cinstanceTransforms[*iid] * xform);
    }
    
    return true;
}


bool
UsdGeomBBoxCache::ComputePointInstanceWorldBounds(
    UsdGeomPointInstancer const &instancer,
    int64_t const *instanceIdBegin,
    size_t numIds,
    GfBBox3d *result)
{
    return _ComputePointInstanceBoundsHelper(
        instancer, instanceIdBegin, numIds,
        _ctmCache.GetLocalToWorldTransform(instancer.GetPrim()), result);
}

bool
UsdGeomBBoxCache::ComputePointInstanceRelativeBounds(
    const UsdGeomPointInstancer &instancer,
    int64_t const *instanceIdBegin,
    size_t numIds,
    const UsdPrim &relativeToAncestorPrim,
    GfBBox3d *result)
{
    GfMatrix4d primCtm =
        _ctmCache.GetLocalToWorldTransform(instancer.GetPrim());
    GfMatrix4d ancestorCtm =
        _ctmCache.GetLocalToWorldTransform(relativeToAncestorPrim);
    GfMatrix4d relativeCtm = ancestorCtm.GetInverse() * primCtm;
    return _ComputePointInstanceBoundsHelper(
        instancer, instanceIdBegin, numIds, relativeCtm, result);
}

bool
UsdGeomBBoxCache::ComputePointInstanceLocalBounds(
    const UsdGeomPointInstancer& instancer,
    int64_t const *instanceIdBegin,
    size_t numIds,
    GfBBox3d *result)
{
    // The value of resetsXformStack does not affect the local bound.
    bool resetsXformStack = false;
    return _ComputePointInstanceBoundsHelper(
        instancer, instanceIdBegin, numIds,
        _ctmCache.GetLocalTransformation(
            instancer.GetPrim(), &resetsXformStack), result);
}

bool
UsdGeomBBoxCache::ComputePointInstanceUntransformedBounds(
    const UsdGeomPointInstancer& instancer,
    int64_t const *instanceIdBegin,
    size_t numIds,
    GfBBox3d *result)
{
    return _ComputePointInstanceBoundsHelper(
        instancer, instanceIdBegin, numIds, GfMatrix4d(1), result);
}

void
UsdGeomBBoxCache::Clear()
{
    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] CLEARED\n");
    _ctmCache.Clear();
    _bboxCache.clear();
}

void
UsdGeomBBoxCache::SetIncludedPurposes(const TfTokenVector& includedPurposes)
{
    _includedPurposes = includedPurposes;
}

GfBBox3d
UsdGeomBBoxCache::_GetCombinedBBoxForIncludedPurposes(
    const _PurposeToBBoxMap &bboxes)
{
    GfBBox3d combinedBound;
    for (const TfToken &purpose : _includedPurposes) {
        _PurposeToBBoxMap::const_iterator it = bboxes.find(purpose);
        if (it != bboxes.end()) {
            const GfBBox3d &bboxForPurpose = it->second;
            if (!bboxForPurpose.GetRange().IsEmpty())
                combinedBound = GfBBox3d::Combine(combinedBound,
                                                  bboxForPurpose);
        }
    }
    return combinedBound;
}

void
UsdGeomBBoxCache::SetTime(UsdTimeCode time)
{
    if (time == _time)
        return;

    // If we're switching time into or out of default, then clear all the
    // entries in the cache.
    //
    // This is done because the _IsVarying() check (below) returns false for an
    // attribute when
    // * it has a default value,
    // * it has a single time sample and
    // * its default value is different from the varying time sample.
    //
    // This is an optimization that works well when playing through a shot and
    // computing bboxes sequentially.
    //
    // It should not common to compute bboxes at the default frame. Hence,
    // clearing all values here should not cause any performance issues.
    //
    bool clearUnvarying = false;
    if (_time == UsdTimeCode::Default() || time == UsdTimeCode::Default())
        clearUnvarying = true;
    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] Setting time: %f "
                               " clearUnvarying: %s\n",
                               time.GetValue(),
                               clearUnvarying ? "true": "false");

    for (auto &primAndEntry : _bboxCache) {
        if (clearUnvarying || primAndEntry.second.isVarying) {
            primAndEntry.second.isComplete = false;
            // Clear cached bboxes.
            primAndEntry.second.bboxes.clear();
            TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] invalidating %s "
                                       "for time change\n",
                                       primAndEntry.first.ToString().c_str());
        }
    }
    _time = time;
    _ctmCache.SetTime(_time);
}

// -------------------------------------------------------------------------- //
// UsdGeomBBoxCache Private API
// -------------------------------------------------------------------------- //

bool
UsdGeomBBoxCache::_ShouldIncludePrim(const UsdPrim& prim)
{
    TRACE_FUNCTION();
    
    // If the prim is typeless or has an unknown type, it may have descendants 
    // that are imageable. Hence, we include it in bbox computations.
    if (!prim.IsA<UsdTyped>()) {
        return true;
    }

    // If the prim is typed it can participate in child bound accumulation only 
    // if it is imageable.
    if (!prim.IsA<UsdGeomImageable>()) {
        TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] excluded, not IMAGEABLE type. "
                                   "prim: %s, primType: %s\n",
                                   prim.GetPath().GetText(),
                                   prim.GetTypeName().GetText());

        return false;
    }

    if (!_ignoreVisibility) {
        UsdGeomImageable img(prim);
        TfToken vis;
        if (img.GetVisibilityAttr().Get(&vis, _time)
            && vis == UsdGeomTokens->invisible) {
            TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] excluded for VISIBILITY. "
                                       "prim: %s visibility at time %s: %s\n",
                                       prim.GetPath().GetText(),
                                       TfStringify(_time).c_str(),
                                       vis.GetText());
            return false;
        }
    }

    return true;
}

template <class AttributeOrQuery>
static bool
_IsVaryingImpl(const UsdTimeCode time, const AttributeOrQuery& attr)
{
    // XXX: Copied from UsdImagingDelegate::_TrackVariability.
    // XXX: This logic is highly sensitive to the underlying quantization of
    //      time. Also, the epsilon value (.000001) may become zero for large
    //      time values.
    double lower, upper, queryTime;
    bool hasSamples;
    queryTime = time.IsDefault() ? 1.000001 : time.GetValue() + 0.000001;
    // TODO: migrate this logic into UsdAttribute.
    if (attr.GetBracketingTimeSamples(queryTime, &lower, &upper, &hasSamples)
        && hasSamples)
    {
        // The potential results are:
        //    * Requested time was between two time samples
        //    * Requested time was out of the range of time samples (lesser)
        //    * Requested time was out of the range of time samples (greater)
        //    * There was a time sample exactly at the requested time or
        //      there was exactly one time sample.
        // The following logic determines which of these states we are in.

        // Between samples?
        if (lower != upper) {
            return true;
        }

        // Out of range (lower) or exactly on a time sample?
        attr.GetBracketingTimeSamples(lower+.000001,
                                      &lower, &upper, &hasSamples);
        if (lower != upper) {
            return true;
        }

        // Out of range (greater)?
        attr.GetBracketingTimeSamples(lower-.000001,
                                      &lower, &upper, &hasSamples);
        if (lower != upper) {
            return true;
        }
        // Really only one time sample --> not varying for our purposes
    }
    return false;
}

bool
UsdGeomBBoxCache::_IsVarying(const UsdAttribute& attr)
{
    return _IsVaryingImpl(_time, attr);
}

bool
UsdGeomBBoxCache::_IsVarying(const UsdAttributeQuery& query)
{
    return _IsVaryingImpl(_time, query);
}

// Returns true if the given prim is a component or a subcomponent.
static
bool
_IsComponentOrSubComponent(const UsdPrim &prim)
{
    UsdModelAPI model(prim);
    TfToken kind;
    if (!model.GetKind(&kind))
        return false;


    return KindRegistry::IsA(kind, KindTokens->component) ||
        KindRegistry::IsA(kind, KindTokens->subcomponent);
}

// Returns the nearest ancestor prim that's a component or a subcomponent, or
// the stage's pseudoRoot if none are found. For the purpose of computing
// bounding boxes, subcomponents as treated similar to components, i.e. child
// bounds are accumulated in subcomponent-space for prims that are underneath
// a subcomponent.
//
static
UsdPrim
_GetNearestComponent(const UsdPrim &prim)
{
    UsdPrim modelPrim = prim;
    while (modelPrim) {
        if (_IsComponentOrSubComponent(modelPrim))
            return modelPrim;

        modelPrim = modelPrim.GetParent();
    }

    // If we get here, it means we did not find a model or a subcomponent at or
    // above the given prim. Hence, return the stage's pseudoRoot.
    return prim.GetStage()->GetPseudoRoot();
}

template <bool IsRecursive>
void
UsdGeomBBoxCache::_ComputePurposeInfo(
    _Entry *entry, const _PrimContext &primContext)
{
    if (entry->purposeInfo) {
        return;
    }

    const UsdPrim &prim = primContext.prim;

    // Special case for prototype prims. Prototypes don't actually have their
    // own purpose attribute. The prims that instance the prototype will provide
    // its purpose. It's important that we apply the instancing prim's purpose
    // to this prototype prim context so that the prototype's children can
    // properly inherit the instancing prim's purpose if needed. Note that this
    // only applies if the instancing prim provides a purpose that is
    // inheritable.
    if (prim.IsPrototype()) {
        if (primContext.instanceInheritablePurpose.IsEmpty()) {
            // If the instancing prim's purpose is not inheritable, this 
            // prototype prim context won't provide an inheritable purpose to
            // its children either.
            entry->purposeInfo = UsdGeomImageable::PurposeInfo(
                UsdGeomTokens->default_, false);
        } else {
            // Otherwise this prototype can provide the instancing prim's 
            // inheritable pupose to its children.
            entry->purposeInfo = UsdGeomImageable::PurposeInfo(
                primContext.instanceInheritablePurpose, true);
        }
    } else {
        UsdGeomImageable img(prim);
        UsdPrim parentPrim = prim.GetParent();
        if (parentPrim && parentPrim.GetPath() != SdfPath::AbsoluteRootPath()) {
            // Try and get the parent prim's purpose first. If we find it in the
            // cache, we can compute this prim's purpose efficiently by avoiding
            // the n^2 recursion which results from using the 
            // UsdGeomImageable::ComputePurpose() API directly.
            
            // If this prim is in a prototype then its parent prim will be too. 
            // The parent prim's context will have the same inheritable purpose
            // from the instance as this prim context does.
            _PrimContext parentPrimContext(
                parentPrim, primContext.instanceInheritablePurpose);
            _Entry *parentEntry = _FindEntry(parentPrimContext);
            if (parentEntry) {
                if (IsRecursive) {
                    // If this is recursive make sure the parent's purpose is 
                    // computed and cached first.
                    _ComputePurposeInfo<IsRecursive>(
                        parentEntry, parentPrimContext);
                    entry->purposeInfo = img.ComputePurposeInfo(
                        parentEntry->purposeInfo);
                    return;
                } else {
                    // Not recursive. just check that the parent purpose has 
                    // been computed. 
                    if (parentEntry->purposeInfo) {
                        entry->purposeInfo = img.ComputePurposeInfo(
                            parentEntry->purposeInfo);
                        return;
                    }
                    TF_DEBUG(USDGEOM_BBOX).Msg(
                        "[BBox Cache] Computing purpose for <%s> before purpose"
                        "of parent <%s> is cached\n",
                        primContext.ToString().c_str(),
                        parentPrimContext.ToString().c_str());
                }
            }
        } 
        TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] Computing purpose without "
                                   "cached parent for <%s>\n",
                                   primContext.ToString().c_str());
        entry->purposeInfo = img.ComputePurposeInfo();
    }
}

// Helper to determine if we should use extents hints for \p prim.
bool UsdGeomBBoxCache::
_UseExtentsHintForPrim(UsdPrim const &prim) const
{
    return _useExtentsHint && prim.IsModel() &&
        prim.GetPath() != SdfPath::AbsoluteRootPath();
}

bool
UsdGeomBBoxCache::_ShouldPruneChildren(const UsdPrim &prim,
                                       UsdGeomBBoxCache::_Entry *entry)
{
    // If the entry is already complete, we don't need to try to initialize it.
    if (entry->isComplete) {
        return true;
    }

    // Check if prim is a UsdGeomBoundable. Boundables should always provide
    // their own extent and do not require participation from descendants.
    if (prim.IsA<UsdGeomBoundable>()) {
        return true;
    }
        
    if (!_UseExtentsHintForPrim(prim)) {
        return false;
    }

    UsdAttribute extentsHintAttr = UsdGeomModelAPI(prim).GetExtentsHintAttr();
    VtVec3fArray extentsHint;
    return (extentsHintAttr
            && extentsHintAttr.Get(&extentsHint, _time)
            && extentsHint.size() >= 2);
}

UsdGeomBBoxCache::_Entry*
UsdGeomBBoxCache::_FindOrCreateEntriesForPrim(
    const _PrimContext& primContext,
    std::vector<_PrimContext> *prototypePrimContexts)
{
    // Add an entry for the prim to the cache and if the bound is already in 
    // the cache, return it.
    // 
    // Note that this means we always have an entry for the given prim,
    // even if that prim does not pass the predicate given to the tree
    // iterator below (e.g., the prim is a class).
    _Entry* entry = _InsertEntry(primContext);
    if (entry && entry->isComplete) {
        const _PurposeToBBoxMap& bboxes = entry->bboxes;
        TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] hit: %s %s\n",
            primContext.ToString().c_str(),
            TfStringify(_GetCombinedBBoxForIncludedPurposes(bboxes)).c_str());
        return entry;
    }
    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] miss: %s\n",
                                primContext.ToString().c_str());

    // isIncluded only gets cached in the multi-threaded path for child prims,
    // make sure the prim we're querying has the correct flag cached also. We
    // can't do this in _ResolvePrim because we need to compute the flag for 
    // children before recursing upon them.
    entry->isIncluded = _ShouldIncludePrim(primContext.prim);

    // Pre-populate all cache entries, note that some entries may already exist.
    // Note also we do not exclude unloaded prims - we want them because they
    // may have authored extentsHints we can use; thus we can have bboxes in
    // model-hierarchy-only.

    TfHashSet<_PrimContext, _PrimContextHash> seenPrototypePrimContexts;

    UsdPrimRange range(primContext.prim, 
        (UsdPrimIsActive && UsdPrimIsDefined && !UsdPrimIsAbstract));
    for (auto it = range.begin(); it != range.end(); ++it) {
        _PrimContext cachePrimContext(
            *it, primContext.instanceInheritablePurpose);
        _Entry *cacheEntry = _InsertEntry(cachePrimContext);
        
        if (_ShouldPruneChildren(*it, cacheEntry)) {
            // The entry already exists and is complete, we don't need
            // the child entries for this query.
            it.PruneChildren();
        }
        else if (it->IsInstance()) {
            // This prim is an instance without an extent or hint, so we need
            // to compute bounding boxes for the prototype prims.
            const UsdPrim prototype = it->GetPrototype();
            // We typically compute the purpose for prims later in _ResolvePrim,
            // but for an instance prim, we need to compute the purpose for this 
            // prim context now so that we can associate this instance's 
            // inheritable purpose with the prototype. 
            // 
            // Note that we recursively cache the computed purposes of all 
            // cached ancestors of the prim here as we won't have necessarily
            // computed them before reaching this prim. It should be safe to
            // cache ancestors recursively as this code is not used in a 
            // multithreaded context.
            _ComputePurposeInfo<true>(cacheEntry, cachePrimContext);
            _PrimContext prototypePrimContext(
                prototype, cacheEntry->purposeInfo.GetInheritablePurpose());
            if (seenPrototypePrimContexts.insert(prototypePrimContext).second) {
                prototypePrimContexts->push_back(prototypePrimContext);
            }
            it.PruneChildren();
        }
    }

    return entry;
}

bool
UsdGeomBBoxCache::_Resolve(
    const UsdPrim& prim,
    UsdGeomBBoxCache::_PurposeToBBoxMap *bboxes)
{
    TRACE_FUNCTION();
    // NOTE: Bounds are cached in local space, but computed in world space.

    // Drop the GIL here if we have it before we spawn parallel tasks, since
    // resolving properties on prims in worker threads may invoke plugin code
    // that needs the GIL.
    TF_PY_ALLOW_THREADS_IN_SCOPE();

    // If the bound is in the cache, return it.
    std::vector<_PrimContext> prototypePrimContexts;
    _PrimContext primContext(prim);
    _Entry *entry = _FindOrCreateEntriesForPrim(primContext, 
                                                &prototypePrimContexts);
    if (entry && entry->isComplete) {
        *bboxes = entry->bboxes;
        return (!bboxes->empty());
    }

    WorkWithScopedParallelism([&]() {
                        
            // Resolve all prototype prims first to avoid having to synchronize
            // tasks that depend on the same prototype.
            if (!prototypePrimContexts.empty()) {
                _PrototypeBBoxResolver bboxesForPrototypes(this);
                bboxesForPrototypes.Resolve(prototypePrimContexts);
            }
            
            // XXX: This swapping out is dubious... see XXX below.
            _ThreadXformCache xfCaches;
            xfCaches.local().Swap(_ctmCache);
            
            // Find the nearest ancestor prim that's a model or a subcomponent.
            UsdPrim modelPrim = _GetNearestComponent(prim);
            GfMatrix4d inverseComponentCtm = _ctmCache.GetLocalToWorldTransform(
                modelPrim).GetInverse();
            
            _dispatcher.Run(
                _BBoxTask(primContext, inverseComponentCtm, this, &xfCaches));
            _dispatcher.Wait();

            // We save the result of one of the caches, but it might be
            // interesting to merge them all here at some point.  XXX: Is this
            // valid?  This only makes sense if we're *100% certain* that
            // rootTask above runs in this thread.  If it's picked up by another
            // worker it won't populate the local xfCaches we're swapping with.
            xfCaches.local().Swap(_ctmCache);
        });
    
    // Note: the map may contain unresolved entries, but future queries will
    // populate them.
    
    // If the bound is in the cache, return it.
    entry = _FindEntry(primContext);
    if (entry == nullptr) {
        return false;
    }
    *bboxes = entry->bboxes;
    return (!bboxes->empty());
}

bool
UsdGeomBBoxCache::_GetBBoxFromExtentsHint(
    const UsdGeomModelAPI &geomModel,
    const UsdAttributeQuery &extentsHintQuery,
    _PurposeToBBoxMap *bboxes)
{
    VtVec3fArray extents;

    if (!extentsHintQuery || !extentsHintQuery.Get(&extents, _time)){
        if (TfDebug::IsEnabled(USDGEOM_BBOX) &&
            !geomModel.GetPrim().IsLoaded()){
            TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] MISSING extentsHint for "
                                       "UNLOADED model %s.\n",
                                       geomModel.GetPrim().GetPath()
                                       .GetString().c_str());
        }

        return false;
    }

    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] Found cached extentsHint for "
        "model %s.\n", geomModel.GetPrim().GetPath().GetString().c_str());

    const TfTokenVector &purposeTokens =
        UsdGeomImageable::GetOrderedPurposeTokens();

    for(size_t i = 0; i < purposeTokens.size(); ++i) {
        size_t idx = i*2;
        // If extents are not available for the value of purpose, it
        // implies that the rest of the bounds are empty.
        // Hence, we can break.
        if ((idx + 2) > extents.size())
            break;

        (*bboxes)[purposeTokens[i]] =
            GfBBox3d( GfRange3d(extents[idx], extents[idx+1]) );
    }

    return true;
}

void
UsdGeomBBoxCache::_ResolvePrim(const _BBoxTask* task,
                               const _PrimContext &primContext,
                               const GfMatrix4d &inverseComponentCtm)
{
    TRACE_FUNCTION();
    // NOTE: Bounds are cached in local space, but computed in world space.

    // If the bound is in the cache, return it.
    _Entry* entry = _FindEntry(primContext);
    if (!TF_VERIFY(entry != NULL))
        return;

    _PurposeToBBoxMap *bboxes = &entry->bboxes;
    if (entry->isComplete) {
        TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] Dependent cache hit: "
            "%s %s\n", primContext.ToString().c_str(),
            TfStringify(_GetCombinedBBoxForIncludedPurposes(*bboxes)).c_str());
        return;
    }
    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] Dependent cache miss: %s\n",
                                primContext.ToString().c_str());

    // Initially the bboxes hash map is empty, which implies empty bounds.

    UsdGeomXformCache &xfCache = task->GetXformCaches()->local();

    // Setting the time redundantly will be a no-op;
    xfCache.SetTime(_time);

    // Compute the purpose for the entry. Note that we do not do this 
    // recursively because _ResolvePrim can be called multithreaded across
    // siblings and any parent's purposes should already be cached before this 
    // _ResolvePrim is called. 
    _ComputePurposeInfo<false>(entry, primContext);

    // Check if the prim is a model and has extentsHint
    const UsdPrim &prim = primContext.prim;
    const bool useExtentsHintForPrim = _UseExtentsHintForPrim(prim);

    std::shared_ptr<UsdAttributeQuery[]> &queries = entry->queries;
    if (!queries) {
        // If this cache doesn't use extents hints, we don't need the
        // corresponding query.
        const size_t numQueries =
            (useExtentsHintForPrim ? NumQueries : NumQueries - 1);
        queries.reset(new UsdAttributeQuery[numQueries]);
    }

    if (useExtentsHintForPrim) {
        UsdGeomModelAPI geomModel(prim);
        const UsdAttributeQuery& extentsHintQuery =
            _GetOrCreateExtentsHintQuery(geomModel, &queries[ExtentsHint]);

        if (_GetBBoxFromExtentsHint(geomModel, extentsHintQuery, bboxes)) {
            entry->isComplete = true;

            // XXX: Do we only need to be doing the following in
            //      the non-varying case, similar to below?
            entry->isVarying = _IsVarying(extentsHintQuery);
            entry->isIncluded = _ShouldIncludePrim(prim);
            return;
        }
    }

    // We only check when isVarying is false, since when an entry doesn't
    // vary over time, this code will only be executed once. If an entry has
    // been marked as varying, we need not check if it's varying again.
    // This relies on entries being initialized with isVarying=false.
    if (!entry->isVarying) {
        // Note that child variability is also accumulated into
        // entry->isVarying (below).

        UsdAttributeQuery visQuery;
        if (!_ignoreVisibility) {
            _GetOrCreateVisibilityQuery(prim, &visQuery);
        }
        
        const UsdAttributeQuery& extentQuery =
            _GetOrCreateExtentQuery(prim, &queries[Extent]);

        UsdGeomXformable xformable(prim);
        entry->isVarying =
            (xformable && xformable.TransformMightBeTimeVarying())
            || (extentQuery && _IsVarying(extentQuery))
            || (visQuery && _IsVarying(visQuery));
    }

    // Leaf gprims and boundable intermediate prims.
    //
    // When boundable prims have an authored extent, it is expected to
    // incorporate the extent of all children, which are pruned from further
    // traversal.
    GfRange3d myRange;

    // Attempt to resolve a boundable prim's extent. If no extent is authored,
    // we attempt to create it for usdGeomPointBased and child classes. If
    // it cannot be created or found, the user is notified of an incorrect prim.
    if (UsdGeomBoundable boundableObj = UsdGeomBoundable(prim)) {
        // UsdGeomBoundable::ComputeExtent checks to see if extent attr has an
        // authored value and sets extent to that, if not it computes extent
        // using intrinsic geometric parameters, provided ComputeExtentFunction
        // is registered for this boundableObj.  If we successfully obtain an
        // extent, create BBox for purpose.
        VtVec3fArray extent;
        if (boundableObj.ComputeExtent(_time, &extent)) {
            GfBBox3d &bboxForPurpose = (*bboxes)[entry->purposeInfo.purpose];
            const VtVec3fArray &extentConst = extent;
            bboxForPurpose.SetRange(GfRange3d(extentConst[0], extentConst[1]));
        }
    }
    else {
        // This is not a boundable, so descend to children.

        // --
        // NOTE: bbox is currently in its local space, the space in which
        // we want to cache it.  If we need to merge in child bounds below,
        // though, we will need to temporarily transform it into component space.
        // --
        bool bboxInComponentSpace = false;

        // This will be computed below if the prim has children with bounds.
        GfMatrix4d localToComponentXform(1.0);
        
        // Accumulate child bounds:
        //
        //  1) Filter and queue up the children to be processed.
        //  2) Spawn new child tasks and wait for them to complete.
        //  3) Accumulate the results into this cache entry.
        //
        
        // Compute the enclosing model's (or subcomponent's) inverse CTM.
        // This will be used to compute the child bounds in model-space.
        const GfMatrix4d &inverseEnclosingComponentCtm =
            _IsComponentOrSubComponent(prim) ?
            xfCache.GetLocalToWorldTransform(prim).GetInverse() :
            inverseComponentCtm;
        
        std::vector<std::pair<_PrimContext, _BBoxTask> > included;
        // See comment in _Resolve about unloaded prims
        UsdPrimSiblingRange children;
        TfToken childInheritableInstancePurpose;
        
        const bool primIsInstance = prim.IsInstance();
        if (primIsInstance) {
            const UsdPrim prototype = prim.GetPrototype();
            children = prototype.GetFilteredChildren(
                UsdPrimIsActive && UsdPrimIsDefined && !UsdPrimIsAbstract);
            // Since we're using the prototype's children, we need to make sure
            // we propagate this instance's inheritable purpose to the
            // prototype's children so they inherit the correct purpose for this
            // instance if needed.
            childInheritableInstancePurpose = 
                entry->purposeInfo.GetInheritablePurpose();
        }
        else {
            children = prim.GetFilteredChildren(
                UsdPrimIsActive && UsdPrimIsDefined && !UsdPrimIsAbstract);
            // Otherwise for standard children that are not across an instance
            // boundary, pass this prim's inheritable purpose along to its
            // children.  
            // XXX: It's worth noting that if a child of a prototype
            // has a purpose opinion, then that child (and its descendants) will
            // have the same computed purpose regardless of the inheritable
            // instance purpose of the prototype. Thus it's technically
            // redundant to store multiple entries for these children per
            // instance purpose. But the trade off for this redundancy means
            // that we don't have to worry about different prototypes or
            // siblings sharing child entries in the cache which would
            // complicate the multithreaded way we resolve bboxes for prototype
            // and sibling prims. This may be something to re-evaluate in the
            // future.
            childInheritableInstancePurpose = 
                primContext.instanceInheritablePurpose;
        }

        for(const UsdPrim &childPrim : children) {

            _PrimContext childPrimContext(
                childPrim, childInheritableInstancePurpose);

            // Skip creating bbox tasks for excluded children.
            //
            // We must do this check here on the children, because when an
            // invisible prim is queried explicitly, we want to return the bound
            // to the client, even if that prim's bbox is not included in the
            // parent bound.
            _Entry* childEntry = _FindEntry(childPrimContext);
            if (!TF_VERIFY(childEntry, "Could not find prim <%s>"
                "in the bboxCache.", childPrimContext.ToString().c_str())) {
                continue;
            }

            // If we're about to process the child for the first time, we must
            // populate isIncluded.
            if (!childEntry->isComplete)
                childEntry->isIncluded = _ShouldIncludePrim(childPrim);

            // We're now confident that the cached flag is correct.
            if (!_ignoreVisibility && !childEntry->isIncluded) {
                // If the child prim is excluded, mark the parent as varying
                // if the child is imageable and its visibility is varying.
                // This will ensure that the parent entry gets dirtied when
                // the child becomes visible.
                UsdGeomImageable img (childPrim);
                if (img)
                    entry->isVarying |= _IsVarying(img.GetVisibilityAttr());
                continue;
            }

            // Queue up the child to be processed.
            if (primIsInstance) {
                // If the prim we're processing is an instance, all of its
                // child prims will come from its prototype prim. The bboxes
                // for these prims should already have been computed in
                // _Resolve, so we don't need to schedule an additional task.
                included.push_back(std::make_pair(childPrimContext, 
                                                  _BBoxTask()));
            }
            else {
                included.emplace_back(childPrimContext,
                                      _BBoxTask(childPrimContext,
                                                inverseEnclosingComponentCtm,
                                                this, task->GetXformCaches()));
            }
        }

        // Spawn and wait.
        //
        // Warning: calling spawn() before set_ref_count results in undefined
        // behavior.
        //
        // All the child bboxTasks will be NULL if the prim is an instance.
        //
        if (!primIsInstance) {
            WorkWithScopedParallelism([&]() {
                    WorkDispatcher wd;
                    for(auto &childAndTask : included) {
                        if (childAndTask.second) {
                            wd.Run(childAndTask.second);
                        }
                    }
                });

            // We may have switched threads, grab the thread-local xfCache
            // again.
            xfCache = task->GetXformCaches()->local();
            xfCache.SetTime(_time);
        }

        // Accumulate child results.
        // Process the child bounding boxes, accumulating their variability and
        // volume into this cache entry.
        for (const auto &childAndTask : included) {
            // The child's bbox is returned in local space, so we must convert
            // it to model space to be compatible with the current bbox.
            _PrimContext const &childPrimContext = childAndTask.first;
            UsdPrim const& childPrim = childPrimContext.prim;

            const _Entry* childEntry = _FindEntry(childPrimContext);
            if (!TF_VERIFY(childEntry->isComplete))
                continue;

            // Accumulate child variability.
            entry->isVarying |= childEntry->isVarying;

            // Accumulate child bounds.
            if (!childEntry->bboxes.empty()) {
                if (!bboxInComponentSpace){
                    // Put the local extent into "baked" component space, i.e.
                    // a bbox with identity transform
                    localToComponentXform =
                        xfCache.GetLocalToWorldTransform(prim) *
                        inverseEnclosingComponentCtm;

                    for (auto &purposeAndBBox : *bboxes) {
                        GfBBox3d &bbox = purposeAndBBox.second;
                        bbox.SetMatrix(localToComponentXform);
                        bbox = GfBBox3d(bbox.ComputeAlignedRange());
                    }

                    bboxInComponentSpace = true;
                }

                _PurposeToBBoxMap childBBoxes = childEntry->bboxes;

                GfMatrix4d childLocalToComponentXform;
                if (primIsInstance) {
                    bool resetsXf = false;
                    childLocalToComponentXform =
                        xfCache.GetLocalTransformation(childPrim, &resetsXf) *
                        localToComponentXform;
                }
                else {
                    childLocalToComponentXform =
                        xfCache.GetLocalToWorldTransform(childPrim) *
                        inverseEnclosingComponentCtm;
                }

                // Convert the resolved BBox to component space.
                for (auto &purposeAndBBox : childBBoxes) {
                    const TfToken &purposeToken = purposeAndBBox.first;

                    GfBBox3d &childBBox = purposeAndBBox.second;
                    childBBox.Transform(childLocalToComponentXform);

                    // Since the range is in component space and the matrix is
                    // identity, we can union in component space.
                    GfBBox3d &bbox = (*bboxes)[purposeToken];
                    bbox.SetRange(GfRange3d(bbox.GetRange()).UnionWith(
                                  childBBox.ComputeAlignedRange()));
                }
            }
        }
        // All prims must be cached in local space: convert bbox from component
        // to local space.
        if (bboxInComponentSpace) {
            // When children are accumulated, the bbox range is in component
            // space, so we must apply the inverse component-space transform
            // (component-to-local) to move it to local space.
            GfMatrix4d componentToLocalXform =
                localToComponentXform.GetInverse();
            for (auto &purposeAndBBox : *bboxes) {
                GfBBox3d &bbox = purposeAndBBox.second;
                bbox.SetMatrix(componentToLocalXform);
            }
        }
    }

    // --
    // NOTE: bbox is now in local space, either via the matrix or range.
    // --

    // Performance note: we could leverage the fact that the bound is initially
    // computed in world space and avoid an extra transformation for recursive
    // calls, however that optimization was not significant in early tests.

    // Mark as cached and return.
    entry->isComplete = true;
    TF_DEBUG(USDGEOM_BBOX).Msg("[BBox Cache] resolved value: %s %s "
        "(varying: %s)\n",
        primContext.ToString().c_str(),
        TfStringify(_GetCombinedBBoxForIncludedPurposes(*bboxes)).c_str(),
        entry->isVarying ? "true" : "false");
}

std::string 
UsdGeomBBoxCache::_PrimContext::ToString() const {
    if (instanceInheritablePurpose.IsEmpty()) {
        return prim.GetPath().GetString();
    } else {
        return TfStringPrintf("[%s]%s", 
                              instanceInheritablePurpose.GetText(), 
                              prim.GetPath().GetText());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
