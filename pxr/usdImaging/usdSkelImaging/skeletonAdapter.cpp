//
// Copyright 2018 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdSkelImaging/skeletonAdapter.h"

#include "pxr/usdImaging/usdSkelImaging/dataSourceSkeletonPrim.h"
#include "pxr/usdImaging/usdSkelImaging/extComputations.h"
#include "pxr/usdImaging/usdSkelImaging/package.h"
#include "pxr/usdImaging/usdSkelImaging/tokens.h"
#include "pxr/usdImaging/usdSkelImaging/utils.h"

#include "pxr/usdImaging/usdImaging/debugCodes.h"
#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/gprimAdapter.h"
#include "pxr/usdImaging/usdImaging/indexProxy.h"
#include "pxr/usdImaging/usdImaging/tokens.h"

#include "pxr/usd/usdGeom/boundable.h"
#include "pxr/usd/usdGeom/pointBased.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "pxr/usd/usdSkel/animMapper.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/tokens.h"
#include "pxr/usd/usdSkel/utils.h"

#include "pxr/imaging/hio/glslfx.h"

#include "pxr/imaging/hd/extComputation.h" // dirtyBits
#include "pxr/imaging/hd/extComputationContext.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/perfLog.h"

#include "pxr/base/gf/quaternion.h"
#include "pxr/base/gf/dualQuatf.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // computation inputs
    (skinningMethod)

    // gpu compute kernels
    (skinPointsLBSKernel)
    (skinPointsDQSKernel)
    (skinPointsSimpleKernel)

    // skel primvar names
    ((skelJointIndices,  "skel:jointIndices"))
    ((skelJointWeights,   "skel:jointWeights"))
    ((skelSkinningMethod, "skel:skinningMethod"))
    ((skelGeomBindXform, "skel:geomBindTransform"))

);

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdSkelImagingSkeletonAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

// XXX: Temporary way to force CPU comps for Storm.
// Ideally, this is a render delegate opinion, or should be handled in Hydra
// ExtComputation.
static bool
_IsEnabledCPUComputations()
{
    static bool enabled = TfGetEnvSetting(USDSKELIMAGING_FORCE_CPU_COMPUTE);
    return enabled;
}

UsdSkelImagingSkeletonAdapter::~UsdSkelImagingSkeletonAdapter() = default;

bool
UsdSkelImagingSkeletonAdapter::IsSupported(
    const UsdImagingIndexProxy* index) const
{
    return index->IsRprimTypeSupported(HdPrimTypeTokens->mesh);
}


SdfPath
UsdSkelImagingSkeletonAdapter::Populate(
    const UsdPrim& prim,
    UsdImagingIndexProxy* index,
    const UsdImagingInstancerContext* instancerContext)
{
    // We expect Populate to be called ONLY on a UsdSkelSkeleton prim.
    if(!TF_VERIFY(prim.IsA<UsdSkelSkeleton>())) {
        return SdfPath();
    }
    if(instancerContext != nullptr) {
        // TODO: support UsdSkel with instancing
        return SdfPath();
    }

    SdfPath const& skelPath = prim.GetPath();
    // Populate may be called via Resync processing for skinned prims, in which
    // case we shouldn't have to repopulate the bone mesh.
    if (_skelDataCache.find(skelPath) == _skelDataCache.end()) {
        // New skeleton prim
        // - Add bone mesh cache entry for the skeleton
        auto skelData = std::make_shared<_SkelData>();
        skelData->skelQuery = _skelCache.GetSkelQuery(UsdSkelSkeleton(prim));
        _skelDataCache[skelPath] = skelData;

        // Insert mesh prim to visualize the bone mesh for the skeleton.
        // Note: This uses the "rest" pose of the skeleton.
        // Also, since the bone mesh isn't backed by the UsdStage, we register 
        // the skeleton prim on its behalf.
        index->InsertRprim(HdPrimTypeTokens->mesh, prim.GetPath(),
                           prim, shared_from_this());
    }

    // Insert computations for each skinned prim targeted by this
    // skeleton. We know this because the SkelRootAdapter populated all the
    // "skeleton -> skinned prims" during Populate.
    // Note: The SkeletonAdapter registers itself as "responsible" for
    // the computation, and we pass the skinnedPrim as the usdPrim,
    // argument and _not_ the skel prim.
    const auto bindingIt = _skelBindingMap.find(skelPath);
    
    if (bindingIt != _skelBindingMap.end()) {
        UsdSkelBinding const& binding = bindingIt->second;
        _SkelData* skelData = _GetSkelData(skelPath);

        // Find the path to the skel root from the first skinning target
        // (all bindings reference the same SkelRoot).
        // TODO: Would be more efficient to have the SkelRootAdapter directly
        // inform us of this relationship.
        SdfPath skelRootPath;
        if (!binding.GetSkinningTargets().empty()) {
            if (const UsdSkelRoot skelRoot =
                UsdSkelRoot::Find(
                    binding.GetSkinningTargets().front().GetPrim())) {
                skelRootPath = skelRoot.GetPrim().GetPath();
                skelData->skelRootPaths.insert(skelRootPath);
            }
        }

        for (UsdSkelSkinningQuery const& query : binding.GetSkinningTargets()) {
            
            // Insert two computations ...
            UsdPrim const& skinnedPrim = query.GetPrim();
            if (!skinnedPrim) {
                continue;
            }

            SdfPath skinnedPrimPath = ResolveCachePath(
                skinnedPrim.GetPath(), instancerContext);

            _skinnedPrimDataCache[skinnedPrimPath] =
                _SkinnedPrimData(skelPath, skelData->skelQuery,
                                 query, skelRootPath);

            // 1. A skinning computation that computes the skinned points, and
            SdfPath compPath = _GetSkinningComputationPath(skinnedPrimPath);

            TF_DEBUG(USDIMAGING_COMPUTATIONS).Msg(
                "[SkeletonAdapter::Populate] Inserting "
                "computation %s for skinned prim %s\n",
                compPath.GetText(), skinnedPrimPath.GetText());

            index->InsertSprim(
                    HdPrimTypeTokens->extComputation,
                    compPath,
                    skinnedPrim,
                    shared_from_this());

            // 2. An aggregator computation that aggregates inputs that
            //    typically don't vary with time. This is necessary because
            //    Hydra ExtComputations does not track dirtiness per input.
            //    The aggregator computation is especially useful for GPU
            //    compute and avoids re-uploading inputs that don't vary to the
            //    GPU.
            SdfPath aggrCompPath =
                _GetSkinningInputAggregatorComputationPath(skinnedPrimPath);

            TF_DEBUG(USDIMAGING_COMPUTATIONS).Msg(
                "[SkeletonAdapter::Populate] Inserting "
                "aggregator computation %s for skinned prim %s\n",
                aggrCompPath.GetText(), skinnedPrimPath.GetText());

            index->InsertSprim(
                HdPrimTypeTokens->extComputation,
                aggrCompPath,
                skinnedPrim,
                shared_from_this());
        }
    } else {
        // Do nothing. This isn't an error. We can have skeletons that
        // don't affect any skinned prims. One example is using variants.
    }

    return prim.GetPath();
}

// ---------------------------------------------------------------------- //
/// Parallel Setup and Resolve
// ---------------------------------------------------------------------- //

void
UsdSkelImagingSkeletonAdapter::TrackVariability(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // WARNING: This method is executed from multiple threads, the value cache
    // has been carefully pre-populated to avoid mutating the underlying
    // container during update.

    if (_IsCallbackForSkeleton(prim)) {
        _TrackBoneMeshVariability(  prim,
                                    cachePath,
                                    timeVaryingBits,
                                    instancerContext);
        return;
    }

    if (_IsSkinnedPrimPath(cachePath)) {
        _TrackSkinnedPrimVariability(   prim,
                                        cachePath,
                                        timeVaryingBits,
                                        instancerContext);
        return;
    }

    if (_IsSkinningComputationPath(cachePath)) {
         _TrackSkinningComputationVariability(  prim,
                                                cachePath,
                                                timeVaryingBits,
                                                instancerContext);
        return;
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        // Nothing to do; these are not expected to be time varying.
        // XXX: Check if inputs from the skinned prim are time-varying and
        // issue a warning.
        return;
    }

    TF_CODING_ERROR("UsdSkelImagingSkeletonAdapter::TrackVariability : Received"
                    " unknown prim %s ", cachePath.GetText());
}


