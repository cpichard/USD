//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdSt/instancer.h"

#include "pxr/imaging/hdSt/drawItem.h"
#include "pxr/imaging/hdSt/primUtils.h"
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hd/debugCodes.h"
#include "pxr/imaging/hd/rprimSharedData.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include "pxr/imaging/hgi/capabilities.h"

PXR_NAMESPACE_OPEN_SCOPE


HdStInstancer::HdStInstancer(HdSceneDelegate* delegate,
                             SdfPath const &id)
    : HdInstancer(delegate, id)
    , _instancePrimvarNumElements(0)
    , _visible(true)
{
}

void
HdStInstancer::Sync(HdSceneDelegate *sceneDelegate,
                    HdRenderParam   *renderParam,
                    HdDirtyBits     *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& instancerId = GetId();

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _visible = sceneDelegate->GetVisible(instancerId);
    }

    _UpdateInstancer(sceneDelegate, dirtyBits);
    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, instancerId)) {
        _SyncPrimvars(sceneDelegate, dirtyBits);
    }
}

void
HdStInstancer::_SyncPrimvars(HdSceneDelegate *sceneDelegate,
                             HdDirtyBits *dirtyBits)
{
    SdfPath const& instancerId = GetId();

    HdPrimvarDescriptorVector primvars =
        HdStGetInstancerPrimvarDescriptors(this, sceneDelegate);

    HdBufferSourceSharedPtrVector sources;
    sources.reserve(primvars.size());

    HdStResourceRegistrySharedPtr const& resourceRegistry =
        std::static_pointer_cast<HdStResourceRegistry>(
        sceneDelegate->GetRenderIndex().GetResourceRegistry());

    // Reset _instancePrimvarNumElements, in case the number of instances
    // is varying.
    _instancePrimvarNumElements= 0;

    for (HdPrimvarDescriptor const& primvar: primvars) {
        VtValue value = sceneDelegate->Get(instancerId, primvar.name);
        if (!value.IsEmpty()) {
            HdBufferSourceSharedPtr source;
            if (primvar.name == HdInstancerTokens->instanceTransforms) {
                if (value.IsHolding<VtArray<GfMatrix4d>>()) {
                    // Explicitly invoke the c'tor taking a
                    // VtArray<GfMatrix4d> to ensure we properly convert to
                    // the appropriate floating-point matrix type.
                    HgiCapabilities const * capabilities =
                        resourceRegistry->GetHgi()->GetCapabilities();
                    bool const doublesSupported = capabilities->IsSet(
                        HgiDeviceCapabilitiesBitsShaderDoublePrecision);
                    source.reset(new HdVtBufferSource(
                        primvar.name,
                        value.UncheckedGet<VtArray<GfMatrix4d>>(),
                        1,
                        doublesSupported));
                }
                else if (value.IsHolding<VtArray<GfMatrix4f>>()) {
                    source.reset(new HdVtBufferSource(
                        primvar.name,
                        value,
                        1,
                        false));
                }
            }
            else {
                source.reset(new HdVtBufferSource(primvar.name, value));
            }

            // This is a defensive check, but ideally we would not be sent
            // empty arrays from the client. Once UsdImaging can fulfill
            // this contract efficiently, this check should emit a coding
            // error.
            if (source->GetNumElements() == 0) {
                continue;
            }

            // Latch onto the first numElements we see.
            size_t numElements = source->GetNumElements();
            if (_instancePrimvarNumElements== 0) {
                _instancePrimvarNumElements = numElements;
            }

            if (numElements != _instancePrimvarNumElements) {
                // This primvar buffer is in a bad state; we can't have
                // different numbers of instances per primvar.  Trim to the
                // lower value.  Note: later on, we also trim the instance
                // indices to be in this smaller range.
                //
                // This is recovery code; the scene delegate shouldn't let
                // us get here...
                TF_WARN("Inconsistent number of '%s' values "
                        "(%zu vs %zu) for <%s>.",
                        primvar.name.GetText(),
                        source->GetNumElements(),
                        _instancePrimvarNumElements,
                        instancerId.GetText());
                _instancePrimvarNumElements
                    = std::min(numElements, _instancePrimvarNumElements);
            }

            sources.push_back(source);
        }
    }

    if (!HdStCanSkipBARAllocationOrUpdate(
         sources, _instancePrimvarRange, *dirtyBits)) {
        // XXX: This should be based off the DirtyPrimvarDesc bit.
        bool hasDirtyPrimvarDesc = (*dirtyBits & HdChangeTracker::DirtyPrimvar);
        HdBufferSpecVector bufferSpecs;
        HdBufferSpec::GetBufferSpecs(sources, &bufferSpecs);

        HdBufferSpecVector removedSpecs;
        if (hasDirtyPrimvarDesc) {
            TfTokenVector internallyGeneratedPrimvars; // none
            removedSpecs = HdStGetRemovedOrReplacedPrimvarBufferSpecs(
                _instancePrimvarRange, primvars,
                internallyGeneratedPrimvars, bufferSpecs, instancerId);
        }
        
        // Update local primvar range.
        _instancePrimvarRange =
            resourceRegistry->UpdateNonUniformBufferArrayRange(
                HdTokens->primvar, _instancePrimvarRange, bufferSpecs,
                removedSpecs, HdBufferArrayUsageHintBitsStorage);

        TF_VERIFY(_instancePrimvarRange->IsValid());

        // schedule to sync gpu
        if (!sources.empty()) {
            resourceRegistry->AddSources(
                _instancePrimvarRange, std::move(sources));
        }
    }
}

