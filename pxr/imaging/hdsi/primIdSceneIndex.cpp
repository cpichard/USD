//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdsi/primIdSceneIndex.h"

#include "pxr/imaging/hd/primIdSchema.h"
#include "pxr/imaging/hd/sceneGlobalsSchema.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPrimView.h"
#include "pxr/imaging/hd/tokens.h"

#include "pxr/usd/sdf/pathTable.h"

#include "pxr/base/tf/stl.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/trace/trace.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace HdsiPrimIdSceneIndex_Impl
{

// The largest id we ever hand out to clients (unless there are
// 2^24 prims or more).
static constexpr uint32_t maxId = (1 << 24) - 1;

//
// Compact version of std::optional<uint32_t>.
//
// Assumes that that stored value is less than 2^31.
//
class _OptionalPrimId
{
public:
    _OptionalPrimId()
      : _value(-1) { }
    _OptionalPrimId(const uint32_t value)
      : _value(static_cast<int32_t>(value)) {}

    explicit operator bool() const { return _value >= 0; }

    uint32_t operator*() const { return static_cast<uint32_t>(_value); }

private:
    // Negative value indicates it is an empty optional.
    // Non-negative value stores the value.
    int32_t _value;
};

// Scene index state.
struct _PrimIdInfo
{
    // Prim path to optional prim Id.
    //
    // We do not add an entry for non-imageable prims.
    // However, the SdfPathTable adds ancestors of imageable prims
    // if necessary (which will be empty _OptionalPrimId's.
    //
    SdfPathTable<_OptionalPrimId> pathToId;

    // Inverse of the above path table.
    //
    // Can contain empty paths since prim ids might not be consecutive.
    std::vector<SdfPath> idToPath;
};

// Prim-level container data source for an imagable prim overlaying the
// the given input data source with the prim Id schema.
class _PrimDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimDataSource);

    TfTokenVector GetNames() override
    {
        TfTokenVector names =
            _inputDataSource ? _inputDataSource->GetNames() : TfTokenVector();
        if (std::find(names.begin(), names.end(),
                      HdPrimIdSchema::GetSchemaToken()) == names.end()) {
            names.push_back(HdPrimIdSchema::GetSchemaToken());
        }
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdPrimIdSchema::GetSchemaToken()) {
            return _ComputePrimIdDataSource();
        }
        if (_inputDataSource) {
            return _inputDataSource->Get(name);
        }
        return nullptr;
    }

private:
    _PrimDataSource(
        HdContainerDataSourceHandle const &inputDataSource,
        _PrimIdInfoSharedPtr const &primIdInfo,
        const SdfPath &primPath)
      : _inputDataSource(inputDataSource)
      , _primIdInfo(primIdInfo)
      , _primPath(primPath)
    {
    }

    // Looks up primId in table and puts it into HdPrimIdSchema.
    HdContainerDataSourceHandle _ComputePrimIdDataSource() const
    {
        TRACE_FUNCTION();

        const auto it = _primIdInfo->pathToId.find(_primPath);
        if (it == _primIdInfo->pathToId.end() || !it->second) {
            return nullptr;
        }
        return HdPrimIdSchema::Builder()
            .SetPrimId(
                HdRetainedTypedSampledDataSource<uint32_t>::New(*it->second))
            .Build();
    }

    HdContainerDataSourceHandle const _inputDataSource;
    _PrimIdInfoSharedPtr const _primIdInfo;
    const SdfPath _primPath;
};

// Reverse prim id to path table as data source.
class _PrimIdTableDataSource : public HdVectorDataSource
{
public:
    HD_DECLARE_DATASOURCE(_PrimIdTableDataSource);

    size_t GetNumElements() override
    {
        return _primIdInfo->idToPath.size();
    }

    HdDataSourceBaseHandle GetElement(const size_t element) override
    {
        if (element >= _primIdInfo->idToPath.size()) {
            return nullptr;
        }
        const SdfPath &path = _primIdInfo->idToPath[element];
        if (path.IsEmpty()) {
            return nullptr;
        }
        return HdRetainedTypedSampledDataSource<SdfPath>::New(path);
    }

private:
    _PrimIdTableDataSource(_PrimIdInfoSharedPtr const &primIdInfo)
      : _primIdInfo(primIdInfo)
    {
    }

    _PrimIdInfoSharedPtr const _primIdInfo;
};

} // namespace HdsiPrimIdSceneIndex_Impl

using namespace HdsiPrimIdSceneIndex_Impl;

HdsiPrimIdSceneIndex::HdsiPrimIdSceneIndex(
        HdSceneIndexBaseRefPtr const &inputScene)
  : HdSingleInputFilteringSceneIndexBase(inputScene)
  , _primIdInfo(std::make_shared<_PrimIdInfo>())
  , _rootPrimOverlayDataSource(
      HdRetainedContainerDataSource::New(
          HdSceneGlobalsSchema::GetSchemaToken(),
          HdSceneGlobalsSchema::Builder()
              .SetPrimIdToPath(_PrimIdTableDataSource::New(_primIdInfo))
              .Build()))
{
    TRACE_FUNCTION();

    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();

    for (const SdfPath &primPath : HdSceneIndexPrimView(input)) {
        
        if (HdPrimTypeIsGprim(input->GetPrim(primPath).primType)) {
            _primIdInfo->pathToId[primPath] =
                static_cast<uint32_t>(_primIdInfo->idToPath.size());
            _primIdInfo->idToPath.push_back(primPath);
        }
    }
}