void
UsdSkelImagingSkeletonAdapter::UpdateForTime(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{

    // UpdateForTime will be called on behalf of the hydra computations since
    // the skeleton adapter is registered against them. However any value that
    // needs to be pulled from the computation prims can happen via the
    // respective prim adapter methods that are invoked when the scene delegate
    // getters for the ExtComputation are called.

    // Note that UpdateForTime will still get called for the hydra computations
    // since this adapter is registered for them. However, we don't do anything
    // for them here and instead handle all pulls from the computation prims
    // directly in various GetExtComputation* calls on the adapter (called 
    // from the scene delegate).
    if (_IsSkinningComputationPath(cachePath)) {
        return;
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        return;
    }

    if (_IsCallbackForSkeleton(prim)) {
        return _UpdateBoneMeshForTime(  prim,
                                        cachePath,
                                        time,
                                        requestedBits,
                                        instancerContext);
    }

    if (_IsSkinnedPrimPath(cachePath)) {
        return _UpdateSkinnedPrimForTime(   prim,
                                            cachePath,
                                            time,
                                            requestedBits,
                                            instancerContext);
    }

    TF_CODING_ERROR("UsdSkelImagingSkeletonAdapter::UpdateForTime : Received"
                    " unknown prim %s ", cachePath.GetText());
}

// ---------------------------------------------------------------------- //
/// Change Processing
// ---------------------------------------------------------------------- //

HdDirtyBits
UsdSkelImagingSkeletonAdapter::ProcessPropertyChange(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    const TfToken& propertyName)
{
    if (_IsCallbackForSkeleton(prim)) {
        if (propertyName == UsdGeomTokens->visibility) {
            return HdChangeTracker::DirtyVisibility;
        }
        if (propertyName == UsdGeomTokens->purpose) {
            return HdChangeTracker::DirtyRenderTag;
        }
        if (propertyName == UsdGeomTokens->extent) {
            return HdChangeTracker::DirtyExtent;
        }
        if (UsdGeomXformable::IsTransformationAffectedByAttrNamed(
                propertyName)) {
            return HdChangeTracker::DirtyTransform;
        }

        // XXX: Changes to properties on the skeleton (e.g., the joint 
        // hierarchy) should propagate to the computations.
        // We don't have access to the UsdImagingIndexProxy here, so we cannot
        // use the property name to propagate dirtyness.

        // Returning AllDirty triggers a resync of the skeleton.
        // See ProcessPrimResync(..)
        return HdChangeTracker::AllDirty;
    }
    
    if (_IsSkinnedPrimPath(cachePath)) {

        // Since The SkeletonAdapter hijacks skinned prims (see SkelRootAdapter),
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        HdDirtyBits dirtyBits =
            adapter->ProcessPropertyChange(prim, cachePath, propertyName);
        
        // XXX: We need to handle UsdSkel-related primvars manually here, since
        // they're ignored in GprimAdapter.
        if (propertyName == UsdSkelTokens->primvarsSkelJointIndices || 
            propertyName == UsdSkelTokens->primvarsSkelJointWeights ||
            propertyName == UsdSkelTokens->primvarsSkelSkinningMethod ||
            propertyName == UsdSkelTokens->primvarsSkelGeomBindTransform ||
            propertyName == UsdSkelTokens->skelJoints ||
            propertyName == UsdSkelTokens->skelBlendShapes ||
            propertyName == UsdSkelTokens->skelBlendShapeTargets) {
            
            if (dirtyBits == HdChangeTracker::AllDirty) {
                // XXX: We don't have access to the UsdImagingIndexProxy here,
                // so we can't propagate dirtyness to the computation Sprims
                // here. Instead, we set the DirtyPrimvar bit on the skinned
                // prim, and handle the dirtyness propagation in MarkDirty(..).
                dirtyBits = HdChangeTracker::DirtyPrimvar;
            } else {
                TF_WARN("Skinned prim %s needs to be resync'd because of a"
                        "property change. Hijacking doesn't work in this "
                        "scenario.\n", cachePath.GetText());
            }
        }

        return dirtyBits;
    }
    
    if (_IsSkinningComputationPath(cachePath) ||
        _IsSkinningInputAggregatorComputationPath(cachePath)) {
        // Nothing to do.
        return HdChangeTracker::Clean;
    }

    // We don't expect to get callbacks on behalf of any other prims on
    // the USD stage.
    TF_WARN("Unhandled ProcessPropertyChange callback for cachePath <%s> "
                "in UsdSkelImagingSkelAdapter.", cachePath.GetText());
    return HdChangeTracker::Clean;
}

void
UsdSkelImagingSkeletonAdapter::ProcessPrimResync(
    SdfPath const& primPath,
    UsdImagingIndexProxy* index)
{
    TF_DEBUG(USDIMAGING_CHANGES).Msg(
        "[SkeletonAdapter] ProcessPrimResync called for %s\n",
        primPath.GetText());

    // The SkelRoot must be repopulated upon a resync of the Skel
    // or any of the skinned prims.
    // Prior to removal of cache entries (in _RemovePrim), lookup
    // the SkelRoot so that we know what to repopulate.
    SdfPathVector pathsToRepopulate;
    if (_IsSkinnedPrimPath(primPath)) {
        if (const _SkinnedPrimData* data = _GetSkinnedPrimData(primPath)) {
            pathsToRepopulate.emplace_back(data->skelRootPath);
        }
    } else {
        // PrimResync might be called on behalf of the skeleton.
        if (_SkelData* skelData = _GetSkelData(primPath)) {
            pathsToRepopulate.insert(
                pathsToRepopulate.end(),
                skelData->skelRootPaths.begin(),
                skelData->skelRootPaths.end());
        }
    }

    // Remove prim and primInfo entries.
    // A skeleton removal triggers all skinned prims using it to be removed as
    // well.
    _RemovePrim(primPath, index);

    if (!pathsToRepopulate.empty()) {
        // This isn't as bad as it seems.
        // While Populate will be called on all prims under the SkelRoot,
        // we'll only re-insert prims that were removed.
        // See UsdImagingIndexProxy::AddPrimInfo.
        for (const SdfPath& repopulatePath : pathsToRepopulate) {
            index->Repopulate(repopulatePath);
        }
    }
}

void
UsdSkelImagingSkeletonAdapter::ProcessPrimRemoval(
    SdfPath const& primPath,
    UsdImagingIndexProxy* index)
{
    // Note: _RemovePrim removes the Hydra prim and the UsdImaging primInfo
    // entries as well (unlike the pattern followed in PrimAdapter)
    _RemovePrim(primPath, index);
}

void
UsdSkelImagingSkeletonAdapter::MarkDirty(const UsdPrim& prim,
                                         const SdfPath& cachePath,
                                         HdDirtyBits dirty,
                                         UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // Mark the bone mesh dirty
        index->MarkRprimDirty(cachePath, dirty);
        // Re-run UpdateForTime if needed.
        if (dirty & HdChangeTracker::DirtyPrimvar) {
            index->RequestUpdateForTime(cachePath);
        }
    } else if (_IsSkinnedPrimPath(cachePath)) {

        // Since The SkeletonAdapter hijacks skinned prims (see SkelRootAdapter),
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkDirty(prim, cachePath, dirty, index);
        // Re-run UpdateForTime if needed. Note that adapter->MarkDirty might
        // request this as well, but the delegate will de-duplicate the calls.
        if (dirty & HdChangeTracker::DirtyPrimvar) {
            index->RequestUpdateForTime(cachePath);
        }

        // Propagate dirtyness on the skinned prim to the computations.
        // Also see related comment in ProcessPropertyChange(..)

        // The skinning computation pulls on the transform as well as primvars
        // authored on the skinned prim.
        if (dirty & HdChangeTracker::DirtyTransform ||
            dirty & HdChangeTracker::DirtyPrimvar) {
          
            TF_DEBUG(USDIMAGING_COMPUTATIONS).Msg(
                "[SkeletonAdapter::MarkDirty] Propagating dirtyness from "
                "skinned prim %s to its computations\n", cachePath.GetText());
            
            index->MarkSprimDirty(_GetSkinningComputationPath(cachePath),
                                  HdExtComputation::DirtySceneInput);

        }

        // The aggregator computation pulls on primvars authored on the skinned
        // prim, but doesn't pull on its transform.
        if (dirty & HdChangeTracker::DirtyPrimvar) {
            index->MarkSprimDirty(
                _GetSkinningInputAggregatorComputationPath(cachePath),
                HdExtComputation::DirtySceneInput);
        }
    
    } else if (_IsSkinningComputationPath(cachePath) ||
              _IsSkinningInputAggregatorComputationPath(cachePath)) {

         TF_DEBUG(USDIMAGING_COMPUTATIONS).Msg(
                "[SkeletonAdapter::MarkDirty] Marking "
                "computation %s for skinned prim %s as Dirty (bits = 0x%x\n",
                cachePath.GetText(), prim.GetPath().GetText(), dirty);

        index->MarkSprimDirty(cachePath, dirty);
    
    } else {
        // We don't expect to get callbacks on behalf of any other prims on
        // the USD stage.
         TF_WARN("Unhandled MarkDirty callback for cachePath <%s> "
                 "in UsdSkelImagingSkelAdapter.", cachePath.GetText());
    }
}

void
UsdSkelImagingSkeletonAdapter::MarkRefineLevelDirty(const UsdPrim& prim,
                                                    const SdfPath& cachePath,
                                                    UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // Complexity changes shouldn't affect the bone visualization.
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkRefineLevelDirty(prim, cachePath, index);
    }
    // Nothing to do otherwise.
}

void
UsdSkelImagingSkeletonAdapter::MarkReprDirty(const UsdPrim& prim,
                                             const SdfPath& cachePath,
                                             UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // The bone mesh doesn't have a repr opinion. Use the viewer opinion.
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkReprDirty(prim, cachePath, index);

    }
    // Nothing to do otherwise.
}

void
UsdSkelImagingSkeletonAdapter::MarkCullStyleDirty(const UsdPrim& prim,
                                                  const SdfPath& cachePath,
                                                  UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // Cullstyle changes shouldn't affect the bone visualization.
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkCullStyleDirty(prim, cachePath, index);

    }
    // Nothing to do otherwise.
}

void
UsdSkelImagingSkeletonAdapter::MarkRenderTagDirty(const UsdPrim& prim,
                                                  const SdfPath& cachePath,
                                                  UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // Cullstyle changes shouldn't affect the bone visualization.
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkRenderTagDirty(prim, cachePath, index);

    }
    // Nothing to do otherwise.
}

void
UsdSkelImagingSkeletonAdapter::MarkTransformDirty(const UsdPrim& prim,
                                                  const SdfPath& cachePath,
                                                  UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyTransform);
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkTransformDirty(prim, cachePath, index);

    } else if (_IsSkinningComputationPath(cachePath) ||
              _IsSkinningInputAggregatorComputationPath(cachePath)) {

        // XXX: See comments in ProcessPropertyChange about dirtyness
        // propagation to the computations.
    
    } else {
        // We don't expect to get callbacks on behalf of any other prims on
        // the USD stage.
         TF_WARN("Unhandled MarkDirty callback for cachePath <%s> "
                 "in UsdSkelImagingSkelAdapter.", cachePath.GetText());
    }
}


void
UsdSkelImagingSkeletonAdapter::MarkVisibilityDirty(const UsdPrim& prim,
                                                   const SdfPath& cachePath,
                                                   UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        index->MarkRprimDirty(cachePath, HdChangeTracker::DirtyVisibility);
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkVisibilityDirty(prim, cachePath, index);

        // Note:
        // (1) If a skeleton is invis'd, it continues to affect skinned prims.
        
        // (2) The computations are executed as a result of the Rprim sync step.
        // We skip syncing Rprims that are invis'd (note: if a prim is invisible
        // at the start, we do sync once), and thus won't trigger the
        // computations.

    } else if (_IsSkinningComputationPath(cachePath) ||
              _IsSkinningInputAggregatorComputationPath(cachePath)) {

        // Nothing to do. See comment above.
    
    } else {
        // We don't expect to get callbacks on behalf of any other prims on
        // the USD stage.
         TF_WARN("Unhandled MarkDirty callback for cachePath <%s> "
                 "in UsdSkelImagingSkelAdapter.", cachePath.GetText());
    }
}


void
UsdSkelImagingSkeletonAdapter::MarkMaterialDirty(const UsdPrim& prim,
                                                 const SdfPath& cachePath,
                                                 UsdImagingIndexProxy* index)
{
    if (_IsCallbackForSkeleton(prim)) {
        // The bone mesh uses the fallback material.
    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        adapter->MarkMaterialDirty(prim, cachePath, index);

    }
    // Nothing to do otherwise.
}

PxOsdSubdivTags
UsdSkelImagingSkeletonAdapter::GetSubdivTags(UsdPrim const& usdPrim,
                                             SdfPath const& cachePath,
                                             UsdTimeCode time) const
{
    if (_IsSkinnedPrimPath(cachePath)) {
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(usdPrim);
        return adapter->GetSubdivTags(usdPrim, cachePath, time);
    }
    return UsdImagingPrimAdapter::GetSubdivTags(usdPrim, cachePath, time);
}

/*virtual*/ 
VtValue
UsdSkelImagingSkeletonAdapter::GetTopology(UsdPrim const& prim,
                                           SdfPath const& cachePath,
                                           UsdTimeCode time) const
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_IsCallbackForSkeleton(prim)) {
        // The bone mesh uses the fallback material.
        _SkelData* skelData = _GetSkelData(cachePath);
        if (!TF_VERIFY(skelData)) {
            return VtValue();
        }
        return VtValue(skelData->ComputeTopologyAndRestState());

    } else if ( _IsSkinnedPrimPath(cachePath)) {
        // Since The SkeletonAdapter hijacks callbacks for the skinned prim,
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        return adapter->GetTopology(prim, cachePath, time);
    }
    return VtValue();
}