void
HdStInstancer::_GetInstanceIndices(
    const SdfPath &prototypeId,
    std::vector<VtIntArray> * const instanceIndicesArray)
{
    SdfPath const &instancerId = GetId();

    VtIntArray instanceIndices;

    if (_visible) {
        instanceIndices =
            GetDelegate()->GetInstanceIndices(instancerId, prototypeId);

        // quick sanity check
        // instance indices should not exceed the size of instance primvars.
        for (auto it = instanceIndices.cbegin();
             it != instanceIndices.cend(); ++it) {
            if (*it >= (int)_instancePrimvarNumElements) {
                TF_WARN("Instance index exceeds the element count of instance "
                        "primvars (%d >= %zu) for <%s>",
                        *it, _instancePrimvarNumElements,
                        instancerId.GetText());
                instanceIndices.clear();
                if (_instancePrimvarNumElements > 0) {
                    // insert 0-th index as placeholder (0th should always
                    // exist, since we don't populate instance primvars with
                    // numElements == 0).
                    instanceIndices.push_back(0);
                }
                break;
            }
        }
    }

    instanceIndicesArray->push_back(instanceIndices);

    if (TfDebug::IsEnabled(HD_INSTANCER_UPDATED)) {
        std::stringstream ss;
        ss << instanceIndices;
        TF_DEBUG(HD_INSTANCER_UPDATED).Msg("GetInstanceIndices for proto <%s> "
            "instancer <%s> (parent: <%s>): %s\n",
            prototypeId.GetText(),
            instancerId.GetText(),
            GetParentId().GetText(),
            ss.str().c_str());
    }

    // backtrace the instancer hierarchy to gather all instance indices.
    if (!GetParentId().IsEmpty()) {
        HdInstancer *parentInstancer =
            GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
        if (TF_VERIFY(parentInstancer)) {
            static_cast<HdStInstancer*>(parentInstancer)->
                _GetInstanceIndices(instancerId, instanceIndicesArray);
        }
    }
}

VtIntArray
HdStInstancer::GetInstanceIndices(SdfPath const &prototypeId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // delegate provides sparse index array for prototypeId.
    std::vector<VtIntArray> instanceIndicesArray;
    _GetInstanceIndices(prototypeId, &instanceIndicesArray);
    int instancerNumLevels = (int)instanceIndicesArray.size();

    if (!TF_VERIFY(instancerNumLevels > 0)) {
        return VtIntArray();
    }

    // create the cartesian product of instanceIndices array. Each tuple is
    // preceded by a global instance index <n>.
    // e.g.
    //   input   : [0,1] [3,4,5] [7,8]
    //   output  : [<0>,0,3,7,  <1>,1,3,7,  <2>,0,4,7,  <3>,1,4,7,
    //              <4>,0,5,7,  <5>,1,5,7,  <6>,0,3,8, ...]

    size_t nTotal = 1;
    for (int i = 0; i < instancerNumLevels; ++i) {
        nTotal *= instanceIndicesArray[i].size();
    }
    int instanceIndexWidth = 1 + instancerNumLevels;

    VtIntArray instanceIndices(nTotal * instanceIndexWidth);
    std::vector<int> currents(instancerNumLevels);
    for (size_t j = 0; j < nTotal; ++j) {
        instanceIndices[j*instanceIndexWidth] = j; // global idx
        for (int i = 0; i < instancerNumLevels; ++i) {
            instanceIndices[j*instanceIndexWidth + i + 1] =
                instanceIndicesArray[i].cdata()[currents[i]];
        }
        ++currents[0];
        for (int i = 0; i < instancerNumLevels-1; ++i) {
            if (static_cast<size_t>(currents[i]) >=
                    instanceIndicesArray[i].size()) {
                ++currents[i+1];
                currents[i] = 0;
            }
        }
    }

    if (TfDebug::IsEnabled(HD_INSTANCER_UPDATED)) {
        std::stringstream ss;
        ss << instanceIndices;
        TF_DEBUG(HD_INSTANCER_UPDATED).Msg("Flattened indices <%s>: %s\n",
            prototypeId.GetText(),
            ss.str().c_str());
    }

    return instanceIndices;
}

PXR_NAMESPACE_CLOSE_SCOPE