HdsiPrimIdSceneIndex::~HdsiPrimIdSceneIndex() = default;

void
HdsiPrimIdSceneIndex::_CompactPrimIdsIfNecessary()
{
    TRACE_FUNCTION();

    if (_primIdInfo->idToPath.size() <= maxId) {
        // No re-assignment unless we reach the maxId.
        return;
    }

    // Re-assign consectuive primId's (starting with 0) to all
    // imagable prims.

    _primIdInfo->idToPath.clear();

    const bool isObserved = _IsObserved();

    HdSceneIndexObserver::DirtiedPrimEntries dirtied;
    for (auto &[primPath, primId] : _primIdInfo->pathToId) {
        if (!primId) {
            continue;
        }
        primId =
            static_cast<uint32_t>(_primIdInfo->idToPath.size());
        _primIdInfo->idToPath.push_back(primPath);
        if (isObserved) {
            dirtied.emplace_back(
                primPath, HdPrimIdSchema::GetDefaultLocator());
        }
    }

    if (!isObserved) {
        return;
    }

    dirtied.emplace_back(
        SdfPath::AbsoluteRootPath(),
        HdSceneGlobalsSchema::GetPrimIdToPathLocator());

    _SendPrimsDirtied(dirtied);
}

HdSceneIndexPrim
HdsiPrimIdSceneIndex::GetPrim(const SdfPath &primPath) const
{
    TRACE_FUNCTION();

    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (primPath.IsAbsoluteRootPath()) {
        // Serve the primIdToPath field of the HdSceneGlobalsSchema
        // for the root prim.
        prim.dataSource =
            HdOverlayContainerDataSource::OverlayedContainerDataSources(
                _rootPrimOverlayDataSource, prim.dataSource);
        return prim;
    }

    if (HdPrimTypeIsGprim(prim.primType)) {
        // Wrap prim to populate the primId schema (lazily).
        prim.dataSource = _PrimDataSource::New(
            prim.dataSource, _primIdInfo, primPath);
    }

    return prim;
}

SdfPathVector
HdsiPrimIdSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdsiPrimIdSceneIndex::_PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    TRACE_FUNCTION();

    bool primIdsChanged = false;

    {
        TRACE_SCOPE("Updating prim id maps");

        for (const HdSceneIndexObserver::AddedPrimEntry &entry : entries) {
            if (HdPrimTypeIsGprim(entry.primType)) {
                _OptionalPrimId &existingId =
                    _primIdInfo->pathToId[entry.primPath];
                if (existingId) {
                    continue;
                }
                // Prim didn't have an existing primId, need to assign a new
                // one.
                existingId =
                    static_cast<uint32_t>(_primIdInfo->idToPath.size());
                _primIdInfo->idToPath.push_back(entry.primPath);
                primIdsChanged = true;
            } else {
                auto it = _primIdInfo->pathToId.find(entry.primPath);
                if (it == _primIdInfo->pathToId.end()) {
                    continue;
                }
                if (!it->second) {
                    continue;
                }
                // Prim had an existing primId. We need to delete it.
                _primIdInfo->idToPath[*it->second] = {};
                primIdsChanged = true;
                if (it.HasChild()) {
                    // It has imagable descendants, so we cannot
                    // delete it from the SdfPathTable.
                    it->second = {};
                } else {
                    // No children, so safe to delete.
                    _primIdInfo->pathToId.erase(it);
                }
            }
        }
    }

    _CompactPrimIdsIfNecessary();

    if (!_IsObserved()) {
        return;
    }

    _SendPrimsAdded(entries);

    if (primIdsChanged) {
        _SendPrimsDirtied(
            { { SdfPath::AbsoluteRootPath(),
                HdSceneGlobalsSchema::GetPrimIdToPathLocator() } });
    }
}

void
HdsiPrimIdSceneIndex::_PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    TRACE_FUNCTION();

    bool primIdsChanged = false;

    for (const HdSceneIndexObserver::RemovedPrimEntry &entry : entries) {
        const SdfPath &primPath = entry.primPath;

        if (primPath.IsAbsoluteRootPath()) {
            TfReset(_primIdInfo->pathToId);
            TfReset(_primIdInfo->idToPath);
            primIdsChanged = true;
            break;
        }

        // Update idToPath for all descendants of prim path.
        const auto [begin, end] =
            _primIdInfo->pathToId.FindSubtreeRange(primPath);
        for (auto it = begin; it != end; ++it) {
            if (it->second) {
                _primIdInfo->idToPath[*it->second] = {};
                primIdsChanged = true;
            }
        }

        // pathToId is SdfPathTable, so all descendants get
        // removed automatically.
        _primIdInfo->pathToId.erase(primPath);
    }

    if (!_IsObserved()) {
        return;
    }

    _SendPrimsRemoved(entries);

    if (primIdsChanged) {
        _SendPrimsDirtied(
            { { SdfPath::AbsoluteRootPath(),
                        HdSceneGlobalsSchema::GetPrimIdToPathLocator() } });
    }
}

void
HdsiPrimIdSceneIndex::_PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    _SendPrimsDirtied(entries);
}

PXR_NAMESPACE_CLOSE_SCOPE