/*virtual*/
GfRange3d
UsdSkelImagingSkeletonAdapter::GetExtent(UsdPrim const& prim,
                                         SdfPath const& cachePath,
                                         UsdTimeCode time) const
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    UsdGeomBoundable boundable(prim);
    VtVec3fArray extent;
    if (boundable.GetExtentAttr().Get(&extent, time) && extent.size() == 2) {
        // Note:
        // Usd stores extent as 2 float vecs. We do an implicit 
        // conversion to doubles
        const VtVec3fArray &extentConst = extent.AsConst();
        return GfRange3d(extentConst[0], extentConst[1]);
    } else {
        // Return empty range if no value was found.
        return GfRange3d();
    }
}

/*virtual*/
VtValue
UsdSkelImagingSkeletonAdapter::Get(UsdPrim const& prim,
                                   SdfPath const& cachePath,
                                   TfToken const& key,
                                   UsdTimeCode time,
                                   VtIntArray *outIndices) const
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (_IsCallbackForSkeleton(prim)) {
        _SkelData* skelData = _GetSkelData(cachePath);
        if (!TF_VERIFY(skelData)) {
            return VtValue();
        }

        if (key == HdTokens->displayColor) {
            GfVec3f color = _GetSkeletonDisplayColor(prim, time);
            return VtValue(color);

        } else if (key == HdTokens->displayOpacity) {
            float opacity = _GetSkeletonDisplayOpacity(prim, time);
            return VtValue(opacity);

        } else if (key == HdTokens->points) {
            skelData->ComputeTopologyAndRestState();
            return VtValue(skelData->ComputePoints(time));
        } 
    }

    if (_IsSkinnedPrimPath(cachePath)) {
        UsdPrim const& skinnedPrim = prim;
        SdfPath const& skinnedPrimPath = cachePath;

        // Since The SkeletonAdapter hijacks skinned prims (see SkelRootAdapter)
        // make sure to delegate to the actual adapter registered for the prim.
        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(skinnedPrim);
        return adapter->Get(skinnedPrim, skinnedPrimPath, key, time, outIndices);
    }

    return BaseAdapter::Get(prim, cachePath, key, time, outIndices);
}

/*virtual*/
bool
UsdSkelImagingSkeletonAdapter::GetDoubleSided(UsdPrim const& prim, 
                                              SdfPath const& cachePath, 
                                              UsdTimeCode time) const
{
    if (_IsCallbackForSkeleton(prim)) {
        return true;
    } else if (_IsSkinnedPrimPath(cachePath)) {
        if (UsdImagingPrimAdapterSharedPtr adapter =
                _GetPrimAdapter(prim)) {
            return adapter->GetDoubleSided(prim, cachePath, time);
        }
    }
    return BaseAdapter::GetDoubleSided(prim, cachePath, time);
}

/*virtual*/
SdfPath
UsdSkelImagingSkeletonAdapter::GetMaterialId(UsdPrim const& prim, 
                                             SdfPath const& cachePath, 
                                             UsdTimeCode time) const
{
    if (_IsCallbackForSkeleton(prim)) {
        // skeleton has no material
        return SdfPath();
    } else if (_IsSkinnedPrimPath(cachePath)) {
        if (UsdImagingPrimAdapterSharedPtr adapter =
                _GetPrimAdapter(prim)) {
            return adapter->GetMaterialId(prim, cachePath, time);
        }
    }
    return BaseAdapter::GetMaterialId(prim, cachePath, time);
}


// ---------------------------------------------------------------------- //
/// Computation API
// ---------------------------------------------------------------------- //
void
UsdSkelImagingSkeletonAdapter::InvokeComputation(
    SdfPath const& cachePath,
    HdExtComputationContext* context)
{
    HD_TRACE_FUNCTION();
    TfToken skinningMethod = UsdSkelTokens->classicLinear;
    if (const _SkinnedPrimData* const skinnedPrimData =
            _GetSkinnedPrimData(cachePath.GetParentPath())) {
        skinningMethod = skinnedPrimData->skinningQuery.GetSkinningMethod();
    }

    UsdSkelImagingInvokeExtComputation(skinningMethod, context);
}

// ---------------------------------------------------------------------- //
/// Non-virtual public API
// ---------------------------------------------------------------------- //

void
UsdSkelImagingSkeletonAdapter::RegisterSkelBinding(
    UsdSkelBinding const& binding)
{
    _skelBindingMap[binding.GetSkeleton().GetPath()] = binding;
}

// ---------------------------------------------------------------------- //
/// Change Processing API (protected)
// ---------------------------------------------------------------------- //

void
UsdSkelImagingSkeletonAdapter::_RemovePrim(const SdfPath& cachePath,
                                           UsdImagingIndexProxy* index)
{
    // Note: We remove both prim (R/Sprim) and primInfo entries (unlike
    // UsdImagingPrimAdapter::_RemovePrim) since we override
    // ProcessPrimRemoval and ProcessPrimResync, which call _RemovePrim.
    
    // Alternative way of finding whether this is a callback for the skeleton/
    // bone mesh.
    if (_GetSkelData(cachePath)) {

        TF_DEBUG(USDIMAGING_CHANGES).Msg(
                "[SkeletonAdapter::_RemovePrim] Remove skeleton"
                "%s\n", cachePath.GetText());
        
        // Remove bone mesh.
        index->RemoveRprim(cachePath);

        // Remove all skinned prims that are targered by the skeleton, and their
        // computations.
        UsdSkelBinding const& binding = _skelBindingMap[cachePath];
        for (auto const& skinningQuery : binding.GetSkinningTargets()) {
            _RemoveSkinnedPrimAndComputations(
                skinningQuery.GetPrim().GetPath(), index);
        }

        // Clear various caches.
        _skelBindingMap.erase(cachePath);
        _skelDataCache.erase(cachePath);
        // TODO: Clearing the entire skel cache is excessive, but correct.
        _skelCache.Clear();

    } else if (_IsSkinnedPrimPath(cachePath)) {
        _RemoveSkinnedPrimAndComputations(cachePath, index);
    }

    // Ignore callbacks on behalf of the computations since we remove them
    // only when removing the skinned prim.
}

// ---------------------------------------------------------------------- //
/// Handlers for the Bone Mesh
// ---------------------------------------------------------------------- //
bool
UsdSkelImagingSkeletonAdapter::_IsCallbackForSkeleton(const UsdPrim& prim) const
{
    // The Skeleton prim is registered against the bone mesh. See Populate(..)
    return prim.IsA<UsdSkelSkeleton>();
}

GfVec3f
UsdSkelImagingSkeletonAdapter::_GetSkeletonDisplayColor(
        const UsdPrim& prim,
        UsdTimeCode time) const
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    UsdGeomPrimvarsAPI primvars(prim);

    if (UsdGeomPrimvar pv = primvars.GetPrimvar(
            UsdGeomTokens->primvarsDisplayColor)) {
        // May be stored as a constant.
        GfVec3f color;
        if (pv.Get(&color, time))
            return color;

        // May be stored as an array holding a single elem.
        VtVec3fArray colors;
        if (pv.Get(&colors, time) && colors.size() == 1)
            return colors[0];
    }
    return GfVec3f(0.5f);
}


float
UsdSkelImagingSkeletonAdapter::_GetSkeletonDisplayOpacity(
        const UsdPrim& prim,
        UsdTimeCode time) const
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    UsdGeomPrimvarsAPI primvars(prim);

    if (UsdGeomPrimvar pv = primvars.GetPrimvar(
            UsdGeomTokens->primvarsDisplayOpacity)) {
        // May be stored as a constant.
        float opacity;
        if (pv.Get(&opacity, time))
            return opacity;

        // May be stored as an array holding a single elem.
        VtFloatArray opacities;
        if (pv.Get(&opacities, time) && opacities.size() == 1)
            return opacities[0];
    }
    return 1.0f;
}

void
UsdSkelImagingSkeletonAdapter::_TrackBoneMeshVariability(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    const _SkelData* skelData = _GetSkelData(cachePath);
    if (!TF_VERIFY(skelData)) {
        return;
    }

    if (!_IsVarying(prim,
                    UsdGeomTokens->primvarsDisplayColor,
                    HdChangeTracker::DirtyPrimvar,
                    UsdImagingTokens->usdVaryingPrimvar,
                    timeVaryingBits,
                    false)) {
        // Only do this second check if the displayColor isn't already known
        // to be varying.
        _IsVarying(prim,
                   UsdGeomTokens->primvarsDisplayOpacity,
                   HdChangeTracker::DirtyPrimvar,
                   UsdImagingTokens->usdVaryingPrimvar,
                   timeVaryingBits,
                   false);
    }

    // Discover time-varying extent.
    _IsVarying(prim,
               UsdGeomTokens->extent,
               HdChangeTracker::DirtyExtent,
               UsdImagingTokens->usdVaryingExtent,
               timeVaryingBits,
               false);

    // Discover time-varying points.
    if (const UsdSkelAnimQuery& animQuery =
        skelData->skelQuery.GetAnimQuery()) {

        if(animQuery.JointTransformsMightBeTimeVarying()) {
            (*timeVaryingBits) |= HdChangeTracker::DirtyPoints;
            HD_PERF_COUNTER_INCR(UsdImagingTokens->usdVaryingPrimvar);
        }
    }

    // Discover time-varying transforms.
    _IsTransformVarying(prim,
                        HdChangeTracker::DirtyTransform,
                        UsdImagingTokens->usdVaryingXform,
                        timeVaryingBits);

    // Discover time-varying visibility.
    _IsVarying(prim,
               UsdGeomTokens->visibility,
               HdChangeTracker::DirtyVisibility,
               UsdImagingTokens->usdVaryingVisibility,
               timeVaryingBits,
               true);
}


TfToken
UsdSkelImagingSkeletonAdapter::GetPurpose(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& instanceInheritablePurpose) const
{
    TRACE_FUNCTION();

    TfToken purpose;

    if (_IsCallbackForSkeleton(prim)) {

        const _SkelData* skelData = _GetSkelData(cachePath);
        if (TF_VERIFY(skelData)) {
            purpose = skelData->ComputePurpose();
        }

        // Empty purpose means there is no authored or inherited opinion.
        // Instead of falling back to "default" purpose (via UsdGeomImageable),
        // we use "guide" to avoid rendering the bone mesh always.
        // See commentary in _SkelData::ComputePurpose.
        if (purpose.IsEmpty()) {
            if (instanceInheritablePurpose.IsEmpty()) {
                purpose = UsdGeomTokens->guide;
            } else {
                purpose = instanceInheritablePurpose;
            }
        }

    } else {
        purpose = BaseAdapter::GetPurpose(prim, cachePath, 
                                          instanceInheritablePurpose);
    }

    return purpose;
}

