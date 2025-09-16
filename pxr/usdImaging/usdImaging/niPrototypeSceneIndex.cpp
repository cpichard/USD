//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdImaging/niPrototypeSceneIndex.h"

#include "pxr/usdImaging/usdImaging/prototypeSceneIndexUtils.h"
#include "pxr/usdImaging/usdImaging/usdPrimInfoSchema.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hd/instancedBySchema.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/refPtr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/vt/array.h"

#include "pxr/pxr.h"

#include <cstddef>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(
    UsdImaging_NiPrototypeSceneIndexTokens,
    USDIMAGING_NI_PROTOTYPE_SCENE_INDEX_TOKENS);

using namespace UsdImaging_PrototypeSceneIndexUtils;

namespace {

bool
_IsUsdInstance(HdContainerDataSourceHandle const &primSource)
{
    UsdImagingUsdPrimInfoSchema schema =
        UsdImagingUsdPrimInfoSchema::GetFromParent(primSource);
    HdPathDataSourceHandle const pathDs = schema.GetNiPrototypePath();
    if (!pathDs) {
        return false;
    }
    const SdfPath usdPrototypePath = pathDs->GetTypedValue(0.0f);
    return !usdPrototypePath.IsEmpty();
}

HdDataSourceBaseHandle
_ResetXformToIdentityDataSource()
{
    return
        HdXformSchema::Builder()
            .SetMatrix(
                HdRetainedTypedSampledDataSource<GfMatrix4d>::New(
                    GfMatrix4d(1.0)))
            .SetResetXformStack(
                HdRetainedTypedSampledDataSource<bool>::New(
                    true))
            .Build();
}

const HdContainerDataSourceHandle &
_UnderlaySource()
{
    static HdContainerDataSourceHandle const result =
        HdRetainedContainerDataSource::New(
            HdInstancedBySchema::GetSchemaToken(),
            UsdImaging_NiPrototypeSceneIndex::GetInstancedByDataSource());
    return result;
}

HdContainerDataSourceHandle
_PrototypeRootOverlaySource(const HdContainerDataSourceHandle &ds)
{
    static HdContainerDataSourceHandle const overlayDs =
        HdRetainedContainerDataSource::New(
            HdInstancedBySchema::GetSchemaToken(),
            UsdImaging_NiPrototypeSceneIndex::GetInstancedByDataSource(),
            // The prototypes should always be defined at the origin.
            HdXformSchema::GetSchemaToken(),
            _ResetXformToIdentityDataSource());
    return HdOverlayContainerDataSource::OverlayedContainerDataSources(
        overlayDs, ds);
}

}

UsdImaging_NiPrototypeSceneIndexRefPtr
UsdImaging_NiPrototypeSceneIndex::New(
    HdSceneIndexBaseRefPtr const &inputSceneIndex,
    const bool forNativePrototype,
    HdContainerDataSourceHandle const &prototypeRootOverlayDs)
{
    return TfCreateRefPtr(
        new UsdImaging_NiPrototypeSceneIndex(
            inputSceneIndex, forNativePrototype, prototypeRootOverlayDs));
}

UsdImaging_NiPrototypeSceneIndex::
UsdImaging_NiPrototypeSceneIndex(
    HdSceneIndexBaseRefPtr const &inputSceneIndex,
    const bool forNativePrototype,
    HdContainerDataSourceHandle const &prototypeRootOverlayDs)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
  , _forNativePrototype(forNativePrototype)
  , _prototypeRootOverlaySource(
      _PrototypeRootOverlaySource(prototypeRootOverlayDs))
{
}

/* static */
const SdfPath &
UsdImaging_NiPrototypeSceneIndex::GetInstancerPath()
{
    static const SdfPath path =
        SdfPath::AbsoluteRootPath()
            .AppendChild(
                UsdImaging_NiPrototypeSceneIndexTokens->instancer);
    return path;
}

/* static */
const SdfPath &
UsdImaging_NiPrototypeSceneIndex::GetPrototypePath()
{
    static const SdfPath path =
        GetInstancerPath()
            .AppendChild(
                UsdImaging_NiPrototypeSceneIndexTokens->prototype);
    return path;
}

/* static */
const HdDataSourceBaseHandle &
UsdImaging_NiPrototypeSceneIndex::GetInstancedByDataSource()
{
    using DataSource = HdRetainedTypedSampledDataSource<VtArray<SdfPath>>;

    static const HdDataSourceBaseHandle &ds =
        HdInstancedBySchema::Builder()
            .SetPaths(
                DataSource::New(
                    { UsdImaging_NiPrototypeSceneIndex::GetInstancerPath() }))
            .SetPrototypeRoots(
                DataSource::New(
                    { UsdImaging_NiPrototypeSceneIndex::GetPrototypePath() }))
            .Build();

    return ds;
}

HdSceneIndexPrim
UsdImaging_NiPrototypeSceneIndex::GetPrim(
    const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (!prim.dataSource) {
        return prim;
    }

    if (_IsUsdInstance(prim.dataSource)) {
        if (IsRenderablePrimType(prim.primType)) {
            prim.primType = TfToken();
        }
        return prim;
    }

    if (!_forNativePrototype) {
        return prim;
    }

    if (!primPath.HasPrefix(GetPrototypePath())) {
        return prim;
    }

    static const size_t n = GetPrototypePath().GetPathElementCount();

    if (primPath.GetPathElementCount() == n) {
        // primPath is /UsdNiInstancer/UsdNiPrototype

        prim.dataSource = HdOverlayContainerDataSource::New(
            _prototypeRootOverlaySource,
            prim.dataSource);
    } else {
        // primPath is an ancestor of /UsdNiInstancer/UsdNiPrototype

        prim.dataSource = HdOverlayContainerDataSource::New(
            prim.dataSource,
            _UnderlaySource());
    }

    return prim;
}

SdfPathVector
UsdImaging_NiPrototypeSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
UsdImaging_NiPrototypeSceneIndex::_PrimsAdded(
    const HdSceneIndexBase&,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    TRACE_FUNCTION();
    
    if (!_IsObserved()) {
        return;
    }
    
    std::vector<size_t> indicesToErasePrimType;

    {
        TRACE_SCOPE("Scanning entries");
        
        for (size_t i = 0; i < entries.size(); ++i) {
            const HdSceneIndexObserver::AddedPrimEntry &entry = entries[i];
            if (IsRenderablePrimType(entry.primType) &&
                _IsUsdInstance(
                    _GetInputSceneIndex()
                        ->GetPrim(entry.primPath)
                        .dataSource)) {
                indicesToErasePrimType.push_back(i);
            }
        }
    }

    if (indicesToErasePrimType.empty()) {
        _SendPrimsAdded(entries);
    } else {
        HdSceneIndexObserver::AddedPrimEntries newEntries(entries);
        for (const size_t index : indicesToErasePrimType) {
            newEntries[index].primType = TfToken();
        }
        _SendPrimsAdded(newEntries);
    }
}

void
UsdImaging_NiPrototypeSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase&,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    _SendPrimsDirtied(entries);
}

void
UsdImaging_NiPrototypeSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase&,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    _SendPrimsRemoved(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE
