//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

// USD-12324 regression test.
//
// A companion to usdImaging's pure-unit test
// testUsdImagingNiAggregationNonDescendantPaths.cpp, this test authors real
// .usda scenes with simple prims and runs the full UsdImagingCreateSceneIndices
// pipeline (which loads the usdSkelImaging scene-index plugin so that
// skelBinding participates in native-instance aggregation).  It then counts
// native-instance instancers in the aggregated scene index to confirm that
// structurally-equivalent native instances aggregate even when their binding
// data sources reference non-descendant paths.

#include "pxr/usdImaging/usdImaging/sceneIndices.h"

#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexPrimView.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/usd/usd/stage.h"

#include "pxr/base/tf/errorMark.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Open a stage and build the full scene index chain (including native
// instance aggregation and the usdSkelImaging plugin).  Returns the terminal
// scene index that clients consume.
HdSceneIndexBaseRefPtr
_LoadFinalSceneIndex(const std::string &usdFile)
{
    UsdStageRefPtr stage = UsdStage::Open(usdFile);
    if (!stage) {
        TF_CODING_ERROR("Failed to open stage <%s>.", usdFile.c_str());
        return nullptr;
    }

    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    return UsdImagingCreateSceneIndices(info).finalSceneIndex;
}

// Count instancer prims in the aggregated scene index.  Native instance
// aggregation emits one instancer per aggregated prototype (under
// /UsdNiPropagatedPrototypes/.../UsdNiInstancer).  The test scenes contain
// no PointInstancers, so every instancer prim here is a native-instance
// instancer and this count reflects how many distinct prototypes the
// instances aggregated into.
int
_CountInstancers(const HdSceneIndexBaseRefPtr &si)
{
    if (!si) {
        return -1;
    }

    int count = 0;
    for (const SdfPath &primPath : HdSceneIndexPrimView(si)) {
        const HdSceneIndexPrim prim = si->GetPrim(primPath);
        if (prim.primType == HdPrimTypeTokens->instancer) {
            ++count;
        }
    }
    return count;
}

bool
_CheckCount(const char *testName, const std::string &usdFile,
            const int expected)
{
    const HdSceneIndexBaseRefPtr si = _LoadFinalSceneIndex(usdFile);
    const int count = _CountInstancers(si);
    if (count != expected) {
        TF_CODING_ERROR(
            "%s: expected %d instancer(s) in %s, got %d",
            testName, expected, usdFile.c_str(), count);
        return false;
    }
    return true;
}

// Positive: two Models whose inherited skel:skeleton binds into a sibling
// native instance (Rig) aggregate into a single instancer.  One ModelProto
// instancer + one RigProto instancer = 2.
bool
TestSkelSiblingAggregates()
{
    return _CheckCount("TestSkelSiblingAggregates", "skelSibling.usda", 2);
}

// Negative: when the sibling Rigs are different prototypes, the two Models
// bind to distinct prototype-relative skeleton paths and must stay split.
// Two Model instancers + two distinct Rig instancers = 4.
bool
TestSkelSiblingNegative()
{
    return _CheckCount("TestSkelSiblingNegative",
                       "skelSiblingNegative.usda", 4);
}

// model:modelPath relocation: two identical-asset placements whose only
// differing hashed field would be the inherited ancestor model:modelPath
// (/World/A vs /World/B) aggregate once that field is relocated to a primvar.
bool
TestModelPathAggregates()
{
    return _CheckCount("TestModelPathAggregates", "modelPath.usda", 1);
}

} // namespace

int
main()
{
    TfErrorMark m;

    bool ok = true;
    ok &= TestSkelSiblingAggregates();
    ok &= TestSkelSiblingNegative();
    ok &= TestModelPathAggregates();

    if (!ok || !m.IsClean()) {
        return 1;
    }

    std::cout << "All tests passed." << std::endl;
    return 0;
}