const TfTokenVector &
UsdSkelImagingSkeletonAdapter::GetExtComputationSceneInputNames(
    SdfPath const& cachePath) const
{
    // This function provides the scene inputs for the skinning computation
    // for both CPU and GPU codepaths.
    if (_IsSkinningComputationPath(cachePath)) {

        TfToken skinningMethod = UsdSkelTokens->classicLinear;
        const _SkinnedPrimData* skinnedPrimData =
            _GetSkinnedPrimData(cachePath.GetParentPath());
        if (skinnedPrimData) {
            skinningMethod = skinnedPrimData->skinningQuery.GetSkinningMethod();
        }

        // Scene inputs
        if (skinningMethod == UsdSkelTokens->classicLinear) {

            static TfTokenVector sceneInputNames({
                    // From the skinned prim
                    UsdSkelImagingExtComputationInputNameTokens
                        ->primWorldToLocal,
                    // From the skeleton
                    UsdSkelImagingExtComputationInputNameTokens
                        ->blendShapeWeights,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skinningXforms,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skelLocalToWorld
            });
            return sceneInputNames;

        } else if (skinningMethod == UsdSkelTokens->dualQuaternion) {
            // NOTE:
            // -----
            // 'skinningXforms' are used in the CPU computation callback for
            // and the conversion to dual quats is done internally inside
            // the usdSkel/utils skinning callback.
            // 'skinningScaleXforms' and 'skinningDualQuats' are used in
            // the GPU DQS kernel.
            // Passing all of them here as scene input names to keep
            // the inputs uniform for the CPU and GPU cases.
            // XXX
            // This will result in additional data being uploaded to the GPU
            // for the DQS case on every time step since these are scene inputs.
            // This should be revisited if/when this becomes a performance issue.
            static TfTokenVector sceneInputNames({
                    // From the skinned prim
                    UsdSkelImagingExtComputationInputNameTokens
                        ->primWorldToLocal,
                    // From the skeleton
                    UsdSkelImagingExtComputationInputNameTokens
                        ->blendShapeWeights,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skinningXforms,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skinningScaleXforms,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skinningDualQuats,
                    UsdSkelImagingExtComputationInputNameTokens
                        ->skelLocalToWorld
                });
            return sceneInputNames;
        } else {
            static TfTokenVector sceneInputNames;
            TF_WARN("Unknown skinning method: '%s' ", skinningMethod.GetText());
            return sceneInputNames;
        }
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        // ExtComputation inputs
 	// Scene inputs for the aggregator computation.
        static TfTokenVector inputNames({
            // Data authored on the skinned prim as primvars.
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->restPoints,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->geomBindXform,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->influences,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->numInfluencesPerComponent,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->hasConstantInfluences,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->blendShapeOffsets,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->blendShapeOffsetRanges,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->numBlendShapeOffsetRanges
        });
        return inputNames;
    }  

    return BaseAdapter::GetExtComputationSceneInputNames(cachePath);;

}

HdExtComputationInputDescriptorVector
UsdSkelImagingSkeletonAdapter::GetExtComputationInputs(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    const UsdImagingInstancerContext *instancerContext) const
{
    // See NOTE(s) in GetExtComputationSceneInputNames above.
    
    if (_IsSkinningComputationPath(cachePath)) {

        // Computation inputs
        static TfTokenVector compInputNames({
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->restPoints,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->geomBindXform,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->influences,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->numInfluencesPerComponent,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->hasConstantInfluences,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->blendShapeOffsets,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->blendShapeOffsetRanges,
            UsdSkelImagingExtAggregatorComputationInputNameTokens
                ->numBlendShapeOffsetRanges
        });

        SdfPath skinnedPrimPath =
            ResolveCachePath(prim.GetPath(), instancerContext);
        SdfPath renderIndexAggrCompId = _ConvertCachePathToIndexPath(
            _GetSkinningInputAggregatorComputationPath(skinnedPrimPath));
        
        HdExtComputationInputDescriptorVector compInputDescs;
        for (auto const& input : compInputNames) {
            compInputDescs.emplace_back(
                HdExtComputationInputDescriptor(input,
                    renderIndexAggrCompId, input));
        }

        return compInputDescs;

    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        // No computation inputs
        return HdExtComputationInputDescriptorVector();
    }

    return BaseAdapter::GetExtComputationInputs(prim, cachePath, 
            instancerContext);
}

HdExtComputationOutputDescriptorVector
UsdSkelImagingSkeletonAdapter::GetExtComputationOutputs(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    const UsdImagingInstancerContext* instancerContext) const
{
    if (_IsSkinningComputationPath(cachePath)) {
    
        HdTupleType pointsType;
        pointsType.type = HdTypeFloatVec3;
        pointsType.count = 1;
        
        HdExtComputationOutputDescriptorVector outputsEntry;
        outputsEntry.emplace_back(
            UsdSkelImagingExtComputationOutputNameTokens
                ->skinnedPoints,
            pointsType);

        return outputsEntry;
    }

    return BaseAdapter::GetExtComputationOutputs(prim, cachePath, 
            instancerContext);
}

HdExtComputationPrimvarDescriptorVector
UsdSkelImagingSkeletonAdapter::GetExtComputationPrimvars(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    HdInterpolation interpolation,
    const UsdImagingInstancerContext* instancerContext) const
{
    if (_IsSkinnedPrimPath(cachePath)) {

        // We only support 'points' which is vertex interpolation
        if (interpolation != HdInterpolationVertex) {
            return HdExtComputationPrimvarDescriptorVector();
        }

        // Note: We don't specify the # of points, since the prim already knows
        // how many to expect for a given topology.
        // The count field below indicates that we have one vec3f per point.
        HdTupleType pointsType;
        pointsType.type = HdTypeFloatVec3;
        pointsType.count = 1;

        const SdfPath skinnedPrimPath = ResolveCachePath(
            prim.GetPath(), instancerContext);

        HdExtComputationPrimvarDescriptorVector compPrimvars;
        compPrimvars.emplace_back(
                        HdTokens->points,
                        HdInterpolationVertex,
                        HdPrimvarRoleTokens->point,
                        _ConvertCachePathToIndexPath(
                            _GetSkinningComputationPath(skinnedPrimPath)),
                        UsdSkelImagingExtComputationOutputNameTokens
                            ->skinnedPoints,
                        pointsType);

        return compPrimvars;
    }
    return BaseAdapter::GetExtComputationPrimvars(prim, cachePath, 
              interpolation, instancerContext);
}

namespace {

bool
_GetInfluences(const UsdSkelSkinningQuery& skinningQuery,
               UsdTimeCode time,
               VtVec2fArray* influences,
               int* numInfluencesPerComponent,
               bool* isConstant)
{
    VtIntArray vji;
    VtFloatArray vjw;   
    if (skinningQuery.ComputeJointInfluences(&vji, &vjw, time)) {
        influences->resize(vji.size());
        if (UsdSkelInterleaveInfluences(vji, vjw, *influences)) {
            *numInfluencesPerComponent =
                skinningQuery.GetNumInfluencesPerComponent();
            *isConstant = skinningQuery.IsRigidlyDeformed();
            return true;
        }
    }
    return false;
}


bool
_ComputeSkinningTransforms(const UsdSkelSkeletonQuery& skelQuery,
                           const UsdSkelSkinningQuery& skinningQuery,
                           UsdTimeCode time,
                           VtMatrix4fArray* xforms)
{
    HD_TRACE_FUNCTION();
    // PERFORMANCE:
    // Would be better to query skinning transforms only once per
    // skeleton, and share the results across each skinned prim.
    VtMatrix4fArray xformsInSkelOrder;
    if (skelQuery.ComputeSkinningTransforms(&xformsInSkelOrder, time)) {
        
        if (skinningQuery.GetJointMapper()) {
            // Each skinned prim may specify its own ordering of joints.
            // (eg., because only a subset set of joints may apply to the prim).
            // Return the remapped results.
            return skinningQuery.GetJointMapper()->RemapTransforms(
                xformsInSkelOrder, xforms);
        } else {
            // Prim does not specify a joint order, so joints are returned
            // in skel order.
            *xforms = std::move(xformsInSkelOrder);
            return true;
        }
    }
    return false;
}
               

bool
_ComputeSubShapeWeights(const UsdSkelSkeletonQuery& skelQuery,
                        const UsdSkelBlendShapeQuery& blendShapeQuery,
                        const UsdSkelSkinningQuery& skinningQuery,
                        UsdTimeCode time,
                        VtFloatArray* subShapeWeights)
{
    HD_TRACE_FUNCTION();

    // PERFORMANCE:
    // It is better to compute the initial weight values from the skel query,
    // and then share the results across each skinned prim!
    VtFloatArray weights;
    if (const UsdSkelAnimQuery& animQuery = skelQuery.GetAnimQuery()) {
        if (animQuery.ComputeBlendShapeWeights(&weights, time)) {

            // Each skinned prim may specify its own ordering of blend shapes   
            // (eg., because only a subset of blend shapes may apply to
            // the prim). Remap them.
            VtFloatArray weightsInPrimOrder;
            
            if (skinningQuery.GetBlendShapeMapper()) {
                const float defaultValue = 0.0f;
                if (!skinningQuery.GetBlendShapeMapper()->Remap(
                        weights, &weightsInPrimOrder,
                        /*elementSize*/ 1, &defaultValue)) {
                    return false;
                }
            } else {
                weightsInPrimOrder = std::move(weights);
            }

            return blendShapeQuery.ComputeFlattenedSubShapeWeights(
                weightsInPrimOrder, subShapeWeights);
        }
    }
    return false;
}

// Extract the Scale & Shear parts of 4x4 matrices by removing the
// translation & rotation. Return only the upper-left 3x3 matrices.
bool
_ExtractSkinningScaleXforms(const VtMatrix4fArray& skinningXforms,
                            VtMatrix3fArray* skinningScaleXforms)
{
    if (!skinningScaleXforms) {
        TF_CODING_ERROR("'skinningScaleXforms' pointer is null.");
        return false;
    }

    // Convert skinningXforms to skinningScaleXforms
    skinningScaleXforms->resize(skinningXforms.size());
    GfMatrix4f scaleOrientMat, factoredRotMat, perspMat;
    GfVec3f scale, translation;

    for (size_t i = 0; i < skinningXforms.size(); ++i) {
        const GfMatrix4f &matrix = skinningXforms[i];
        if (!matrix.Factor(&scaleOrientMat, &scale, &factoredRotMat,
                           &translation, &perspMat)) {
            // unable to decompose, set to identity
            (*skinningScaleXforms)[i] = GfMatrix3f(1);
        } else {
            // Remove shear & extract rotation
            factoredRotMat.Orthonormalize();
            // Calculate the scale + shear transform
            const GfMatrix4f tmpNonScaleXform =
                factoredRotMat * GfMatrix4f(1.0).SetTranslate(translation);
            (*skinningScaleXforms)[i] = (matrix * tmpNonScaleXform.GetInverse()).
                ExtractRotationMatrix();   // Extract the upper-left 3x3 matrix
        }
    }

    return true;
}

// Extract the translation & rotation parts of 4x4 matrices into dual quaternions.
// Use a pair of Vec4f to represent a dual quaternion.
bool
_ExtractSkinningDualQuats(const VtMatrix4fArray& skinningXforms,
                          VtVec4fArray* skinningDualQuats)
{
    if (!skinningDualQuats) {
        TF_CODING_ERROR("'skinningDualQuats' pointer is null.");
        return false;
    }

    // Convert skinningXforms to skinningDualQuats
    skinningDualQuats->resize(skinningXforms.size()*2);
    GfMatrix4f scaleOrientMat, factoredRotMat, perspMat;
    GfVec3f scale, translation;
    GfDualQuatf dq;

    for (size_t i = 0; i < skinningXforms.size(); ++i) {
        const GfMatrix4f &matrix = skinningXforms[i];
        if (!matrix.Factor(&scaleOrientMat, &scale, &factoredRotMat,
                           &translation, &perspMat)) {
            // unable to decompose, set to zero
            dq = GfDualQuatf::GetZero();
        } else {
            // Remove shear & extract rotation
            factoredRotMat.Orthonormalize();
            const GfQuaternion rotationQ = factoredRotMat.ExtractRotationMatrix()
                .ExtractRotationQuaternion();
            dq = GfDualQuatf(GfQuatf(GfQuatd(rotationQ.GetReal(), rotationQ.GetImaginary())),
                             translation);
        }

        const float    real_r = dq.GetReal().GetReal();
        const GfVec3f &real_i = dq.GetReal().GetImaginary();
        (*skinningDualQuats)[i*2]   = GfVec4f(real_i[0], real_i[1], real_i[2], real_r);

        const float    dual_r = dq.GetDual().GetReal();
        const GfVec3f &dual_i = dq.GetDual().GetImaginary();
        (*skinningDualQuats)[i*2+1] = GfVec4f(dual_i[0], dual_i[1], dual_i[2], dual_r);
    }

    return true;
}


} // namespace

