//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdExecImaging/stageSceneIndex.h"

#include "pxr/base/tf/refPtr.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdExecImaging_StageSceneIndexRefPtr
UsdExecImaging_StageSceneIndex::New()
{
    return TfCreateRefPtr(new UsdExecImaging_StageSceneIndex);
}

UsdExecImaging_StageSceneIndex::UsdExecImaging_StageSceneIndex() = default;

UsdExecImaging_StageSceneIndex::~UsdExecImaging_StageSceneIndex() = default;

HdSceneIndexPrim
UsdExecImaging_StageSceneIndex::GetPrim(const SdfPath &primPath) const
{
    return {};
}

SdfPathVector
UsdExecImaging_StageSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return {};
}

void
UsdExecImaging_StageSceneIndex::SetStage(UsdStageRefPtr stage)
{
}

void
UsdExecImaging_StageSceneIndex::SetTime(UsdTimeCode time)
{
}

void
UsdExecImaging_StageSceneIndex::ApplyPendingUpdates()
{
}

PXR_NAMESPACE_CLOSE_SCOPE
