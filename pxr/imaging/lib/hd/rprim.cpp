//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/hd/rprim.h"

#include "pxr/imaging/hd/bufferSpec.h"
#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/computation.h"
#include "pxr/imaging/hd/drawItem.h"
#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/instancer.h"
#include "pxr/imaging/hd/instanceRegistry.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/repr.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include "pxr/base/tf/envSetting.h"

#include "pxr/base/arch/hash.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_ENV_SETTING(HD_ENABLE_SHARED_VERTEX_PRIMVAR, 1,
                      "Enable sharing of vertex primvar");

HdRprim::HdRprim(SdfPath const& id,
                 SdfPath const& instancerId)
    : _instancerId(instancerId)
    , _materialId()
    , _sharedData(HdDrawingCoord::DefaultNumSlots,
                  /*visible=*/true)
{
    _sharedData.rprimID = id;
}

HdRprim::~HdRprim()
{
    /*NOTHING*/
}

// -------------------------------------------------------------------------- //
///                 Rprim Hydra Engine API : Pre-Sync & Sync-Phase
// -------------------------------------------------------------------------- //

bool
HdRprim::CanSkipDirtyBitPropagationAndSync(HdDirtyBits bits) const
{
    // For invisible prims, we'd like to avoid syncing data, which involves:
    // (a) the scene delegate pulling data post dirty-bit propagation 
    // (b) the rprim processing its dirty bits and
    // (c) the rprim committing resource updates to the GPU
    // 
    // However, the current design adds a draw item for a repr during repr 
    // initialization (see _InitRepr) even if a prim may be invisible, which
    // requires us go through the sync process to avoid tripping other checks.
    // 
    // XXX: We may want to avoid this altogether, or rethink how we approach
    // the two workflow scenarios:
    // ( i) objects that are always invisible (i.e., never loaded by the user or
    // scene)
    // (ii) vis-invis'ing objects
    //  
    // For now, we take the hit of first repr initialization (+ sync) and avoid
    // time-varying updates to the invisible prim.
    // 
    // Note: If the sync is skipped, the dirty bits in the change tracker
    // remain the same.
    bool skip = false;

    HdDirtyBits mask = (HdChangeTracker::DirtyVisibility |
                        HdChangeTracker::NewRepr);

    if (!IsVisible() && !(bits & mask)) {
        // By setting the propagated dirty bits to Clean, we effectively 
        // disable delegate and rprim sync
        skip = true;
        HD_PERF_COUNTER_INCR(HdPerfTokens->skipInvisibleRprimSync);
    }

    return skip;
}

HdDirtyBits
HdRprim::PropagateRprimDirtyBits(HdDirtyBits bits)
{
    // If the dependent computations changed - assume all
    // primvars are dirty
    if (bits & HdChangeTracker::DirtyComputationPrimvarDesc) {
        bits |= (HdChangeTracker::DirtyPoints  |
                 HdChangeTracker::DirtyNormals |
                 HdChangeTracker::DirtyWidths  |
                 HdChangeTracker::DirtyPrimvar);
    }

    // when refine level changes, topology becomes dirty.
    // XXX: can we remove DirtyDisplayStyle then?
    if (bits & HdChangeTracker::DirtyDisplayStyle) {
        bits |=  HdChangeTracker::DirtyTopology;
    }

    // if topology changes, all dependent bits become dirty.
    if (bits & HdChangeTracker::DirtyTopology) {
        bits |= (HdChangeTracker::DirtyPoints  |
                 HdChangeTracker::DirtyNormals |
                 HdChangeTracker::DirtyPrimvar);
    }

    // Let subclasses propagate bits
    return _PropagateDirtyBits(bits);
}

void
HdRprim::InitRepr(HdSceneDelegate* delegate,
                  TfToken const &reprToken,
                  HdDirtyBits *dirtyBits)
{
    // If _sharedData.instancerLevels == -1, it's uninitialized and we should
    // compute it now.
    if (_sharedData.instancerLevels == -1) {
        _sharedData.instancerLevels = HdInstancer::GetInstancerNumLevels(
            delegate->GetRenderIndex(), *this);
    }

    _InitRepr(reprToken, dirtyBits);
}

// -------------------------------------------------------------------------- //
///                 Rprim Hydra Engine API : Execute-Phase
// -------------------------------------------------------------------------- //
const HdRprim::HdDrawItemPtrVector*
HdRprim::GetDrawItems(TfToken const& reprToken) const
{
    HdReprSharedPtr repr = _GetRepr(reprToken);
    if (repr) {
        return &(repr->GetDrawItems());
    }
    return nullptr;
}

// -------------------------------------------------------------------------- //
///                     Rprim Hydra Engine API : Cleanup
// -------------------------------------------------------------------------- //
void
HdRprim::Finalize(HdRenderParam *renderParam)
{
}

// -------------------------------------------------------------------------- //
///                              Rprim Data API
// -------------------------------------------------------------------------- //
void
HdRprim::SetPrimId(int32_t primId)
{
    _primId = primId;
    // Don't set DirtyPrimID here, to avoid undesired variability tracking.
}

bool
HdRprim::IsDirty(HdChangeTracker &changeTracker) const
{
    return changeTracker.IsRprimDirty(GetId());
}

void
HdRprim::UpdateReprSelector(HdSceneDelegate* delegate,
                            HdDirtyBits *dirtyBits)
{
    SdfPath const& id = GetId();
    if (HdChangeTracker::IsReprDirty(*dirtyBits, id)) {
        _authoredReprSelector = delegate->GetReprSelector(id);
        *dirtyBits &= ~HdChangeTracker::DirtyRepr;
    }
}

// -------------------------------------------------------------------------- //
///                             Rprim Shared API
// -------------------------------------------------------------------------- //
HdReprSharedPtr const &
HdRprim::_GetRepr(TfToken const &reprToken) const
{
    _ReprVector::const_iterator reprIt =
        std::find_if(_reprs.begin(), _reprs.end(),
                     _ReprComparator(reprToken));
    if (reprIt == _reprs.end()) {
        TF_CODING_ERROR("_InitRepr() should be called for repr %s on prim %s.",
                        reprToken.GetText(), GetId().GetText());
        static const HdReprSharedPtr ERROR_RETURN;
        return ERROR_RETURN;
    }
    return reprIt->second;
}

void
HdRprim::_UpdateVisibility(HdSceneDelegate* delegate,
                           HdDirtyBits *dirtyBits)
{
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, GetId())) {
        _sharedData.visible = delegate->GetVisible(GetId());
    }
}

void 
HdRprim::_SetMaterialId(HdChangeTracker &changeTracker,
                        SdfPath const& materialId)
{
    if (_materialId != materialId) {
        _materialId = materialId;

        // The batches need to be verified and rebuilt, since a changed shader
        // may change aggregation.
        changeTracker.MarkBatchesDirty();
    }
}

VtMatrix4dArray
HdRprim::GetInstancerTransforms(HdSceneDelegate* delegate)
{
    SdfPath instancerId = _instancerId;
    VtMatrix4dArray transforms;

    HdRenderIndex &renderIndex = delegate->GetRenderIndex();

    while (!instancerId.IsEmpty()) {
        transforms.push_back(delegate->GetInstancerTransform(instancerId));
        HdInstancer *instancer = renderIndex.GetInstancer(instancerId);
        if (instancer) {
            instancerId = instancer->GetParentId();
        } else {
            instancerId = SdfPath();
        }
    }
    return transforms;
}

//
// De-duplicating and sharing immutable primvar data.
// 
// Primvar data is identified using a hash computed from the
// sources of the primvar data, of which there are generally
// two kinds:
//   - data provided by the scene delegate
//   - data produced by computations
// 
// Immutable and mutable buffer data is managed using distinct
// heaps in the resource registry. Aggregation of buffer array
// ranges within each heap is managed separately.
// 
// We attempt to balance the benefits of sharing vs efficient
// varying update using the following simple strategy:
//
//  - When populating the first repr for an rprim, allocate
//    the primvar range from the immutable heap and attempt
//    to deduplicate the data by looking up the primvarId
//    in the primvar instance registry.
//
//  - When populating an additional repr for an rprim using
//    an existing immutable primvar range, compute an updated
//    primvarId and allocate from the immutable heap, again
//    attempting to deduplicate.
//
//  - Otherwise, migrate the primvar data to the mutable heap
//    and abandon further attempts to deduplicate.
//
//  - The computation of the primvarId for an rprim is cumulative
//    and includes the new sources of data being committed
//    during each successive update.
//
//  - Once we have migrated a primvar allocation to the mutable
//    heap we will no longer spend time computing a primvarId.
//

/* static */
bool
HdRprim::_IsEnabledSharedVertexPrimvar()
{
    static bool enabled =
        (TfGetEnvSetting(HD_ENABLE_SHARED_VERTEX_PRIMVAR) == 1);
    return enabled;
}

uint64_t
HdRprim::_ComputeSharedPrimvarId(uint64_t baseId,
                                 HdBufferSourceVector const &sources,
                                 HdComputationVector const &computations) const
{
    size_t primvarId = baseId;
    for (HdBufferSourceSharedPtr const &bufferSource : sources) {
        size_t sourceId = bufferSource->ComputeHash();
        primvarId = ArchHash64((const char*)&sourceId,
                               sizeof(sourceId), primvarId);

        if (bufferSource->HasPreChainedBuffer()) {
            HdBufferSourceSharedPtr src = bufferSource->GetPreChainedBuffer();

            while (src) {
                size_t chainedSourceId = bufferSource->ComputeHash();
                primvarId = ArchHash64((const char*)&chainedSourceId,
                                       sizeof(chainedSourceId), primvarId);

                src = src->GetPreChainedBuffer();
            }
        }
    }

    HdBufferSpecVector bufferSpecs;
    HdBufferSpec::GetBufferSpecs(computations, &bufferSpecs);
    for (HdBufferSpec const &bufferSpec : bufferSpecs) {
        boost::hash_combine(primvarId, bufferSpec.name);
        boost::hash_combine(primvarId, bufferSpec.tupleType.type);
        boost::hash_combine(primvarId, bufferSpec.tupleType.count);
    }

    return primvarId;
}

PXR_NAMESPACE_CLOSE_SCOPE