VtValue 
UsdSkelImagingSkeletonAdapter::_GetExtComputationInputForSkinningComputation(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext) const
{
    TRACE_FUNCTION();

    // XXX: We don't receive the "cachePath" for the skinned prim, and so
    // the method below won't work when using multiple UsdImagingDelgate'.
    SdfPath skinnedPrimCachePath = ResolveCachePath(
        prim.GetPath(), instancerContext);

    // XXX: The only time varying input here is the skinning xforms.
    // However, we don't have fine-grained tracking to tell which
    // scene input is "dirty". Hence, fetch all these values and update
    // the value cache.
    // Note: With CPU computations, this is necessary. We don't use
    //       persistent buffer sources to cache the inputs.
    //       With GPU computations, we can use an "input aggregation"
    //       computations to remove the non-varying inputs into its own
    //       computation.

    // dispatchCount, elementCount, restPoints, geomBindXform
    if (name == HdTokens->dispatchCount ||
        name == HdTokens->elementCount) {

        // For dispatchCount, elementCount, we need to know 
        // the number of points on the skinned prim. Pull only when 
        // required.
        VtVec3fArray restPoints = _GetSkinnedPrimPoints(prim, 
                                        skinnedPrimCachePath, time);
        size_t numPoints = restPoints.size();
        return VtValue(numPoints);
    }

    // primWorldToLocal
    if (name == UsdSkelImagingExtComputationInputNameTokens
                    ->primWorldToLocal) {
        UsdGeomXformCache xformCache(time);
        GfMatrix4d primWorldToLocal =
                xformCache.GetLocalToWorldTransform(prim).GetInverse();
        return VtValue(primWorldToLocal);
    }
    
    // skinningXforms, skinningScaleXforms,
    // skinningDualQuats, skelLocalToWorld, blendShapeWeights
    if (name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningXforms ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningScaleXforms ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningDualQuats ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skelLocalToWorld ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->blendShapeWeights)
    {
        const _SkinnedPrimData* skinnedPrimData = 
            _GetSkinnedPrimData(skinnedPrimCachePath);

        if (!TF_VERIFY(skinnedPrimData)) {
            return VtValue();
        }

        const _SkelData* skelData = _GetSkelData(skinnedPrimData->skelPath);
        if (!TF_VERIFY(skelData)) {
            return VtValue();
        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningXforms ||
            name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningScaleXforms ||
            name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningDualQuats) {
            // Compute skinningXforms
            VtMatrix4fArray skinningXforms;
            if (!skinnedPrimData->hasJointInfluences ||
                !_ComputeSkinningTransforms(skelData->skelQuery,
                                            skinnedPrimData->skinningQuery,
                                            time, &skinningXforms)) {
                skinningXforms.assign(
                    skinnedPrimData->skinningQuery.GetJointMapper() ?
                    skinnedPrimData->skinningQuery.GetJointMapper()->size():
                    skelData->skelQuery.GetTopology().size(),
                    GfMatrix4f(1));
            }

            if (name == UsdSkelImagingExtComputationInputNameTokens
                            ->skinningXforms)
                return VtValue(skinningXforms);

            if (name == UsdSkelImagingExtComputationInputNameTokens
                            ->skinningScaleXforms) {
                // Extract skinningScaleXforms from skinningXforms
                VtMatrix3fArray skinningScaleXforms;
                if (!TF_VERIFY(_ExtractSkinningScaleXforms
                               (skinningXforms, &skinningScaleXforms)))
                    return VtValue();

                return VtValue(skinningScaleXforms);
            }

            if (name == UsdSkelImagingExtComputationInputNameTokens
                            ->skinningDualQuats) {
                // Extract skinningDualQuats from skinningXforms
                VtVec4fArray skinningDualQuats;
                if (!TF_VERIFY(_ExtractSkinningDualQuats
                               (skinningXforms, &skinningDualQuats)))
                    return VtValue();

                return VtValue(skinningDualQuats);
            }
        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->blendShapeWeights) {
            VtFloatArray weights;
            if (!skinnedPrimData->blendShapeQuery ||
                !_ComputeSubShapeWeights(skelData->skelQuery,
                                         *skinnedPrimData->blendShapeQuery,
                                         skinnedPrimData->skinningQuery,
                                         time, &weights)) {
                if (skinnedPrimData->blendShapeQuery) {
                    weights.assign(
                        skinnedPrimData->blendShapeQuery->GetNumSubShapes(),
                        0.0f);
                }
            }
            return VtValue(weights);

        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skelLocalToWorld) {
            // PERFORMANCE:
            // Would be better if we could access a shared xformCache here?
            UsdGeomXformCache xformCache(time);

            UsdPrim skelPrim(skelData->skelQuery.GetPrim());
            if (skelPrim.IsInPrototype()) {
                const auto bindingIt = 
                    _skelBindingMap.find(skinnedPrimData->skelPath);
                if (bindingIt != _skelBindingMap.end()) {
                    skelPrim = bindingIt->second.GetSkeleton().GetPrim();
                }
            }
            GfMatrix4d skelLocalToWorld =
                xformCache.GetLocalToWorldTransform(skelPrim);
            return VtValue(skelLocalToWorld);
        }
    }

    return BaseAdapter::GetExtComputationInput(prim, cachePath, name, time,
            instancerContext);
}

VtValue 
UsdSkelImagingSkeletonAdapter::_GetExtComputationInputForInputAggregator(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext) const
{
    // DispatchCount, ElementCount aren't relevant for an input aggregator
    // computation. 
    if (name == HdTokens->dispatchCount || name == HdTokens->elementCount) {
        return VtValue(size_t(0));
    }

    // XXX: We don't receive the "cachePath" for the skinned prim, and so
    // the method below won't work when using multiple UsdImagingDelegate's.
    SdfPath skinnedPrimCachePath = ResolveCachePath(
        prim.GetPath(), instancerContext);

    const _SkinnedPrimData* skinnedPrimData =
            _GetSkinnedPrimData(skinnedPrimCachePath);
    if (!TF_VERIFY(skinnedPrimData)) {
        return VtValue();
    }

    // restPoints
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->restPoints) {
        VtVec3fArray restPoints =
            _GetSkinnedPrimPoints(prim, skinnedPrimCachePath, time);
        return VtValue(restPoints);
    }

    // geomBindXform
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens->geomBindXform) {
        // read (optional) geomBindTransform property.
        // If unauthored, it is identity.
        const GfMatrix4d geomBindXform =
            skinnedPrimData->skinningQuery.GetGeomBindTransform();

        // Skinning computations use float precision.
        return VtValue(GfMatrix4f(geomBindXform));
    }

    // influences, numInfluencesPerComponent, hasConstantInfluences
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->influences ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->numInfluencesPerComponent ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->hasConstantInfluences) {

        VtVec2fArray influences;
        int numInfluencesPerComponent = 0;
        bool usesConstantJointPrimvar = false;
            
        if (skinnedPrimData->hasJointInfluences) {
            _GetInfluences(skinnedPrimData->skinningQuery,
                           time, &influences,
                           &numInfluencesPerComponent,
                           &usesConstantJointPrimvar);
        }

        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens->influences) {
            return VtValue(influences);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens->numInfluencesPerComponent) {
            return VtValue(numInfluencesPerComponent);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens->hasConstantInfluences) {
            return VtValue(usesConstantJointPrimvar);
        }
    }

            
    // blendShapeOffsets, blendShapeOffsetRanges, numBlendShapeOffsetRanges
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->blendShapeOffsets ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->blendShapeOffsetRanges ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->numBlendShapeOffsetRanges) {
            
        VtVec4fArray offsets;
        VtVec2iArray ranges;
        if (skinnedPrimData->blendShapeQuery) {
            skinnedPrimData->blendShapeQuery->ComputePackedShapeTable(
                &offsets, &ranges);
        }

        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->blendShapeOffsets) {
            return VtValue(offsets);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->blendShapeOffsetRanges) {
            return VtValue(ranges);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->numBlendShapeOffsetRanges) {
            // The size of the offset ranges needs to be available for GL
            return VtValue(static_cast<int>(ranges.size()));
        }
    }

    return BaseAdapter::GetExtComputationInput(prim, cachePath, name, time,
            instancerContext);
}

/// Unions the provided list of samples with the boundary of the shutter
/// interval, and clamps to the maximum number of samples.
static size_t
_UnionTimeSamples(
    GfInterval const& interval,
    size_t maxNumSamples,
    std::vector<double>* timeSamples)
{
    // Add time samples at the boudary conditions
    timeSamples->push_back(interval.GetMin());
    timeSamples->push_back(interval.GetMax());

    // Sort and remove duplicates.
    std::sort(timeSamples->begin(), timeSamples->end());
    timeSamples->erase(std::unique(timeSamples->begin(), timeSamples->end()),
                       timeSamples->end());

    return std::min(maxNumSamples, timeSamples->size());
}

static void
_InitIdentityXforms(
    UsdSkelSkeletonQuery const& skelQuery,
    UsdSkelSkinningQuery const& skinningQuery,
    VtMatrix4fArray* skinningXforms)
{
    skinningXforms->assign(skinningQuery.GetJointMapper()
                               ? skinningQuery.GetJointMapper()->size()
                               : skelQuery.GetTopology().size(),
                           GfMatrix4f(1));
}

