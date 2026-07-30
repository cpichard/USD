//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/usdImaging/usdImaging/niInstanceAggregationSceneIndex.h"
#include "pxr/usdImaging/usdImaging/usdPrimInfoSchema.h"

#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/retainedSceneIndex.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/base/tf/errorMark.h"

#include <functional>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Regression test for USD-12324

// Build a prim data source with __usdPrimInfo.niPrototypePath set, and
// optionally a second data source field keyed by bindingName.
HdContainerDataSourceHandle
_MakeInstancePrimSource(
    const SdfPath &protoPath,
    const TfToken &bindingName = {},
    const HdDataSourceBaseHandle &bindingValue = nullptr)
{
    HdContainerDataSourceHandle primInfo =
        UsdImagingUsdPrimInfoSchema::Builder()
            .SetNiPrototypePath(
                HdRetainedTypedSampledDataSource<SdfPath>::New(protoPath))
            .Build();
    if (bindingName.IsEmpty()) {
        return HdRetainedContainerDataSource::New(
            UsdImagingUsdPrimInfoSchema::GetSchemaToken(), primInfo);
    }
    return HdRetainedContainerDataSource::New(
        UsdImagingUsdPrimInfoSchema::GetSchemaToken(), primInfo,
        bindingName, bindingValue);
}

// Traverse the output scene and count instancer prims for the given prototype.
// Instancer paths have the form:
// /UsdNiPropagatedPrototypes/<bindingScope>/<prototypeName>/UsdNiInstancer
int
_CountInstancers(HdSceneIndexBase &si, const TfToken &prototypeName)
{
    int count = 0;
    std::function<void (const SdfPath &)> visit = [&](const SdfPath &p) {
        if (UsdImaging_NiInstanceAggregationSceneIndex
                ::GetPrototypeNameFromInstancerPath(p) == prototypeName) {
            ++count;
        }
        for (const SdfPath &child : si.GetChildPrimPaths(p)) {
            visit(child);
        }
    };
    visit(SdfPath::AbsoluteRootPath());
    return count;
}

// Case 2: DevCrowd holds a siblingRef to the sibling Rig native instance.
// Two instances (/A/DevCrowd, /B/DevCrowd) with different absolute Rig paths
// should aggregate to a single instancer once both paths are normalized to the
// same canonical prototype root.
bool
TestSiblingNativeInstance()
{
    const TfToken bindingName("siblingRef");
    const TfToken protoNameDevCrowd("__Proto_DevCrowd");
    const TfToken protoNameRig("__Proto_Rig");
    const SdfPath protoDevCrowdPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameDevCrowd);
    const SdfPath protoRigPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameRig);

    auto retained = HdRetainedSceneIndex::New();
    retained->AddPrims({
        { SdfPath("/A/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/A/Rig"))) },
        { SdfPath("/A/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath) },
        { SdfPath("/B/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/B/Rig"))) },
        { SdfPath("/B/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath) },
    });

    auto si = UsdImaging_NiInstanceAggregationSceneIndex::New(
        retained, /*forNativePrototype=*/ false, {bindingName});

    const int count = _CountInstancers(*si, protoNameDevCrowd);
    if (count != 1) {
        TF_CODING_ERROR(
            "TestSiblingNativeInstance: expected 1 instancer for %s, got %d",
            protoNameDevCrowd.GetText(), count);
        return false;
    }
    return true;
}

// Case 2 Sub-path: siblingRef points to a child of the sibling Rig native
// instance (e.g. /A/Rig/skeleton). The ancestor walk finds /A/Rig ->
// /__Proto_Rig; the translator then reroots /A/Rig/skeleton ->
// /__Proto_Rig/skeleton for both instances, yielding matching hashes.
bool
TestChildOfSiblingNativeInstance()
{
    const TfToken bindingName("siblingRef");
    const TfToken protoNameDevCrowd("__Proto_DevCrowd");
    const TfToken protoNameRig("__Proto_Rig");
    const SdfPath protoDevCrowdPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameDevCrowd);
    const SdfPath protoRigPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameRig);

    auto retained = HdRetainedSceneIndex::New();
    retained->AddPrims({
        { SdfPath("/A/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/A/Rig/skeleton"))) },
        { SdfPath("/A/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath) },
        { SdfPath("/A/Rig/skeleton"), TfToken(),
          HdRetainedContainerDataSource::New() },
        { SdfPath("/B/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/B/Rig/skeleton"))) },
        { SdfPath("/B/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath) },
        { SdfPath("/B/Rig/skeleton"), TfToken(),
          HdRetainedContainerDataSource::New() },
    });

    auto si = UsdImaging_NiInstanceAggregationSceneIndex::New(
        retained, /*forNativePrototype=*/ false, {bindingName});

    const int count = _CountInstancers(*si, protoNameDevCrowd);
    if (count != 1) {
        TF_CODING_ERROR(
            "TestChildOfSiblingNativeInstance: expected 1 instancer for %s, got %d",
            protoNameDevCrowd.GetText(), count);
        return false;
    }
    return true;
}

// Case 1 Ni: Rig's modelRef points to its parent, which is a native instance.
// /A and /B are both instances of /__Proto_ParentModel. The translator's
// ancestor walk reroots both /A and /B to /__Proto_ParentModel, making
// /A/Rig and /B/Rig produce the same binding hash. This exercises the
// non-descendant path whose nearest ancestor is itself a native instance.
bool
TestAncestorNativeInstance()
{
    const TfToken bindingName("modelRef");
    const TfToken protoNameCG("__Proto_ParentModel");
    const TfToken protoNameRig("__Proto_Rig");
    const SdfPath protoCGPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameCG);
    const SdfPath protoRigPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameRig);

    auto retained = HdRetainedSceneIndex::New();
    retained->AddPrims({
        { SdfPath("/A"), TfToken(),
          _MakeInstancePrimSource(protoCGPath) },
        { SdfPath("/A/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/A"))) },
        { SdfPath("/B"), TfToken(),
          _MakeInstancePrimSource(protoCGPath) },
        { SdfPath("/B/Rig"), TfToken(),
          _MakeInstancePrimSource(protoRigPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/B"))) },
    });

    auto si = UsdImaging_NiInstanceAggregationSceneIndex::New(
        retained, /*forNativePrototype=*/ false, {bindingName});

    const int count = _CountInstancers(*si, protoNameRig);
    if (count != 1) {
        TF_CODING_ERROR(
            "TestAncestorNativeInstance: expected 1 instancer for %s, got %d",
            protoNameRig.GetText(), count);
        return false;
    }
    return true;
}

// Negative case: two DevCrowd instances reference siblings with DIFFERENT
// prototype names. Their binding hashes must differ -> 2 separate instancers.
// Confirms the fix does not over-aggregate structurally distinct instances.
bool
TestDifferentSiblingPrototypes()
{
    const TfToken bindingName("siblingRef");
    const TfToken protoNameDevCrowd("__Proto_DevCrowd");
    const TfToken protoNameRigA("__Proto_RigA");
    const TfToken protoNameRigB("__Proto_RigB");
    const SdfPath protoDevCrowdPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameDevCrowd);
    const SdfPath protoRigAPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameRigA);
    const SdfPath protoRigBPath =
        SdfPath::AbsoluteRootPath().AppendChild(protoNameRigB);

    auto retained = HdRetainedSceneIndex::New();
    retained->AddPrims({
        { SdfPath("/A/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/A/RigTypeA"))) },
        { SdfPath("/A/RigTypeA"), TfToken(),
          _MakeInstancePrimSource(protoRigAPath) },
        { SdfPath("/B/DevCrowd"), TfToken(),
          _MakeInstancePrimSource(protoDevCrowdPath, bindingName,
              HdRetainedTypedSampledDataSource<SdfPath>::New(
                  SdfPath("/B/RigTypeB"))) },
        { SdfPath("/B/RigTypeB"), TfToken(),
          _MakeInstancePrimSource(protoRigBPath) },
    });

    auto si = UsdImaging_NiInstanceAggregationSceneIndex::New(
        retained, /*forNativePrototype=*/ false, {bindingName});

    const int count = _CountInstancers(*si, protoNameDevCrowd);
    if (count != 2) {
        TF_CODING_ERROR(
            "TestDifferentSiblingPrototypes: expected 2 instancers for %s, got %d",
            protoNameDevCrowd.GetText(), count);
        return false;
    }
    return true;
}

} // namespace

int
main()
{
    TfErrorMark m;
    bool ok = true;
    ok &= TestSiblingNativeInstance();
    ok &= TestChildOfSiblingNativeInstance();
    ok &= TestAncestorNativeInstance();
    ok &= TestDifferentSiblingPrototypes();
    if (!ok || !m.IsClean()) {
        return 1;
    }
    std::cout << "All tests passed." << std::endl;
    return 0;
}