size_t
UsdSkelImagingSkeletonAdapter::_SampleExtComputationInputForSkinningComputation(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext,
    size_t maxSampleCount,
    float *sampleTimes,
    VtValue *sampleValues)
{
    TRACE_FUNCTION();

    if (maxSampleCount == 0) {
        return 0;
    }

    // XXX: We don't receive the "cachePath" for the skinned prim, and so
    // the method below won't work when using multiple UsdImagingDelgate'.
    SdfPath skinnedPrimCachePath = ResolveCachePath(
        prim.GetPath(), instancerContext);

    // XXX: The only time varying input here is the skinning xforms.
    // However, we don't have fine-grained tracking to tell which
    // scene input is "dirty". Hence, fetch all these values and update
    // the value cache.
    // Note: With CPU computations, this is necessary. We don't use
    //       persistent buffer sources to cache the inputs.
    //       With GPU computations, we can use an "input aggregation"
    //       computations to remove the non-varying inputs into its own
    //       computation.
    

    // dispatchCount, elementCount, restPoints, geomBindXform
    if (name == HdTokens->dispatchCount ||
        name == HdTokens->elementCount) {

        // For dispatchCount, elementCount, we need to know 
        // the number of points on the skinned prim. Pull only when 
        // required.
        VtVec3fArray restPoints = _GetSkinnedPrimPoints(prim, 
                                        skinnedPrimCachePath, time);
        size_t numPoints = restPoints.size();
        sampleValues[0] = VtValue(numPoints);
        sampleTimes[0] = 0.f;
        return 1;
    }

    // primWorldToLocal
    if (name == UsdSkelImagingExtComputationInputNameTokens
                    ->primWorldToLocal) {
        // This "CAPACITY = 4" indicates the maximum size of the stack in these 
        // TfSmallVectors. Ideally, this would be configurable by higher level 
        // components such as the render delegate (similar to the rest of our 
        // sampling API in Hydra) but for now this is a step in the right 
        // direction.
        constexpr unsigned int CAPACITY = 4;
        TfSmallVector<GfMatrix4d, CAPACITY> sampleXforms(maxSampleCount);

        UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
        const size_t numSamples = adapter->SampleTransform(
            prim, skinnedPrimCachePath, time, maxSampleCount, sampleTimes,
            sampleXforms.data());

        const size_t numEvaluatedSamples = std::min(numSamples, maxSampleCount);
        for (size_t i = 0; i < numEvaluatedSamples; ++i) {
            sampleValues[i] = VtValue(sampleXforms[i].GetInverse());
        }

        return numSamples;
    }
    
    // skinningXforms, skinningScaleXforms, skinningDualQuats,
    // skelLocalToWorld, blendShapeWeights
    if (name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningXforms ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningScaleXforms ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skinningDualQuats ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->skelLocalToWorld ||
        name == UsdSkelImagingExtComputationInputNameTokens
                    ->blendShapeWeights)
    {
        const _SkinnedPrimData* skinnedPrimData = 
            _GetSkinnedPrimData(skinnedPrimCachePath);

        if (!TF_VERIFY(skinnedPrimData)) {
            return 0;
        }

        const _SkelData* skelData = _GetSkelData(skinnedPrimData->skelPath);
        if (!TF_VERIFY(skelData)) {
            return 0;
        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningXforms ||
            name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningScaleXforms ||
            name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningDualQuats) {
            const UsdSkelAnimQuery &animQuery = skinnedPrimData->animQuery;

            if (skinnedPrimData->hasJointInfluences && animQuery) {

                const GfInterval interval = _GetCurrentTimeSamplingInterval();
                std::vector<double> times;
                if (!animQuery.GetJointTransformTimeSamplesInInterval(interval,
                                                                      &times)) {
                    return 0;
                }

                const size_t numSamplesToEvaluate =
                    _UnionTimeSamples(interval, maxSampleCount, &times);

                for (size_t i = 0; i < numSamplesToEvaluate; ++i) {
                    sampleTimes[i] = times[i] - time.GetValue();

                    VtMatrix4fArray skinningXforms;
                    if (!_ComputeSkinningTransforms(
                            skelData->skelQuery, skinnedPrimData->skinningQuery,
                            times[i], &skinningXforms)) {
                        _InitIdentityXforms(skelData->skelQuery,
                                            skinnedPrimData->skinningQuery,
                                            &skinningXforms);
                    }
                    if (name == UsdSkelImagingExtComputationInputNameTokens
                                    ->skinningXforms) {
                        sampleValues[i] = VtValue::Take(skinningXforms);
                    }
                    else if (name == UsdSkelImagingExtComputationInputNameTokens
                                         ->skinningScaleXforms) {
                        VtMatrix3fArray skinningScaleXforms;
                        _ExtractSkinningScaleXforms(skinningXforms,
                                                    &skinningScaleXforms);
                        sampleValues[i] = VtValue::Take(skinningScaleXforms);
                    }
                    else {
                        VtVec4fArray skinningDualQuats;
                        _ExtractSkinningDualQuats(skinningXforms,
                                                  &skinningDualQuats);
                        sampleValues[i] = VtValue::Take(skinningDualQuats);
                    }
                }

                return times.size();
            }
            else {
                VtMatrix4fArray skinningXforms;
                _InitIdentityXforms(skelData->skelQuery,
                                    skinnedPrimData->skinningQuery,
                                    &skinningXforms);
                sampleValues[0] = VtValue::Take(skinningXforms);
                sampleTimes[0] = 0.f;
                return 1;
            }
        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->blendShapeWeights) {
            const UsdSkelAnimQuery &animQuery = skinnedPrimData->animQuery;
            if (skinnedPrimData->blendShapeQuery && animQuery) {

                const GfInterval interval = _GetCurrentTimeSamplingInterval();
                std::vector<double> times;
                if (!animQuery.GetBlendShapeWeightTimeSamplesInInterval(
                        interval, &times)) {
                    return 0;
                }

                const size_t numSamplesToEvaluate =
                    _UnionTimeSamples(interval, maxSampleCount, &times);

                for (size_t i = 0; i < numSamplesToEvaluate; ++i) {
                    sampleTimes[i] = times[i] - time.GetValue();

                    VtFloatArray weights;
                    if (!_ComputeSubShapeWeights(
                            skelData->skelQuery,
                            *skinnedPrimData->blendShapeQuery,
                            skinnedPrimData->skinningQuery, times[i],
                            &weights)) {
                        weights.assign(
                            skinnedPrimData->blendShapeQuery->GetNumSubShapes(),
                            0.0f);
                    }
                    sampleValues[i] = VtValue::Take(weights);
                }

                return times.size();

            } else {
                sampleValues[0] = VtValue(VtFloatArray());
                sampleTimes[0] = 0.f;
                return 1;
            }
        }

        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skelLocalToWorld) {
            UsdPrim skelPrim(skelData->skelQuery.GetPrim());
            if (skelPrim.IsInPrototype()) {
                const auto bindingIt = 
                    _skelBindingMap.find(skinnedPrimData->skelPath);
                if (bindingIt != _skelBindingMap.end()) {
                    skelPrim = bindingIt->second.GetSkeleton().GetPrim();
                }
            }

            static constexpr unsigned int CAPACITY = 4;
            TfSmallVector<GfMatrix4d, CAPACITY> sampleXforms(maxSampleCount);

            SdfPath skelCachePath = ResolveCachePath(
                skelPrim.GetPath(), instancerContext);
            UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(skelPrim);

            const size_t numSamples = adapter->SampleTransform(
                skelPrim, skelCachePath, time, maxSampleCount, sampleTimes,
                sampleXforms.data());

            const size_t numEvaluatedSamples =
                std::min(numSamples, maxSampleCount);
            for (size_t i = 0; i < numEvaluatedSamples; ++i) {
                sampleValues[i] = VtValue(sampleXforms[i]);
            }

            return numSamples;
        }
    }

    return BaseAdapter::SampleExtComputationInput(
        prim, cachePath, name, time, instancerContext, maxSampleCount,
        sampleTimes, sampleValues);
}

size_t
UsdSkelImagingSkeletonAdapter::_SampleExtComputationInputForInputAggregator(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext,
    size_t maxSampleCount,
    float *sampleTimes,
    VtValue *sampleValues)
{
    if (maxSampleCount == 0) {
        return 0;
    }

    // DispatchCount, ElementCount aren't relevant for an input aggregator
    // computation. 
    if (name == HdTokens->dispatchCount || name == HdTokens->elementCount) {
        return 0;
    }

    // XXX: We don't receive the "cachePath" for the skinned prim, and so
    // the method below won't work when using multiple UsdImagingDelegate's.
    SdfPath skinnedPrimCachePath = ResolveCachePath(
            prim.GetPath(), instancerContext);

    const _SkinnedPrimData* skinnedPrimData =
            _GetSkinnedPrimData(skinnedPrimCachePath);
    if (!TF_VERIFY(skinnedPrimData)) {
        return 0;
    }

    // restPoints
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->restPoints) {
        // Rest points aren't expected to be time-varying.
        sampleValues[0] =
            VtValue(_GetSkinnedPrimPoints(prim, skinnedPrimCachePath, time));
        sampleTimes[0] = 0.f;
        return 1;
    }

    // skinningMethod
    if (name == _tokens->skinningMethod) {
        // read (optional) skinningMethod property.
        // If unauthored, it is "classicLinear".
        const TfToken skinningMethod =
            skinnedPrimData->skinningQuery.GetSkinningMethod();

        sampleValues[0] = VtValue(skinningMethod);
        sampleTimes[0] = 0.f;
        return 1;
    }

    // geomBindXform
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->geomBindXform) {
        // read (optional) geomBindTransform property.
        // If unauthored, it is identity.
        const GfMatrix4d geomBindXform =
            skinnedPrimData->skinningQuery.GetGeomBindTransform();

        // Skinning computations use float precision.
        sampleValues[0] = GfMatrix4f(geomBindXform);
        sampleTimes[0] = 0.f;
        return 1;
    }

    // influences, numInfluencesPerComponent, hasConstantInfluences
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->influences ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->numInfluencesPerComponent ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->hasConstantInfluences) {

        VtVec2fArray influences;
        int numInfluencesPerComponent = 0;
        bool usesConstantJointPrimvar = false;

        if (skinnedPrimData->hasJointInfluences) {
            _GetInfluences(skinnedPrimData->skinningQuery, time, &influences,
                           &numInfluencesPerComponent,
                           &usesConstantJointPrimvar);
        }

        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->influences) {
            sampleValues[0] = VtValue(influences);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->numInfluencesPerComponent) {
            sampleValues[0] = VtValue(numInfluencesPerComponent);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->hasConstantInfluences) {
            sampleValues[0] = VtValue(usesConstantJointPrimvar);
        }

        sampleTimes[0] = 0.f;
        return 1;
    }

    // blendShapeOffsets, blendShapeOffsetRanges, numBlendShapeOffsetRanges
    if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->blendShapeOffsets ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->blendShapeOffsetRanges ||
        name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                    ->numBlendShapeOffsetRanges) {

        VtVec4fArray offsets;
        VtVec2iArray ranges;
        if (skinnedPrimData->blendShapeQuery) {
            skinnedPrimData->blendShapeQuery->ComputePackedShapeTable(
                &offsets, &ranges);
        }

        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->blendShapeOffsets) {
            sampleValues[0] = VtValue(offsets);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->blendShapeOffsetRanges) {
            sampleValues[0] = VtValue(ranges);
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                        ->numBlendShapeOffsetRanges) {
            // The size of the offset ranges needs to be available for GL
            sampleValues[0] = VtValue(static_cast<int>(ranges.size()));
        }

        sampleTimes[0] = 0.f;
        return 1;
    }

    return BaseAdapter::SampleExtComputationInput(
        prim, cachePath, name, time, instancerContext, maxSampleCount,
        sampleTimes, sampleValues);
}

VtValue 
UsdSkelImagingSkeletonAdapter::GetExtComputationInput(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext) const
{
    TRACE_FUNCTION();

    if (_IsSkinningComputationPath(cachePath)) {
        return _GetExtComputationInputForSkinningComputation(
                prim, cachePath, name, time, instancerContext);
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        return _GetExtComputationInputForInputAggregator(
                prim, cachePath, name, time, instancerContext);
    }

    return BaseAdapter::GetExtComputationInput(prim, cachePath, name, time,
            instancerContext);
}

size_t
UsdSkelImagingSkeletonAdapter::SampleExtComputationInput(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& name,
    UsdTimeCode time,
    const UsdImagingInstancerContext* instancerContext,
    size_t maxSampleCount,
    float *sampleTimes,
    VtValue *sampleValues)
{
    TRACE_FUNCTION();

    if (_IsSkinningComputationPath(cachePath)) {
        return _SampleExtComputationInputForSkinningComputation(
            prim, cachePath, name, time, instancerContext, maxSampleCount,
            sampleTimes, sampleValues);
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        return _SampleExtComputationInputForInputAggregator(
            prim, cachePath, name, time, instancerContext, maxSampleCount,
            sampleTimes, sampleValues);
    }

    return BaseAdapter::SampleExtComputationInput(
        prim, cachePath, name, time, instancerContext, maxSampleCount,
        sampleTimes, sampleValues);
}

std::string 
UsdSkelImagingSkeletonAdapter::GetExtComputationKernel(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    const UsdImagingInstancerContext* instancerContext) const
{
    TRACE_FUNCTION();

    if (_IsSkinningComputationPath(cachePath)) {
        if (_IsEnabledCPUComputations()) {
            return std::string();
        } else {
            // read (optional) skinningMethod property.
            // If unauthored, it is classicLinear.
            TfToken skinningMethod = UsdSkelTokens->classicLinear;
            const _SkinnedPrimData* skinnedPrimData =
                _GetSkinnedPrimData(cachePath.GetParentPath());
            if (skinnedPrimData) {
                skinningMethod = skinnedPrimData->skinningQuery.GetSkinningMethod();
            }

            if (skinningMethod == UsdSkelTokens->classicLinear) {
                return _GetLBSSkinningComputeKernel();
            } else if (skinningMethod == UsdSkelTokens->dualQuaternion) {
                return _GetDQSSkinningComputeKernel();
            } else {
                TF_WARN("Unknown skinning method: '%s' ", skinningMethod.GetText());
                return std::string();
            }
        }
    }

    if (_IsSkinningInputAggregatorComputationPath(cachePath)) {
        return std::string();
    }

    return BaseAdapter::GetExtComputationKernel(prim, cachePath, 
                instancerContext);

}

void
UsdSkelImagingSkeletonAdapter::_UpdateBoneMeshForTime(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    _SkelData* skelData = _GetSkelData(cachePath);
    if (!TF_VERIFY(skelData)) {
        return;
    }

    TF_DEBUG(USDIMAGING_CHANGES).Msg("[UpdateForTime] Skeleton path: <%s>\n",
                                     prim.GetPath().GetText());
    TF_DEBUG(USDIMAGING_CHANGES).Msg("[UpdateForTime] Cache path: <%s>\n",
                                     cachePath.GetText());

    if (requestedBits & HdChangeTracker::DirtyPrimvar) {
        UsdImagingPrimvarDescCache* primvarDescCache = _GetPrimvarDescCache();

        // Expose points as a primvar.
        _MergePrimvar(&primvarDescCache->GetPrimvars(cachePath),
                      HdTokens->points,
                      HdInterpolationVertex,
                      HdPrimvarRoleTokens->point);
        _MergePrimvar(&primvarDescCache->GetPrimvars(cachePath),
                      HdTokens->displayColor,
                      HdInterpolationConstant,
                      HdPrimvarRoleTokens->color);
        _MergePrimvar(&primvarDescCache->GetPrimvars(cachePath),
                      HdTokens->displayOpacity,
                      HdInterpolationConstant);
    }
}


// ---------------------------------------------------------------------- //
/// Common utitily methods for skinning computations & skinned prims
// ---------------------------------------------------------------------- //
bool
UsdSkelImagingSkeletonAdapter::_IsAffectedByTimeVaryingSkelAnim(
    const SdfPath& skinnedPrimPath) const
{
    const _SkinnedPrimData* skinnedPrimData =
        _GetSkinnedPrimData(skinnedPrimPath);
    if (!TF_VERIFY(skinnedPrimData)) {
        return false;
    }

    const _SkelData* skelData = _GetSkelData(skinnedPrimData->skelPath);
    if (!TF_VERIFY(skelData)) {
        return false;
    }

    // Discover time-varying joint transforms.
    if (const UsdSkelAnimQuery& animQuery =
        skelData->skelQuery.GetAnimQuery()) {

        return (skinnedPrimData->hasJointInfluences &&
                animQuery.JointTransformsMightBeTimeVarying()) ||
               (skinnedPrimData->blendShapeQuery &&
                animQuery.BlendShapeWeightsMightBeTimeVarying());
    }
    return false;
}

void
UsdSkelImagingSkeletonAdapter::_RemoveSkinnedPrimAndComputations(
    const SdfPath& cachePath,
    UsdImagingIndexProxy* index)
{
    TF_DEBUG(USDIMAGING_CHANGES).Msg(
                "[SkeletonAdapter::_RemovePrim] Remove skinned prim %s and its "
                "computations.\n", cachePath.GetText());
    
    // Remove skinned prim.
    index->RemoveRprim(cachePath);

    // Remove the computations it participates in.
    SdfPath compPath = _GetSkinningComputationPath(cachePath);
    index->RemoveSprim(HdPrimTypeTokens->extComputation, compPath);
    
    SdfPath aggrCompPath =
        _GetSkinningInputAggregatorComputationPath(cachePath);
    index->RemoveSprim(HdPrimTypeTokens->extComputation, aggrCompPath);

    // Clear cache entry.
    _skinnedPrimDataCache.erase(cachePath);
}

// ---------------------------------------------------------------------- //
/// Handlers for the skinning computations
// ---------------------------------------------------------------------- //
SdfPath
UsdSkelImagingSkeletonAdapter::_GetSkinningComputationPath(
    const SdfPath& skinnedPrimPath) const
{
    return skinnedPrimPath.AppendChild(
        UsdSkelImagingExtComputationNameTokens->computation);
}


SdfPath
UsdSkelImagingSkeletonAdapter::_GetSkinningInputAggregatorComputationPath(
    const SdfPath& skinnedPrimPath) const
{
    return skinnedPrimPath.AppendChild(
        UsdSkelImagingExtComputationNameTokens->aggregatorComputation);
}


bool
UsdSkelImagingSkeletonAdapter::_IsSkinningComputationPath(
    const SdfPath& cachePath) const
{
    return cachePath.GetName() == UsdSkelImagingExtComputationNameTokens
                                      ->computation;
}


bool
UsdSkelImagingSkeletonAdapter::_IsSkinningInputAggregatorComputationPath(
    const SdfPath& cachePath) const
{
    return cachePath.GetName() == UsdSkelImagingExtComputationNameTokens
                                      ->aggregatorComputation;
}


void
UsdSkelImagingSkeletonAdapter::_TrackSkinningComputationVariability(
    const UsdPrim& skinnedPrim,
    const SdfPath& computationPath,
    HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // XXX: We don't receive the "cachePath" for the skinned prim, and so
    // the method below won't work when using multiple UsdImagingDelgate's.
    SdfPath skinnedPrimCachePath = ResolveCachePath(
            skinnedPrim.GetPath(), instancerContext);
    
    if (_IsAffectedByTimeVaryingSkelAnim(skinnedPrimCachePath)) {
        (*timeVaryingBits) |= HdExtComputation::DirtySceneInput;
        HD_PERF_COUNTER_INCR(UsdImagingTokens->usdVaryingPrimvar);
    }

    // XXX: Issue warnings for computation inputs that we don't expect to be 
    // time varying.
}


VtVec3fArray
UsdSkelImagingSkeletonAdapter::_GetSkinnedPrimPoints(
    const UsdPrim& skinnedPrim,
    const SdfPath& skinnedPrimCachePath,
    UsdTimeCode time) const
{
    // Since only UsdGeomBased-type prims can be targeted by a skeleton,
    // we expect the skinned prim adapter to derive from GprimAdapter.
    UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(skinnedPrim);
    auto gprimAdapter =
        std::dynamic_pointer_cast<UsdImagingGprimAdapter> (adapter);
    if (!TF_VERIFY(gprimAdapter)) {
        return VtVec3fArray();
    }

    VtValue points = gprimAdapter->GetPoints(skinnedPrim, time);
    if (!TF_VERIFY(points.IsHolding<VtVec3fArray>())) {
        return VtVec3fArray();
    }
    return points.UncheckedGet<VtVec3fArray>();
}


/* static */
std::string
UsdSkelImagingSkeletonAdapter::_LoadSkinningComputeKernel(const TfToken& kernelKey)
{
    TRACE_FUNCTION();
    HioGlslfx gfx(UsdSkelImagingPackageSkinningShader());
    if (!gfx.IsValid()) {
        TF_CODING_ERROR("Couldn't load UsdImagingGLPackageSkinningShader");
        return std::string();
    }

    std::string shaderSource = gfx.GetSource(kernelKey);
    if (!TF_VERIFY(!shaderSource.empty())) {
        TF_WARN("Skinning compute shader is missing kernel '%s'",
                kernelKey.GetText());
        return std::string();
    }

    TF_DEBUG(HD_EXT_COMPUTATION_UPDATED).Msg(
        "Kernel for skinning is :\n%s\n", shaderSource.c_str());
    return shaderSource;
}


/* static */
const std::string&
UsdSkelImagingSkeletonAdapter::_GetLBSSkinningComputeKernel()
{
    static const std::string shaderSource(_LoadSkinningComputeKernel(_tokens->skinPointsLBSKernel));
    return shaderSource;
}

/* static */
const std::string&
UsdSkelImagingSkeletonAdapter::_GetDQSSkinningComputeKernel()
{
    static const std::string shaderSource(_LoadSkinningComputeKernel(_tokens->skinPointsDQSKernel));
    return shaderSource;
}

// ---------------------------------------------------------------------- //
/// Handlers for the skinned prim
// ---------------------------------------------------------------------- //

bool
UsdSkelImagingSkeletonAdapter::_IsSkinnedPrimPath(
    const SdfPath& cachePath) const
{
    if (_skinnedPrimDataCache.find(cachePath) != _skinnedPrimDataCache.end()) {
        return true;
    }
    return false;
}


void
UsdSkelImagingSkeletonAdapter::_TrackSkinnedPrimVariability(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    HdDirtyBits* timeVaryingBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // Since The SkeletonAdapter hijacks skinned prims (see SkelRootAdapter),
    // make sure to delegate to the actual adapter registered for the prim.
    UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(prim);
    adapter->TrackVariability(prim, cachePath,
                              timeVaryingBits, instancerContext);

    if (_IsAffectedByTimeVaryingSkelAnim(cachePath)) {
        (*timeVaryingBits) |= HdChangeTracker::DirtyPoints;
        HD_PERF_COUNTER_INCR(UsdImagingTokens->usdVaryingPrimvar);
    }
}


void
UsdSkelImagingSkeletonAdapter::_UpdateSkinnedPrimForTime(
    const UsdPrim& prim,
    const SdfPath& cachePath,
    UsdTimeCode time,
    HdDirtyBits requestedBits,
    const UsdImagingInstancerContext* instancerContext) const
{
    // For readability
    UsdPrim const& skinnedPrim = prim;
    SdfPath const& skinnedPrimPath = cachePath;

    TF_DEBUG(USDIMAGING_CHANGES).Msg(
        "[UpdateForTime] Skinned prim path: <%s>\n", prim.GetPath().GetText());
    TF_DEBUG(USDIMAGING_CHANGES).Msg
        ("[UpdateForTime] Cache path: <%s>\n", cachePath.GetText());

    // Suppress the dirtybit for points, so we don't publish 'points' as a
    // primvar. Also suppressing normals: normals will instead be computed
    // post-skinning, as if they were unauthored (since GPU normal skinning
    // is not yet supported).
    requestedBits &= ~(HdChangeTracker::DirtyPoints|
                       HdChangeTracker::DirtyNormals);

    // Since The SkeletonAdapter hijacks skinned prims (see SkelRootAdapter),
    // make sure to delegate to the actual adapter registered for the prim.
    UsdImagingPrimAdapterSharedPtr adapter = _GetPrimAdapter(skinnedPrim);
    adapter->UpdateForTime(skinnedPrim, skinnedPrimPath,
                           time, requestedBits, instancerContext);

    
    // Don't publish skinning related primvars since they're consumed only by
    // the computations.
    // XXX: The usage of elementSize for jointWeights/Indices primvars to have
    // multiple values per-vertex is not supported yet in Hydra.
    if (requestedBits & HdChangeTracker::DirtyPrimvar) {
        UsdImagingPrimvarDescCache* primvarDescCache = _GetPrimvarDescCache();
        HdPrimvarDescriptorVector& primvars =
            primvarDescCache->GetPrimvars(skinnedPrimPath);
        for (auto it = primvars.begin(); it != primvars.end(); ) {
            if (it->name == _tokens->skelJointIndices ||
                it->name == _tokens->skelJointWeights  ||
                it->name == _tokens->skelSkinningMethod ||
                it->name == _tokens->skelGeomBindXform) {
                it = primvars.erase(it);
            } else {
                ++it;
            }
        }
    }
}

TfTokenVector
UsdSkelImagingSkeletonAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
UsdSkelImagingSkeletonAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (!subprim.IsEmpty()) {
        return {};
    }

    return UsdSkelImagingPrimTypeTokens->skeleton;
}

HdContainerDataSourceHandle
UsdSkelImagingSkeletonAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (!subprim.IsEmpty()) {
        return nullptr;
    }

    return UsdSkelImagingDataSourceSkeletonPrim::New(
            prim.GetPath(), prim, stageGlobals);
}

HdDataSourceLocatorSet
UsdSkelImagingSkeletonAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (!subprim.IsEmpty()) {
        return {};
    }

    return UsdSkelImagingDataSourceSkeletonPrim::Invalidate(
        prim, subprim,properties, invalidationType);
}

// ---------------------------------------------------------------------- //
/// _SkelData
// ---------------------------------------------------------------------- //

UsdSkelImagingSkeletonAdapter::_SkelData*
UsdSkelImagingSkeletonAdapter::_GetSkelData(const SdfPath& cachePath) const
{
    auto it = _skelDataCache.find(cachePath);
    return it != _skelDataCache.end() ? it->second.get() : nullptr;
}


HdMeshTopology
UsdSkelImagingSkeletonAdapter::_SkelData::ComputeTopologyAndRestState()
{
    HdMeshTopology meshTopology;

    size_t numPoints = 0;
    UsdSkelImagingComputeBoneTopology(skelQuery.GetTopology(),
                                      &meshTopology,
                                      &numPoints);

    // While computing topology, we also compute the 'rest pose'
    // of the bone mesh, along with joint influences.
    VtMatrix4dArray xforms; 
    skelQuery.GetJointWorldBindTransforms(&xforms);

    _numJoints = xforms.size();

    const VtMatrix4dArray &xformsConst = xforms.AsConst();
    UsdSkelImagingComputeBonePoints(skelQuery.GetTopology(), xformsConst,
                                    numPoints, &_boneMeshPoints);

    UsdSkelImagingComputeBoneJointIndices(skelQuery.GetTopology(),
                                          &_boneMeshJointIndices, numPoints);

    // Transform points by their inverse bind transforms. This puts bone points
    // in the right space so that when we compute bone points on frame changes,
    // we only need to consider joint transforms (and can disregard bind
    // transforms). This is only possible since each point of the mesh is 
    // influenced by only one joint.
    if (TF_VERIFY(_boneMeshPoints.size() == _boneMeshJointIndices.size())) {

        for (auto& xf : xforms) {
            xf = xf.GetInverse();
        }

        const GfMatrix4d* invBindXforms = xforms.cdata();
        const int* jointIndices = _boneMeshJointIndices.cdata();
        GfVec3f* points = _boneMeshPoints.data();
        for (size_t i = 0; i < _boneMeshPoints.size(); ++i) {
            int jointIdx = jointIndices[i];
            TF_DEV_AXIOM(jointIdx >= 0 &&
                         static_cast<size_t>(jointIdx) < xforms.size());
            points[i] = GfVec3f(invBindXforms[jointIdx].Transform(points[i]));
        }
    }

    return meshTopology;
}


VtVec3fArray
UsdSkelImagingSkeletonAdapter::_SkelData::ComputePoints(
    UsdTimeCode time) const
{
    // Initial bone points were stored pre-transformed by the *inverse* world
    // bind transforms. To correctly position/orient them, we simply need to
    // transform each bone point by the corresponding skel-space joint
    // transform.
    VtMatrix4dArray xforms;
    if (skelQuery.ComputeJointSkelTransforms(&xforms, time)) {
        if (xforms.size() != _numJoints) {
            TF_WARN("Size of computed xforms [%zu] != expected num "
                    "joints [%zu].", xforms.size(), _numJoints);
            return _boneMeshPoints;
        }

        if(TF_VERIFY(_boneMeshPoints.size() == _boneMeshJointIndices.size())) {
            // TODO: MakeUnique()
            VtVec3fArray skinnedPoints(_boneMeshPoints);

            const int* jointIndices = _boneMeshJointIndices.cdata();
            const GfMatrix4d* jointXforms = xforms.cdata();
            GfVec3f* points = skinnedPoints.data();

            for(size_t pi = 0; pi < skinnedPoints.size(); ++pi) {
                int jointIdx = jointIndices[pi];

                TF_DEV_AXIOM(jointIdx >= 0 &&
                             static_cast<size_t>(jointIdx) < xforms.size());

                // XXX: Joint transforms in UsdSkel are required to be
                // affine, so this is safe!
                points[pi] =
                    GfVec3f(jointXforms[jointIdx].TransformAffine(points[pi]));
            }
            return skinnedPoints;
        }
    }
    return _boneMeshPoints;
}

namespace {
// XXX The functions below are duplicated from UsdGeomImageable.

// Helper for computing only the authored purpose token from a valid imageable
// prim. Returns an empty purpose token otherwise.
static TfToken
_ComputeAuthoredPurpose(const UsdGeomImageable &ip)
{
    if (ip) {
        UsdAttribute purposeAttr = ip.GetPurposeAttr();
        if (purposeAttr.HasAuthoredValue()) {
            TfToken purpose;
            purposeAttr.Get(&purpose);
            return purpose;
        }
    }
    return TfToken();
}

// Helper for computing the purpose that can be inherited from an ancestor 
// imageable when there is no authored purpose on the prim. Walks up the prim
// hierarchy and returns the first authored purpose opinion found on an 
// imageable prim. Returns an empty token if there's purpose opinion to inherit
// from.
static TfToken
_ComputeInheritableAncestorPurpose(const UsdPrim &prim)
{
    UsdPrim parent = prim.GetParent();
    while (parent) {
        const TfToken purpose =
            _ComputeAuthoredPurpose(UsdGeomImageable(parent));
        if (!purpose.IsEmpty()) {
            return purpose;
        }
        parent = parent.GetParent();
    }
    return TfToken();
}

}

TfToken
UsdSkelImagingSkeletonAdapter::_SkelData::ComputePurpose() const
{
    HD_TRACE_FUNCTION();

    const UsdSkelSkeleton &skel = skelQuery.GetSkeleton();

    TfToken purpose = _ComputeAuthoredPurpose(skel);
    if (purpose.IsEmpty()) {
        purpose = _ComputeInheritableAncestorPurpose(skel.GetPrim());
    }
    // Note that we don't fallback to "default" purrpose here.
    return purpose;

    // XXX Ideally, we'd want the UsdSkelSkeleton schema to fallback to purpose
    //     "guide" rather than "default" and just do:
    // return skelQuery.GetSkeleton().ComputePurpose();
}


// ---------------------------------------------------------------------- //
/// _SkinnedPrimData
// ---------------------------------------------------------------------- //

const UsdSkelImagingSkeletonAdapter::_SkinnedPrimData*
UsdSkelImagingSkeletonAdapter::_GetSkinnedPrimData(
    const SdfPath& cachePath) const
{
    auto it = _skinnedPrimDataCache.find(cachePath);
    return it != _skinnedPrimDataCache.end() ? &it->second : nullptr;
}

UsdSkelImagingSkeletonAdapter::_SkinnedPrimData::_SkinnedPrimData(
    const SdfPath& skelPath,
    const UsdSkelSkeletonQuery& skelQuery,
    const UsdSkelSkinningQuery& skinningQuery,
    const SdfPath& skelRootPath)
    : skinningQuery(skinningQuery),
      animQuery(skelQuery.GetAnimQuery()),
      skelPath(skelPath),
      skelRootPath(skelRootPath),
      hasJointInfluences(skinningQuery.HasJointInfluences())
{
    if (skinningQuery.HasBlendShapes() && skelQuery.GetAnimQuery()) {
        blendShapeQuery = std::make_shared<UsdSkelBlendShapeQuery>(
            UsdSkelBindingAPI(skinningQuery.GetPrim()));
    }
}


PXR_NAMESPACE_CLOSE_SCOPE
