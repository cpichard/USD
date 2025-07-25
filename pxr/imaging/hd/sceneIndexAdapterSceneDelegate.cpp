//
// Copyright 2021 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/sceneIndexAdapterSceneDelegate.h"

#include "pxr/imaging/hd/aov.h"
#include "pxr/imaging/hd/basisCurvesTopology.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceLegacyPrim.h"
#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/dirtyBitsTranslator.h"
#include "pxr/imaging/hd/enums.h"
#include "pxr/imaging/hd/extComputationCpuCallback.h"
#include "pxr/imaging/hd/field.h"
#include "pxr/imaging/hd/geomSubset.h"
#include "pxr/imaging/hd/legacyTaskFactory.h"
#include "pxr/imaging/hd/legacyTaskSchema.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/meshTopology.h"
#include "pxr/imaging/hd/prefixingSceneIndex.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderSettings.h"
#include "pxr/imaging/hd/repr.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/schemaTypeDefs.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/topology.h"
#include "pxr/imaging/hd/types.h"


#include "pxr/imaging/hd/basisCurvesSchema.h"
#include "pxr/imaging/hd/basisCurvesTopologySchema.h"
#include "pxr/imaging/hd/cameraSchema.h"
#include "pxr/imaging/hd/capsuleSchema.h"
#include "pxr/imaging/hd/categoriesSchema.h"
#include "pxr/imaging/hd/coneSchema.h"
#include "pxr/imaging/hd/coordSysBindingSchema.h"
#include "pxr/imaging/hd/coordSysSchema.h"
#include "pxr/imaging/hd/cubeSchema.h"
#include "pxr/imaging/hd/cylinderSchema.h"
#include "pxr/imaging/hd/displayFilterSchema.h"
#include "pxr/imaging/hd/extComputationInputComputationSchema.h"
#include "pxr/imaging/hd/extComputationOutputSchema.h"
#include "pxr/imaging/hd/extComputationPrimvarSchema.h"
#include "pxr/imaging/hd/extComputationPrimvarsSchema.h"
#include "pxr/imaging/hd/extComputationSchema.h"
#include "pxr/imaging/hd/extentSchema.h"
#include "pxr/imaging/hd/geomSubsetSchema.h"
#include "pxr/imaging/hd/imageShaderSchema.h"
#include "pxr/imaging/hd/instanceCategoriesSchema.h"
#include "pxr/imaging/hd/instancedBySchema.h"
#include "pxr/imaging/hd/instancerTopologySchema.h"
#include "pxr/imaging/hd/integratorSchema.h"
#include "pxr/imaging/hd/legacyDisplayStyleSchema.h"
#include "pxr/imaging/hd/lightSchema.h"
#include "pxr/imaging/hd/materialBindingSchema.h"
#include "pxr/imaging/hd/materialBindingsSchema.h"
#include "pxr/imaging/hd/materialConnectionSchema.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialNodeParameterSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/purposeSchema.h"
#include "pxr/imaging/hd/renderBufferSchema.h"
#include "pxr/imaging/hd/renderProductSchema.h"
#include "pxr/imaging/hd/renderSettingsSchema.h"
#include "pxr/imaging/hd/renderVarSchema.h"
#include "pxr/imaging/hd/sampleFilterSchema.h"
#include "pxr/imaging/hd/sphereSchema.h"
#include "pxr/imaging/hd/subdivisionTagsSchema.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/volumeFieldBindingSchema.h"
#include "pxr/imaging/hd/volumeFieldSchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/imaging/hf/perfLog.h"
#include "pxr/imaging/pxOsd/subdivTags.h"
#include "pxr/imaging/pxOsd/tokens.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/base/arch/vsnprintf.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/range1f.h"
#include "pxr/base/gf/range2f.h"
#include "pxr/base/gf/range3d.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/stl.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"

#include "pxr/pxr.h"

#include "tbb/concurrent_unordered_set.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

//
// If the input prim is a datasource prim, we need some sensible default
// here...  For now, we pass [0,0] to turn off multisampling.
constexpr float _fallbackStartTime = 0.0f;
constexpr float _fallbackEndTime   = 0.0f;

/* static */
HdSceneIndexBaseRefPtr
HdSceneIndexAdapterSceneDelegate::AppendDefaultSceneFilters(
    HdSceneIndexBaseRefPtr inputSceneIndex, SdfPath const &delegateID)
{
    HdSceneIndexBaseRefPtr result = inputSceneIndex;

    // if no prefix, don't add HdPrefixingSceneIndex
    if (!delegateID.IsEmpty() && delegateID != SdfPath::AbsoluteRootPath()) {
        result = HdPrefixingSceneIndex::New(result, delegateID);
    }

    // disabling flattening as it's not yet needed for pure emulation
    //result = HdFlatteningSceneIndex::New(result);

    return result;
}

// ----------------------------------------------------------------------------

HdSceneIndexAdapterSceneDelegate::HdSceneIndexAdapterSceneDelegate(
    HdSceneIndexBaseRefPtr inputSceneIndex,
    HdRenderIndex *parentIndex,
    SdfPath const &delegateID)
: HdSceneDelegate(parentIndex, delegateID)
, _inputSceneIndex(inputSceneIndex)
, _sceneDelegatesBuilt(false)
, _cachedLocatorSet()
, _cachedDirtyBits(0)
, _cachedPrimType()
{

    std::string registeredName = ArchStringPrintf(
        "delegate adapter: %s @ %s",
            delegateID.GetString().c_str(),
            parentIndex->GetInstanceName().c_str());

    HdSceneIndexNameRegistry::GetInstance().RegisterNamedSceneIndex(
        registeredName, inputSceneIndex);

    // XXX: note that we will likely want to move this to the Has-A observer
    // pattern we're using now...
    _inputSceneIndex->AddObserver(HdSceneIndexObserverPtr(this));
}

HdSceneIndexAdapterSceneDelegate::~HdSceneIndexAdapterSceneDelegate()
{
    GetRenderIndex()._RemoveSubtree(GetDelegateID(), this);
}

HdSceneIndexPrim
HdSceneIndexAdapterSceneDelegate::_GetInputPrim(SdfPath const& id)
{
    _InputPrimCacheEntry &entry = _inputPrimCache[std::this_thread::get_id()];
    if (entry.first != id) {
        entry.first = id;
        entry.second = _inputSceneIndex->GetPrim(id);
    }
    return entry.second;
}

// ----------------------------------------------------------------------------
// HdSceneIndexObserver interfaces

static
HdTaskSharedPtr _CreateTask(const HdSceneIndexPrim &prim,
                            HdSceneDelegate * const delegate,
                            const SdfPath &indexPath)
{
    const HdLegacyTaskSchema taskSchema =
        HdLegacyTaskSchema::GetFromParent(prim.dataSource);
    HdLegacyTaskFactoryDataSourceHandle const ds =
        taskSchema.GetFactory();
    if (!ds) {
        TF_CODING_ERROR(
            "When adding task %s in HdSceneIndexAdapterSceneDelegate: "
            "No factory data source in HdLegacyTaskSchema.",
            indexPath.GetText());
        return nullptr;
    }
    HdLegacyTaskFactorySharedPtr const factory = ds->GetTypedValue(0.0f);
    if (!factory) {
        TF_CODING_ERROR(
            "When adding task %s in HdSceneIndexAdapterSceneDelegate: "
            "No factory in HdLegacyTaskSchema.",
            indexPath.GetText());
        return nullptr;
    }
    HdTaskSharedPtr const task = factory->Create(delegate, indexPath);
    if (!task) {
        TF_CODING_ERROR(
            "When adding task %s in HdSceneIndexAdapterSceneDelegate: "
            "No task from factory in HdLegacyTaskSchema.",
            indexPath.GetText());
    }
    return task;
}

void
HdSceneIndexAdapterSceneDelegate::_PrimAdded(
    const SdfPath &primPath,
    const TfToken &primType)
{
    SdfPath indexPath = primPath;
    _PrimCacheTable::iterator it = _primCache.find(indexPath);

    // There are 3 possible cases here:
    // (1) The prim doesn't exist yet and we add the prim.
    // (2) The prim exists, but has the wrong type; we remove the prim
    //     and re-insert with the correct type.
    // (3) The prim exists, and has the right type; in this case, we don't
    //     remove the prim but we should invalidate all of its properties.
    //     Note that we make an exception for tasks - which we handle like (2).
    bool isResync = false;

    if (it != _primCache.end()) {
        _PrimCacheEntry &entry = (*it).second;
        const TfToken &existingType = entry.primType;
        if (primType != existingType || primType == HdPrimTypeTokens->task) {
            if (GetRenderIndex().IsRprimTypeSupported(existingType)) {
                GetRenderIndex()._RemoveRprim(indexPath);
            } else if (GetRenderIndex().IsSprimTypeSupported(existingType)) {
                GetRenderIndex()._RemoveSprim(existingType, indexPath);
            } else if (GetRenderIndex().IsBprimTypeSupported(existingType)) {
                GetRenderIndex()._RemoveBprim(existingType, indexPath);
            } else if (existingType == HdPrimTypeTokens->instancer) {
                GetRenderIndex()._RemoveInstancer(indexPath);
            } else if (existingType == HdPrimTypeTokens->geomSubset) {
                GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                    indexPath.GetParentPath(), HdChangeTracker::DirtyTopology);
            }
        } else {
            isResync = true;
        }
    }

    if (!isResync) {
        if (GetRenderIndex().IsRprimTypeSupported(primType)) {
            GetRenderIndex()._InsertRprim(primType, this, indexPath);
        } else if (GetRenderIndex().IsSprimTypeSupported(primType)) {
            GetRenderIndex()._InsertSprim(primType, this, indexPath);
        } else if (GetRenderIndex().IsBprimTypeSupported(primType)) {
            GetRenderIndex()._InsertBprim(primType, this, indexPath);
        } else if (primType == HdPrimTypeTokens->instancer) {
            GetRenderIndex()._InsertInstancer(this, indexPath);
        } else if (primType == HdPrimTypeTokens->geomSubset) {
            GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                indexPath.GetParentPath(), HdChangeTracker::DirtyTopology);
        } else if (primType == HdPrimTypeTokens->task) {
            if (HdTaskSharedPtr const task =
                    _CreateTask(_GetInputPrim(indexPath), this, indexPath)) {
                GetRenderIndex()._InsertTask(this, indexPath, task);
            }
        }
    }

    if (it != _primCache.end()) {
        _PrimCacheEntry & entry = (*it).second;

        // Make sure prim type is up-to-date and clear caches.
        entry.primType = primType;
        std::atomic_store(&(entry.primvarDescriptors),
            std::shared_ptr<_PrimCacheEntry::PrimvarDescriptorsArray>());
        std::atomic_store(&(entry.extCmpPrimvarDescriptors),
            std::shared_ptr<_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray>());
    } else {
        _primCache[indexPath].primType = primType;
    }

    if (isResync) {
        // For resyncs, we need to invalidate the prim.
        // Note that since this only runs if we don't insert the prim, we
        // don't have any duplicate IsRprimTypeSupported/etc calls here.
        static HdDataSourceLocatorSet allDirty = { HdDataSourceLocator() };
        if (GetRenderIndex().IsRprimTypeSupported(primType)) {
            HdDirtyBits allDirtyRprim =
                HdDirtyBitsTranslator::RprimLocatorSetToDirtyBits(
                    primType, allDirty);
            GetRenderIndex().GetChangeTracker().
                _MarkRprimDirty(indexPath, allDirtyRprim);
        } else if (GetRenderIndex().IsSprimTypeSupported(primType)) {
            const TfTokenVector renderContexts =
                GetRenderIndex().GetRenderDelegate()->GetMaterialRenderContexts();    
            HdDirtyBits allDirtySprim =
                HdDirtyBitsTranslator::SprimLocatorSetToDirtyBits(
                    primType, allDirty, renderContexts);
            GetRenderIndex().GetChangeTracker().
                _MarkSprimDirty(indexPath, allDirtySprim);
        } else if (GetRenderIndex().IsBprimTypeSupported(primType)) {
            HdDirtyBits allDirtyBprim =
                HdDirtyBitsTranslator::BprimLocatorSetToDirtyBits(
                    primType, allDirty);
            GetRenderIndex().GetChangeTracker().
                _MarkBprimDirty(indexPath, allDirtyBprim);
        } else if (primType == HdPrimTypeTokens->instancer) {
            HdDirtyBits allDirtyInst =
                HdDirtyBitsTranslator::InstancerLocatorSetToDirtyBits(
                    primType, allDirty);
            GetRenderIndex().GetChangeTracker().
                _MarkInstancerDirty(indexPath, allDirtyInst);
        } else if (primType == HdPrimTypeTokens->geomSubset) {
            GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                indexPath.GetParentPath(), HdChangeTracker::DirtyTopology);
        }
    }

    // Keep hints for prim paths that have been seen with geomSubset children.
    if (primType == HdPrimTypeTokens->geomSubset) {
        _geomSubsetParents.insert(primPath.GetParentPath());
    }
}

void
HdSceneIndexAdapterSceneDelegate::PrimsAdded(
    const HdSceneIndexBase &sender,
    const AddedPrimEntries &entries)
{
    TRACE_FUNCTION();

    // Drop per-thread scene index input prim cache
    _inputPrimCache.clear();

    for (const AddedPrimEntry &entry : entries) {
        _PrimAdded(entry.primPath, entry.primType);
    }
    if (!entries.empty()) {
        _sceneDelegatesBuilt = false;
    }
}

void
HdSceneIndexAdapterSceneDelegate::PrimsRemoved(
    const HdSceneIndexBase &sender,
    const RemovedPrimEntries &entries)
{
    TRACE_FUNCTION();

    // Drop per-thread scene index input prim cache
    _inputPrimCache.clear();

    for (const RemovedPrimEntry &entry : entries) {
        // Special case Remove("/"), since this is a common shutdown operation.
        // Note: _Clear is faster than _RemoveSubtree here.
        if (entry.primPath.IsAbsoluteRootPath()) {
            GetRenderIndex()._Clear();
            _primCache.ClearInParallel();
            TfReset(_primCache);
            continue;
        }

        // RenderIndex::_RemoveSubtree can be expensive, so if we're
        // getting a remove message for a single prim it's better to
        // spend some time detecting that and calling the single-prim remove.
        _PrimCacheTable::iterator it = _primCache.find(entry.primPath);

        if (it == _primCache.end()) {
            continue;
        }

        const TfToken &primType = it->second.primType;

        _PrimCacheTable::iterator child = it;
        ++child;
        if (child == _primCache.end() ||
            child->first.GetParentPath() != it->first) {
            // The next item after entry.primPath is not a child, so we can
            // single-delete...
            if (GetRenderIndex().IsRprimTypeSupported(primType)) {
                GetRenderIndex()._RemoveRprim(entry.primPath);
            } else if (GetRenderIndex().IsSprimTypeSupported(primType)) {
                GetRenderIndex()._RemoveSprim(primType, entry.primPath);
            } else if (GetRenderIndex().IsBprimTypeSupported(primType)) {
                GetRenderIndex()._RemoveBprim(primType, entry.primPath);
            } else if (primType == HdPrimTypeTokens->instancer) {
                GetRenderIndex()._RemoveInstancer(entry.primPath);
            } else if (primType == HdPrimTypeTokens->geomSubset) {
                GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                    entry.primPath.GetParentPath(),
                    HdChangeTracker::DirtyTopology);
            } else if (primType == HdPrimTypeTokens->task) {
                GetRenderIndex()._RemoveTask(entry.primPath);
            }
        } else {
            // Otherwise, there's a subtree and we need to call _RemoveSubtree.
            GetRenderIndex()._RemoveSubtree(entry.primPath, this);
        }
        _primCache.erase(it);
    }

    if (!entries.empty()) {
        _sceneDelegatesBuilt = false;
    }
}

void
HdSceneIndexAdapterSceneDelegate::PrimsDirtied(
    const HdSceneIndexBase &sender,
    const DirtiedPrimEntries &entries)
{
    TRACE_FUNCTION();

    // Drop per-thread scene index input prim cache
    _inputPrimCache.clear();

    for (const DirtiedPrimEntry &entry : entries) {
        const SdfPath &indexPath = entry.primPath;
        _PrimCacheTable::iterator it = _primCache.find(indexPath);
        if (it == _primCache.end()) {
            // no need to do anything if our prim doesn't correspond to a
            // renderIndex entry
            continue;
        }

        const TfToken & primType = (*it).second.primType;

        if (GetRenderIndex().IsRprimTypeSupported(primType)) {
            HdDirtyBits dirtyBits = 0;
            if (entry.dirtyLocators == _cachedLocatorSet &&
                primType == _cachedPrimType) {
                dirtyBits = _cachedDirtyBits;
            } else {
                dirtyBits = HdDirtyBitsTranslator::RprimLocatorSetToDirtyBits(
                    primType, entry.dirtyLocators);
                _cachedLocatorSet = entry.dirtyLocators;
                _cachedPrimType = primType;
                _cachedDirtyBits = dirtyBits;
            }
            if (dirtyBits != HdChangeTracker::Clean) {
                GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                    indexPath, dirtyBits);
            }
        } else if (GetRenderIndex().IsSprimTypeSupported(primType)) {
            const TfTokenVector renderContexts =
                GetRenderIndex().GetRenderDelegate()->GetMaterialRenderContexts();    
            HdDirtyBits dirtyBits =
                HdDirtyBitsTranslator::SprimLocatorSetToDirtyBits(
                        primType, entry.dirtyLocators, renderContexts);
            if (dirtyBits != HdChangeTracker::Clean) {
                GetRenderIndex().GetChangeTracker()._MarkSprimDirty(
                    indexPath, dirtyBits);
            }
        } else if (GetRenderIndex().IsBprimTypeSupported(primType)) {
            HdDirtyBits dirtyBits =
                HdDirtyBitsTranslator::BprimLocatorSetToDirtyBits(
                        primType, entry.dirtyLocators);
            if (dirtyBits != HdChangeTracker::Clean) {
                GetRenderIndex().GetChangeTracker()._MarkBprimDirty(
                    indexPath, dirtyBits);
            }
        } else if (primType == HdPrimTypeTokens->instancer) {
            HdDirtyBits dirtyBits =
                HdDirtyBitsTranslator::InstancerLocatorSetToDirtyBits(
                        primType, entry.dirtyLocators);
            if (dirtyBits != HdChangeTracker::Clean) {
                GetRenderIndex().GetChangeTracker()._MarkInstancerDirty(
                    indexPath, dirtyBits);
            }
        } else if (primType == HdPrimTypeTokens->geomSubset) {
            GetRenderIndex().GetChangeTracker()._MarkRprimDirty(
                indexPath.GetParentPath(), HdChangeTracker::DirtyTopology);
        } else if (primType == HdPrimTypeTokens->task) {
            const HdDirtyBits dirtyBits =
                HdDirtyBitsTranslator::TaskLocatorSetToDirtyBits(
                    entry.dirtyLocators);
            if (dirtyBits != HdChangeTracker::Clean) {
                GetRenderIndex().GetChangeTracker()._MarkTaskDirty(
                    indexPath, dirtyBits);
            }
        }

        if (entry.dirtyLocators.Intersects(
                HdPrimvarsSchema::GetDefaultLocator())) {
            std::atomic_store(&(it->second.primvarDescriptors),
                std::shared_ptr<_PrimCacheEntry::PrimvarDescriptorsArray>());
        }

        if (entry.dirtyLocators.Intersects(
                HdExtComputationPrimvarsSchema::GetDefaultLocator())) {
            std::atomic_store(&(it->second.extCmpPrimvarDescriptors),
                std::shared_ptr<_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray>());
        }
    }
}

void
HdSceneIndexAdapterSceneDelegate::PrimsRenamed(
    const HdSceneIndexBase &sender,
    const RenamedPrimEntries &entries)
{
    ConvertPrimsRenamedToRemovedAndAdded(sender, entries, this);
}

// ----------------------------------------------------------------------------

static bool
_IsVisible(const HdContainerDataSourceHandle& primSource)
{
    if (const auto visSchema = HdVisibilitySchema::GetFromParent(primSource)) {
        if (const HdBoolDataSourceHandle visDs = visSchema.GetVisibility()) {
            return visDs->GetTypedValue(0.0f);
        }
    }
    return true;
}

static SdfPath
_GetBoundMaterialPath(
    const HdContainerDataSourceHandle& ds,
    const TfToken& purpose)
{
    if (const auto bindingsSchema = HdMaterialBindingsSchema::GetFromParent(ds)) {
        if (const HdMaterialBindingSchema bindingSchema =
            bindingsSchema.GetMaterialBinding(purpose)) {
            if (const HdPathDataSourceHandle ds = bindingSchema.GetPath()) {
                return ds->GetTypedValue(0.0f);
            }
        }
    }
    return SdfPath::EmptyPath();
}

static VtIntArray
_Union(const VtIntArray& a, const VtIntArray& b)
{
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    VtIntArray out = a;
    // XXX: VtIntArray has no insert method, does not support back_inserter,
    //      and has no appending operator.
    out.reserve(out.size() + b.size());
    std::for_each(b.cbegin(), b.cend(),
        [&out](const int& val) { out.push_back(val); });
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static void
_GatherGeomSubsets(
    const SdfPath& parentPath,
    const HdSceneIndexBaseRefPtr& sceneIndex,
    const TfToken& materialBindingPurpose,
    HdTopology* topology)
{
    TRACE_FUNCTION();
    TF_VERIFY(topology);
    HdGeomSubsets subsets;
    // Not all direct children are subsets, but all subsets are direct children.
    // XXX: Both UsdImagingStageSceneIndex and HdLegacyGeomSubsetSceneIndex
    // should report child prim paths in authored order.
    for (const SdfPath& childPath : sceneIndex->GetChildPrimPaths(parentPath)) {
        const HdSceneIndexPrim& child = sceneIndex->GetPrim(childPath);
        // XXX lets keep track of subsets we see instead of doing this
        if (child.primType != HdPrimTypeTokens->geomSubset ||
            child.dataSource == nullptr) {
            continue;
        }
        const auto& schema =
            HdGeomSubsetSchema::GetFromParent(child.dataSource);
        if (!schema.IsDefined()) {
            continue;
        }
        const HdTokenDataSourceHandle typeDs = schema.GetType();
        if (!typeDs) {
            continue;
        }
        const TfToken type = typeDs->GetTypedValue(0.0f);
        const HdIntArrayDataSourceHandle indicesDs = schema.GetIndices();
        static const VtIntArray emptyIndices;
        const VtIntArray indices = indicesDs
          ? indicesDs->GetTypedValue(0.0f)
          : emptyIndices;
        if (!_IsVisible(child.dataSource)) {
            if (auto* topo = dynamic_cast<HdMeshTopology*>(topology)) {
                if (type == HdGeomSubsetSchemaTokens->typeFaceSet) {
                    topo->SetInvisibleFaces(
                        _Union(topo->GetInvisibleFaces(), indices));
                } else if (type == HdGeomSubsetSchemaTokens->typePointSet) {
                    topo->SetInvisiblePoints(
                        _Union(topo->GetInvisiblePoints(), indices));
                }
            } else if (auto* topo =
                dynamic_cast<HdBasisCurvesTopology*>(topology)) {
                if (type == HdGeomSubsetSchemaTokens->typeCurveSet) {
                    topo->SetInvisibleCurves(
                        _Union(topo->GetInvisibleCurves(), indices));
                } else if (type == HdGeomSubsetSchemaTokens->typePointSet) {
                    topo->SetInvisiblePoints(
                        _Union(topo->GetInvisiblePoints(), indices));
                }
            }
            continue;
        }
        const SdfPath materialId = _GetBoundMaterialPath(
            child.dataSource, materialBindingPurpose);
        if (materialId.IsEmpty()) {
            continue;
        }
        subsets.push_back({

            // XXX: Hard-coded face type since it is the only one supported.
            HdGeomSubset::Type::TypeFaceSet,
            childPath,
            materialId,
            indices });
    }

    if (auto* topo = dynamic_cast<HdMeshTopology*>(topology)) {
        topo->SetGeomSubsets(subsets);
    }
}

HdMeshTopology
HdSceneIndexAdapterSceneDelegate::GetMeshTopology(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdMeshSchema meshSchema = HdMeshSchema::GetFromParent(prim.dataSource);


    HdMeshTopologySchema meshTopologySchema = meshSchema.GetTopology();
    if (!meshTopologySchema.IsDefined()) {
        return HdMeshTopology();
    }

    HdIntArrayDataSourceHandle faceVertexCountsDataSource =
            meshTopologySchema.GetFaceVertexCounts();

    HdIntArrayDataSourceHandle faceVertexIndicesDataSource =
            meshTopologySchema.GetFaceVertexIndices();

    if (!faceVertexCountsDataSource || !faceVertexIndicesDataSource) {
        return HdMeshTopology();
    }

    TfToken scheme = PxOsdOpenSubdivTokens->none;
    if (HdTokenDataSourceHandle schemeDs =
            meshSchema.GetSubdivisionScheme()) {
        scheme = schemeDs->GetTypedValue(0.0f);
    }

    VtIntArray holeIndices;
    if (HdIntArrayDataSourceHandle holeDs =
            meshTopologySchema.GetHoleIndices()) {
        holeIndices = holeDs->GetTypedValue(0.0f);
    }

    TfToken orientation = PxOsdOpenSubdivTokens->rightHanded;
    if (HdTokenDataSourceHandle orientDs =
            meshTopologySchema.GetOrientation()) {
        orientation = orientDs->GetTypedValue(0.0f);
    }

    HdMeshTopology meshTopology(
        scheme,
        orientation,
        faceVertexCountsDataSource->GetTypedValue(0.0f),
        faceVertexIndicesDataSource->GetTypedValue(0.0f),
        holeIndices);

    if (_geomSubsetParents.find(id) != _geomSubsetParents.end()) {
        const TfToken purpose =
            GetRenderIndex().GetRenderDelegate()->GetMaterialBindingPurpose();
        _GatherGeomSubsets(id, _inputSceneIndex, purpose, &meshTopology);
    }

    return meshTopology;
}

bool
HdSceneIndexAdapterSceneDelegate::GetDoubleSided(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdMeshSchema meshSchema =
        HdMeshSchema::GetFromParent(prim.dataSource);
    if (meshSchema.IsDefined()) {
        HdBoolDataSourceHandle doubleSidedDs = meshSchema.GetDoubleSided();
        if (doubleSidedDs) {
            return doubleSidedDs->GetTypedValue(0.0f);
        }
    } else if (prim.primType == HdPrimTypeTokens->basisCurves) {
        // TODO: We assume all basis curves are double-sided in Hydra. This is
        //       inconsistent with the USD schema, which allows sidedness to be
        //       declared on the USD gprim. Note however that sidedness only
        //       affects basis curves with authored normals (i.e., ribbons).
        return true;
    }
    return false;
}

GfRange3d
HdSceneIndexAdapterSceneDelegate::GetExtent(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdExtentSchema extentSchema =
        HdExtentSchema::GetFromParent(prim.dataSource);
    if (!extentSchema.IsDefined()) {
        return GfRange3d();
    }

    GfVec3d min, max;
    if (HdVec3dDataSourceHandle minDs = extentSchema.GetMin()) {
        min = minDs->GetTypedValue(0);
    }
    if (HdVec3dDataSourceHandle maxDs = extentSchema.GetMax()) {
        max = maxDs->GetTypedValue(0);
    }

    return GfRange3d(min, max);
}

static
bool
_IsLegacyInstancer(const HdSceneIndexPrim &prim)
{    
    if (prim.primType != HdPrimTypeTokens->instancer) {
        return false;
    }

    HdContainerDataSourceHandle const container =
        HdInstancerTopologySchema::
        GetFromParent(prim.dataSource).GetContainer();
    if (!container) {
        return false;
    }
    auto const ds = HdBoolDataSource::Cast(
        container->Get(HdLegacyFlagTokens->isLegacyInstancer));
    if(!ds) {
        return false;
    }
    return ds->GetTypedValue(0.0f);
}

bool
HdSceneIndexAdapterSceneDelegate::GetVisible(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    if (_IsLegacyInstancer(prim)) {
        // For usdImaging delegate.
        // When changing the visibility of a USD point instancer, the
        // delegate does not properly update the visibility of the
        // corresponding Hydra instancer. It actually invis's a point
        // instancer by deleting all the prototype prims.
        return true;
    }
    
    HdVisibilitySchema visibilitySchema =
        HdVisibilitySchema::GetFromParent(prim.dataSource);
    if (!visibilitySchema.IsDefined()) {
        return true; // default visible
    }

    HdBoolDataSourceHandle visDs = visibilitySchema.GetVisibility();
    if (!visDs) {
        return true;
    }
    return visDs->GetTypedValue(0);
}

TfToken
HdSceneIndexAdapterSceneDelegate::GetRenderTag(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdPurposeSchema purposeSchema =
        HdPurposeSchema::GetFromParent(prim.dataSource);
    if (!purposeSchema.IsDefined()) {
        return HdRenderTagTokens->geometry; // default render tag.
    }

    HdTokenDataSourceHandle purposeDs = purposeSchema.GetPurpose();
    if (!purposeDs) {
        return HdRenderTagTokens->geometry;
    }
    return purposeDs->GetTypedValue(0);
}

PxOsdSubdivTags
HdSceneIndexAdapterSceneDelegate::GetSubdivTags(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    PxOsdSubdivTags tags;

    HdMeshSchema meshSchema = HdMeshSchema::GetFromParent(prim.dataSource);
    if (!meshSchema.IsDefined()) {
        return tags;
    }

    HdSubdivisionTagsSchema subdivTagsSchema = meshSchema.GetSubdivisionTags();
    if (!subdivTagsSchema.IsDefined()) {
        return tags;
    }

    if (HdTokenDataSourceHandle fvliDs =
            subdivTagsSchema.GetFaceVaryingLinearInterpolation()) {
        tags.SetFaceVaryingInterpolationRule(fvliDs->GetTypedValue(0.0f));
    }

    if (HdTokenDataSourceHandle ibDs =
            subdivTagsSchema.GetInterpolateBoundary()) {
        tags.SetVertexInterpolationRule(ibDs->GetTypedValue(0.0f));
    }

    if (HdTokenDataSourceHandle tsrDs =
            subdivTagsSchema.GetTriangleSubdivisionRule()) {
        tags.SetTriangleSubdivision(tsrDs->GetTypedValue(0.0f));
    }

    if (HdIntArrayDataSourceHandle cniDs =
            subdivTagsSchema.GetCornerIndices()) {
        tags.SetCornerIndices(cniDs->GetTypedValue(0.0f));
    }

    if (HdFloatArrayDataSourceHandle cnsDs =
            subdivTagsSchema.GetCornerSharpnesses()) {
        tags.SetCornerWeights(cnsDs->GetTypedValue(0.0f));
    }

    if (HdIntArrayDataSourceHandle criDs =
            subdivTagsSchema.GetCreaseIndices()) {
        tags.SetCreaseIndices(criDs->GetTypedValue(0.0f));
    }

    if (HdIntArrayDataSourceHandle crlDs =
            subdivTagsSchema.GetCreaseLengths()) {
        tags.SetCreaseLengths(crlDs->GetTypedValue(0.0f));
    }

    if (HdFloatArrayDataSourceHandle crsDs =
            subdivTagsSchema.GetCreaseSharpnesses()) {
        tags.SetCreaseWeights(crsDs->GetTypedValue(0.0f));
    }

    return tags;
}

HdBasisCurvesTopology
HdSceneIndexAdapterSceneDelegate::GetBasisCurvesTopology(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdBasisCurvesSchema basisCurvesSchema =
        HdBasisCurvesSchema::GetFromParent(prim.dataSource);

    HdBasisCurvesTopologySchema bcTopologySchema =
        basisCurvesSchema.GetTopology();

    if (!bcTopologySchema.IsDefined()) {
        return HdBasisCurvesTopology();
    }

    HdIntArrayDataSourceHandle curveVertexCountsDataSource =
            bcTopologySchema.GetCurveVertexCounts();

    if (!curveVertexCountsDataSource) {
        return HdBasisCurvesTopology();
    }

    VtIntArray curveIndices;
    HdIntArrayDataSourceHandle curveIndicesDataSource =
        bcTopologySchema.GetCurveIndices();
    if (curveIndicesDataSource) {
        curveIndices = curveIndicesDataSource->GetTypedValue(0.0f);
    }

    TfToken basis = HdTokens->bezier;
    HdTokenDataSourceHandle basisDataSource = bcTopologySchema.GetBasis();
    if (basisDataSource) {
        basis = basisDataSource->GetTypedValue(0.0f);
    }

    TfToken type = HdTokens->linear;
    HdTokenDataSourceHandle typeDataSource = bcTopologySchema.GetType();
    if (typeDataSource) {
        type = typeDataSource->GetTypedValue(0.0f);
    }

    TfToken wrap = HdTokens->nonperiodic;
    HdTokenDataSourceHandle wrapDataSource = bcTopologySchema.GetWrap();
    if (wrapDataSource) {
        wrap = wrapDataSource->GetTypedValue(0.0f);
    }

    HdBasisCurvesTopology result(
        type, basis, wrap,
        curveVertexCountsDataSource->GetTypedValue(0.0f),
        curveIndices);

    if (_geomSubsetParents.find(id) != _geomSubsetParents.end()) {
        const TfToken purpose =
            GetRenderIndex().GetRenderDelegate()->GetMaterialBindingPurpose();
        _GatherGeomSubsets(id, _inputSceneIndex, purpose, &result);
    }

    return result;
}

VtArray<TfToken>
HdSceneIndexAdapterSceneDelegate::GetCategories(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    static const VtArray<TfToken> emptyResult;

    HdCategoriesSchema categoriesSchema =
        HdCategoriesSchema::GetFromParent(
            prim.dataSource);

    if (!categoriesSchema.IsDefined()) {
        return emptyResult;
    }

    return categoriesSchema.GetIncludedCategoryNames();
}

HdVolumeFieldDescriptorVector
HdSceneIndexAdapterSceneDelegate::GetVolumeFieldDescriptors(
        SdfPath const &volumeId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(volumeId);

    HdVolumeFieldDescriptorVector result;
    HdVolumeFieldBindingSchema bindingSchema =
        HdVolumeFieldBindingSchema::GetFromParent(prim.dataSource);
    if (!bindingSchema.IsDefined()) {
        return result;
    }

    TfTokenVector names = bindingSchema.GetContainer()->GetNames();
    for (const TfToken& name : names) {
        HdPathDataSourceHandle pathDs =
            bindingSchema.GetVolumeFieldBinding(name);
        if (!pathDs) {
            continue;
        }

        HdVolumeFieldDescriptor desc;
        desc.fieldName = name;
        desc.fieldId = pathDs->GetTypedValue(0);

        // XXX: Kind of a hacky way to get the prim type for the old API.
        HdSceneIndexPrim fieldPrim = _inputSceneIndex->GetPrim(desc.fieldId);
        if (!fieldPrim.dataSource) {
            continue;
        }
        desc.fieldPrimType = fieldPrim.primType;

        result.push_back(desc);
    }

    return result;
}

SdfPath
HdSceneIndexAdapterSceneDelegate::GetMaterialId(SdfPath const & id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdMaterialBindingsSchema materialBindings =
        HdMaterialBindingsSchema::GetFromParent(
            prim.dataSource);
    const TfToken purpose =
        GetRenderIndex().GetRenderDelegate()->GetMaterialBindingPurpose();
    HdMaterialBindingSchema materialBinding =
        materialBindings.GetMaterialBinding(purpose);
    if (HdPathDataSourceHandle const ds = materialBinding.GetPath()) {
        return ds->GetTypedValue(0.0f);
    }
    return SdfPath();
}

HdIdVectorSharedPtr
HdSceneIndexAdapterSceneDelegate::GetCoordSysBindings(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdCoordSysBindingSchema coordSys = HdCoordSysBindingSchema::GetFromParent(
        prim.dataSource);
    if (!coordSys.IsDefined()) {
        return nullptr;
    }

    HdIdVectorSharedPtr idVec = HdIdVectorSharedPtr(new SdfPathVector());
    TfTokenVector names = coordSys.GetContainer()->GetNames();
    for (const TfToken& name : names) {
        HdPathDataSourceHandle pathDs =
            coordSys.GetCoordSysBinding(name);
        if (!pathDs) {
            continue;
        }

        idVec->push_back(pathDs->GetTypedValue(0));
    }

    return idVec;
}

HdRenderBufferDescriptor
HdSceneIndexAdapterSceneDelegate::GetRenderBufferDescriptor(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);
    HdRenderBufferDescriptor desc;

    HdRenderBufferSchema rb = HdRenderBufferSchema::GetFromParent(
        prim.dataSource);
    if (!rb.IsDefined()) {
        return desc;
    }

    HdVec3iDataSourceHandle dim = rb.GetDimensions();
    if (dim) {
        desc.dimensions = dim->GetTypedValue(0);
    }

    HdFormatDataSourceHandle fmt = rb.GetFormat();
    if (fmt) {
        desc.format = fmt->GetTypedValue(0);
    }

    HdBoolDataSourceHandle ms = rb.GetMultiSampled();
    if (ms) {
        desc.multiSampled = ms->GetTypedValue(0);
    }

    return desc;
}

static
std::map<TfToken, VtValue>
_GetHdParamsFromDataSource(
    HdMaterialNodeParameterContainerSchema containerSchema)
{
    std::map<TfToken, VtValue> hdParams;
    if (!containerSchema) {
        return hdParams;
    }

    const TfTokenVector pNames = containerSchema.GetNames();
    for (const auto & pName : pNames) {
        HdMaterialNodeParameterSchema paramSchema = containerSchema.Get(pName);
        if (!paramSchema) {
            continue;
        }

        // Parameter Value
        if (HdSampledDataSourceHandle paramValueDS = paramSchema.GetValue()) {
            hdParams[pName] = paramValueDS->GetValue(0);
        }
        // ColorSpace Metadata
        if (HdTokenDataSourceHandle colorSpaceDS = paramSchema.GetColorSpace()) {
            const TfToken cspName(
                SdfPath::JoinIdentifier(
                    HdMaterialNodeParameterSchemaTokens->colorSpace, pName));
            const TfToken colorSpaceToken = colorSpaceDS->GetTypedValue(0);
            if (!colorSpaceToken.IsEmpty()) {
                hdParams[cspName] = VtValue(colorSpaceDS->GetTypedValue(0));
            }
        }
        // TypeName Metadata
        if (HdTokenDataSourceHandle typeNameDs = paramSchema.GetTypeName()) {
            const TfToken typName(
                SdfPath::JoinIdentifier(
                    HdMaterialNodeParameterSchemaTokens->typeName, pName));
            const TfToken typeNameToken = typeNameDs->GetTypedValue(0);
            if (!typeNameToken.IsEmpty()) {
                hdParams[typName] = VtValue(typeNameToken);
            }
        }
    }
    return hdParams;
}

static
void
_Walk(
    const SdfPath & nodePath,
    HdMaterialNodeContainerSchema nodesSchema,
    const TfTokenVector &renderContexts,
    std::unordered_set<SdfPath, SdfPath::Hash> * visitedSet,
    HdMaterialNetwork * netHd)
{
    if (visitedSet->find(nodePath) != visitedSet->end()) {
        return;
    }

    visitedSet->insert(nodePath);

    TfToken nodePathTk(nodePath.GetToken());

    HdMaterialNodeSchema nodeSchema = nodesSchema.Get(nodePathTk);

    if (!nodeSchema.IsDefined()) {
        return;
    }

    TfToken nodeId;
    if (HdTokenDataSourceHandle idDs = nodeSchema.GetNodeIdentifier()) {
        nodeId = idDs->GetTypedValue(0);
    }

    // check for render-specific contexts
    if (!renderContexts.empty()) {
        if (HdContainerDataSourceHandle idsDs =
                nodeSchema.GetRenderContextNodeIdentifiers()) {
            for (const TfToken &name : renderContexts) {

                if (name.IsEmpty() && !nodeId.IsEmpty()) {
                    // The universal renderContext was requested, so
                    // use the universal nodeId if we found one above.
                    break;
                }
                if (HdTokenDataSourceHandle ds = HdTokenDataSource::Cast(
                        idsDs->Get(name))) {

                    const TfToken v = ds->GetTypedValue(0);
                    if (!v.IsEmpty()) {
                        nodeId = v;
                        break;
                    }
                }
            }
        }
    }

    if (HdMaterialConnectionVectorContainerSchema vectorContainerSchema =
            nodeSchema.GetInputConnections()) {
        const TfTokenVector connsNames = vectorContainerSchema.GetNames();
        for (const auto & connName : connsNames) {
            HdMaterialConnectionVectorSchema vectorSchema =
                vectorContainerSchema.Get(connName);

            if (!vectorSchema) {
                continue;
            }

            for (size_t i = 0 ; i < vectorSchema.GetNumElements() ; i++) {
                HdMaterialConnectionSchema connSchema =
                    vectorSchema.GetElement(i);
                if (!connSchema.IsDefined()) {
                    continue;
                }

                TfToken p = connSchema.GetUpstreamNodePath()->GetTypedValue(0);
                TfToken n =
                    connSchema.GetUpstreamNodeOutputName()->GetTypedValue(0);
                _Walk(SdfPath(p.GetString()), nodesSchema, renderContexts,
                        visitedSet, netHd);

                HdMaterialRelationship r;
                r.inputId = SdfPath(p.GetString());
                r.inputName = n;
                r.outputId = nodePath;
                r.outputName=connName;
                netHd->relationships.push_back(r);
            }
        }
    }

    HdMaterialNode n;
    n.identifier = nodeId;
    n.path = nodePath;
    n.parameters = _GetHdParamsFromDataSource(nodeSchema.GetParameters());
    netHd->nodes.push_back(n);
}

// Note: Utility methods below expect a valid data source handle.
VtDictionary
_ToDictionary(HdSampledDataSourceContainerSchema schema)
{
    VtDictionary dict;
    for (const TfToken& name : schema.GetNames()) {
        if (HdSampledDataSourceHandle valueDs = schema.Get(name)) {
            dict[name.GetString()] = valueDs->GetValue(0);
        }
    }
    return dict;
}

VtDictionary
_ToDictionary(HdContainerDataSourceHandle const &cds)
{
    return _ToDictionary(HdSampledDataSourceContainerSchema(cds));
}

static
HdMaterialNetworkMap
_ToMaterialNetworkMap(
    HdMaterialNetworkSchema netSchema,
    const TfTokenVector& renderContexts)
{
    // Some legacy render delegates may require all shading nodes
    // to be included regardless of whether they are reachable via
    // a terminal. While 100% accuracy in emulation would require that
    // behavior to be enabled by default, it is generally not desireable
    // as it leads to a lot of unnecessary data duplication across
    // terminals.
    //
    // A renderer which wants this behavior can configure its networks
    // with an "includeDisconnectedNodes" data source.
    bool includeDisconnectedNodes = false;
    if (HdContainerDataSourceHandle netContainer = netSchema.GetContainer()) {
        static const TfToken key("includeDisconnectedNodes");
        if (HdBoolDataSourceHandle ds = HdBoolDataSource::Cast(
                netContainer->Get(key))) {
            includeDisconnectedNodes = ds->GetTypedValue(0.0f);
        }
    }

    // Convert HdDataSource with material data to HdMaterialNetworkMap
    HdMaterialNetworkMap matHd;

    // List of visited nodes to facilitate network traversal
    std::unordered_set<SdfPath, SdfPath::Hash> visitedNodes;

    HdMaterialNodeContainerSchema nodesSchema =
        netSchema.GetNodes();
    HdMaterialConnectionContainerSchema terminalsSchema =
        netSchema.GetTerminals();
    const TfTokenVector names = terminalsSchema.GetNames();

    if (const HdSampledDataSourceContainerSchema config =
                                                netSchema.GetConfig()) {
        matHd.config = _ToDictionary(config);
    }

    for (const auto & name : names) {
        visitedNodes.clear();

        // Extract connections one by one
        HdMaterialConnectionSchema connSchema = terminalsSchema.Get(name);
        if (!connSchema.IsDefined()) {
            continue;
        }

        // Keep track of the terminals
        TfToken pathTk = connSchema.GetUpstreamNodePath()->GetTypedValue(0);
        if (pathTk.IsEmpty()) {
            // Allow setting terminals to an empty string to disable them.
            continue;
        }
        SdfPath path(pathTk.GetString());
        matHd.terminals.push_back(path);

        // Continue walking the network
        HdMaterialNetwork & netHd = matHd.map[name];
        _Walk(path, nodesSchema, renderContexts, &visitedNodes, &netHd);

        // see "includeDisconnectedNodes" above
        if (includeDisconnectedNodes && nodesSchema) {
            for (const TfToken &nodeName : nodesSchema.GetNames()) {
                _Walk(SdfPath(nodeName.GetString()),
                    nodesSchema, renderContexts, &visitedNodes, &netHd);
            }
        }
    }

    return matHd;
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetMaterialResource(SdfPath const & id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdMaterialSchema matSchema = HdMaterialSchema::GetFromParent(
            prim.dataSource);
    if (!matSchema.IsDefined()) {
        return VtValue();
    }

    // Query for a material network to match the requested render contexts
    const TfTokenVector renderContexts =
        GetRenderIndex().GetRenderDelegate()->GetMaterialRenderContexts();
    HdMaterialNetworkSchema netSchema = matSchema.GetMaterialNetwork(renderContexts);
    if (!netSchema.IsDefined()) {
        return VtValue();
    }

    return VtValue(_ToMaterialNetworkMap(netSchema, renderContexts));
}

static
TfTokenVector
_ToTokenVector(const std::vector<std::string> &strings)
{
    TfTokenVector tokens;
    tokens.reserve(strings.size());
    for (const std::string &s : strings) {
        tokens.emplace_back(s);
    }
    return tokens;
}

// If paramName has no ":", return empty locator.
// Otherwise, split about ":" to create locator.
static
HdDataSourceLocator
_ParamNameToLocator(TfToken const &paramName)
{
    if (paramName.GetString().find(':') == std::string::npos) {
        return HdDataSourceLocator::EmptyLocator();
    }

    const TfTokenVector parts = _ToTokenVector(
        TfStringTokenize(paramName.GetString(), ":"));

    return HdDataSourceLocator(parts.size(), parts.data());
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetCameraParamValue(
        SdfPath const &cameraId, TfToken const &paramName)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(cameraId);
    if (!prim.dataSource) {
        return VtValue();
    }

    HdCameraSchema cameraSchema =
        HdCameraSchema::GetFromParent(prim.dataSource);

    if (!cameraSchema) {
        return VtValue();
    }

    // If paramName has a ":", say, "foo:bar", we translate to
    // a data source locator here and check whether there is a data source
    // at HdDataSourceLocator{"camera", "foo", "bar"} for the prim in the
    // scene index.
    const HdDataSourceLocator locator = _ParamNameToLocator(paramName);
    if (!locator.IsEmpty()) {
        if (HdSampledDataSourceHandle const ds =
                HdSampledDataSource::Cast(
                    HdContainerDataSource::Get(
                        cameraSchema.GetNamespacedProperties().GetContainer(),
                        locator))) {
            return ds->GetValue(0.0f);
        }

        if (HdSampledDataSourceHandle const ds =
                HdSampledDataSource::Cast(
                    HdContainerDataSource::Get(
                        cameraSchema.GetContainer(),
                        locator))) {
            return ds->GetValue(0.0f);
        }

        // If there was no nested data source for the data source locator
        // we constructed, fall through to query for "foo:bar".
        //
        // This covers the case where emulation is active and we have
        // another HdSceneDelegate that was added to the HdRenderIndex.
        // We want to call GetCameraParamValue on that other scene
        // delegate with the same paramName that we were given (through
        // a HdLegacyPrimSceneIndex (feeding directly or indirectly
        // into the _inputSceneIndex) and the
        // Hd_DataSourceLegacyCameraParamValue data source).
    }

    TfToken cameraSchemaToken = paramName;
    if (paramName == HdCameraTokens->clipPlanes) {
        cameraSchemaToken = HdCameraSchemaTokens->clippingPlanes;
    }

    HdSampledDataSourceHandle valueDs =
        HdSampledDataSource::Cast(
            cameraSchema.GetContainer()->Get(cameraSchemaToken));
    if (!valueDs) {
        return VtValue();
    }

    const VtValue value = valueDs->GetValue(0);
    // Smooth out some incompatibilities between scene delegate and
    // datasource schemas...
    if (paramName == HdCameraSchemaTokens->projection) {
        TfToken proj = HdCameraSchemaTokens->perspective;
        if (value.IsHolding<TfToken>()) {
            proj = value.UncheckedGet<TfToken>();
        }
        return VtValue(proj == HdCameraSchemaTokens->perspective ?
                HdCamera::Perspective :
                HdCamera::Orthographic);
    } else if (paramName == HdCameraSchemaTokens->clippingRange) {
        GfVec2f range(0);
        if (value.IsHolding<GfVec2f>()) {
            range = value.UncheckedGet<GfVec2f>();
        }
        return VtValue(GfRange1f(range[0], range[1]));
    } else if (paramName == HdCameraTokens->clipPlanes) {
        std::vector<GfVec4d> vec;
        if (value.IsHolding<VtArray<GfVec4d>>()) {
            const VtArray<GfVec4d> array =
                value.UncheckedGet<VtArray<GfVec4d>>();
            vec.reserve(array.size());
            for (const GfVec4d &p : array) {
                vec.push_back(p);
            }
        }
        return VtValue(vec);
    } else {
        return value;
    }
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetLightParamValue(
        SdfPath const &id, TfToken const &paramName)
{
    TRACE_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (!prim.dataSource) {
        return VtValue();
    }

    HdContainerDataSourceHandle light =
        HdContainerDataSource::Cast(
            prim.dataSource->Get(HdLightSchemaTokens->light));
    if (light) {
        HdSampledDataSourceHandle valueDs = HdSampledDataSource::Cast(
                light->Get(paramName));
        if (valueDs) {
            return valueDs->GetValue(0);
        }
    }

    return VtValue();
}

namespace {
// Note: Utility methods below expect a valid data source handle.

using _RenderVar = HdRenderSettings::RenderProduct::RenderVar;

_RenderVar
_ToRenderVar(HdRenderVarSchema varSchema)
{
    _RenderVar var;
    if (auto h = varSchema.GetPath()) {
        var.varPath = h->GetTypedValue(0);
    }
    if (auto h = varSchema.GetDataType()) {
        var.dataType = h->GetTypedValue(0);
    }
    if (auto h = varSchema.GetSourceName()) {
        var.sourceName = h->GetTypedValue(0);
    }
    if (auto h = varSchema.GetSourceType()) {
        var.sourceType = h->GetTypedValue(0);
    }
    if (auto h = varSchema.GetNamespacedSettings()) {
        var.namespacedSettings = _ToDictionary(h);
    }
    return var;
}

using _RenderVars = std::vector<_RenderVar>;
_RenderVars
_ToRenderVars(HdRenderVarVectorSchema varsSchema)
{
    const size_t numVars = varsSchema.GetNumElements();

    _RenderVars vars;
    vars.reserve(numVars);

    for (size_t idx = 0; idx < numVars; idx++) {
        if (HdRenderVarSchema varSchema = varsSchema.GetElement(idx)) {
            vars.push_back(_ToRenderVar(varSchema));
        }
    }

    return vars;
}

GfRange2f
_ToRange2f(GfVec4f const &v)
{
    return GfRange2f(GfVec2f(v[0], v[1]), GfVec2f(v[2],v[3]));
}

HdRenderSettings::RenderProduct
_ToRenderProduct(HdRenderProductSchema productSchema)
{
    HdRenderSettings::RenderProduct prod;

    if (auto h = productSchema.GetPath()) {
        prod.productPath = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetType()) {
        prod.type = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetName()) {
        prod.name = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetResolution()) {
        prod.resolution = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetRenderVars()) {
        prod.renderVars = _ToRenderVars(h);
    }
    if (auto h = productSchema.GetCameraPrim()) {
        prod.cameraPath = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetPixelAspectRatio()) {
        prod.pixelAspectRatio = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetAspectRatioConformPolicy()) {
        prod.aspectRatioConformPolicy = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetApertureSize()) {
        prod.apertureSize = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetDataWindowNDC()) {
        prod.dataWindowNDC = _ToRange2f(h->GetTypedValue(0));
    }
    if (auto h = productSchema.GetDisableMotionBlur()) {
        prod.disableMotionBlur = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetDisableDepthOfField()) {
        prod.disableDepthOfField = h->GetTypedValue(0);
    }
    if (auto h = productSchema.GetNamespacedSettings()) {
        prod.namespacedSettings = _ToDictionary(h);
    }
    return prod;
}

HdRenderSettings::RenderProducts
_ToRenderProducts(HdRenderProductVectorSchema productsSchema)
{
    const size_t numProducts = productsSchema.GetNumElements();

    HdRenderSettings::RenderProducts products;
    products.reserve(numProducts);

    for (size_t idx = 0; idx < numProducts; idx++) {
        if (HdRenderProductSchema productSchema =
                                productsSchema.GetElement(idx)) {
            products.push_back(_ToRenderProduct(productSchema));
        }
    }

    return products;
}

VtValue
_GetRenderSettings(HdSceneIndexPrim prim, TfToken const &key)
{
    HdContainerDataSourceHandle renderSettingsDs =
            HdContainerDataSource::Cast(prim.dataSource->Get(
                HdRenderSettingsSchemaTokens->renderSettings));

    HdRenderSettingsSchema rsSchema = HdRenderSettingsSchema(renderSettingsDs);
    if (!rsSchema.IsDefined()) {
        return VtValue();
    }

    if (key == HdRenderSettingsPrimTokens->namespacedSettings) {
        VtDictionary settings;
        if (HdContainerDataSourceHandle namespacedSettingsDs =
                rsSchema.GetNamespacedSettings()) {

            return VtValue(_ToDictionary(namespacedSettingsDs));
        }
    }

    if (key == HdRenderSettingsPrimTokens->active) {
        if (HdBoolDataSourceHandle activeDS = rsSchema.GetActive()) {
            return VtValue(activeDS->GetTypedValue(0));
        }
    }

    if (key == HdRenderSettingsPrimTokens->renderProducts) {
        if (HdRenderProductVectorSchema products =
                rsSchema.GetRenderProducts()) {

            return VtValue(_ToRenderProducts(products));
        }
    }

    if (key == HdRenderSettingsPrimTokens->includedPurposes) {
        if (HdTokenArrayDataSourceHandle purposesDS =
                rsSchema.GetIncludedPurposes()) {

            return VtValue(purposesDS->GetTypedValue(0));
        }
    }

    if (key == HdRenderSettingsPrimTokens->materialBindingPurposes) {
        if (HdTokenArrayDataSourceHandle purposesDS =
                rsSchema.GetMaterialBindingPurposes()) {

            return VtValue(purposesDS->GetTypedValue(0));
        }
    }

    if (key == HdRenderSettingsPrimTokens->renderingColorSpace) {
        if (HdTokenDataSourceHandle colorSpaceDS =
                rsSchema.GetRenderingColorSpace()) {

            return VtValue(colorSpaceDS->GetTypedValue(0));
        }
    }

    if (key == HdRenderSettingsPrimTokens->shutterInterval) {
        if (HdVec2dDataSourceHandle shutterIntervalDS =
                rsSchema.GetShutterInterval()) {

            return VtValue(shutterIntervalDS->GetTypedValue(0));
        }
    }

    return VtValue();
}

template <typename TerminalSchema>
VtValue
_GetRenderTerminalResource(HdSceneIndexPrim prim)
{
    TRACE_FUNCTION();

    // Get Render Terminal Resource as a HdMaterialNodeSchema
    TerminalSchema schema = TerminalSchema::GetFromParent(prim.dataSource);
    if (!schema.IsDefined()) {
        return VtValue();
    }
    HdMaterialNodeSchema nodeSchema = schema.GetResource();
    if (!nodeSchema.IsDefined()) {
        return VtValue();
    }

    // Convert Terminal Resource with material node data to a HdMaterialNode2
    HdMaterialNode2 hdNode2;
    HdTokenDataSourceHandle nodeTypeDS = nodeSchema.GetNodeIdentifier();
    if (nodeTypeDS) {
        hdNode2.nodeTypeId = nodeTypeDS->GetTypedValue(0);
    }

    hdNode2.parameters = _GetHdParamsFromDataSource(nodeSchema.GetParameters());

    return VtValue(hdNode2);
}

HdInterpolation
Hd_InterpolationAsEnum(const TfToken &interpolationToken)
{
    if (interpolationToken == HdPrimvarSchemaTokens->constant) {
        return HdInterpolationConstant;
    } else if (interpolationToken == HdPrimvarSchemaTokens->uniform) {
        return HdInterpolationUniform;
    } else if (interpolationToken == HdPrimvarSchemaTokens->varying) {
        return HdInterpolationVarying;
    } else if (interpolationToken == HdPrimvarSchemaTokens->vertex) {
        return HdInterpolationVertex;
    } else if (interpolationToken == HdPrimvarSchemaTokens->faceVarying) {
        return HdInterpolationFaceVarying;
    } else if (interpolationToken == HdPrimvarSchemaTokens->instance) {
        return HdInterpolationInstance;
    }

    return HdInterpolation(-1);
}

} // anonymous namespace

VtValue
HdSceneIndexAdapterSceneDelegate::_GetImageShaderValue(
    HdSceneIndexPrim prim,
    const TfToken& key)
{
    HdImageShaderSchema imageShaderSchema =
        HdImageShaderSchema::GetFromParent(prim.dataSource);
    if (!imageShaderSchema.IsDefined()) {
        return VtValue();
    }

    if (key == HdImageShaderSchemaTokens->enabled) {
        if (HdBoolDataSourceHandle enabledDs =
                imageShaderSchema.GetEnabled()) {
            return enabledDs->GetValue(0);
        }
    } else if (key == HdImageShaderSchemaTokens->priority) {
        if (HdIntDataSourceHandle priorityDs =
                imageShaderSchema.GetPriority()) {
            return priorityDs->GetValue(0);
        }
    } else if (key == HdImageShaderSchemaTokens->filePath) {
        if (HdStringDataSourceHandle filePathDs =
                imageShaderSchema.GetFilePath()) {
            return filePathDs->GetValue(0);
        }
    } else if (key == HdImageShaderSchemaTokens->constants) {
        if (HdSampledDataSourceContainerSchema constantsSchema =
                imageShaderSchema.GetConstants()) {
            return VtValue(_ToDictionary(constantsSchema));
        }
    } else if (key == HdImageShaderSchemaTokens->materialNetwork) {
        if (HdMaterialNetworkSchema materialNetworkSchema =
                imageShaderSchema.GetMaterialNetwork()) {
            const TfTokenVector renderContexts =
                GetRenderIndex()
                    .GetRenderDelegate()->GetMaterialRenderContexts();
            return VtValue(_ToMaterialNetworkMap(
                materialNetworkSchema, renderContexts));
        }
    }

    return VtValue();
}

HdPrimvarDescriptorVector
HdSceneIndexAdapterSceneDelegate::GetPrimvarDescriptors(
    SdfPath const &id, HdInterpolation interpolation)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _PrimCacheTable::iterator it = _primCache.find(id);
    if (it == _primCache.end()) {
        return {};
    }

    std::shared_ptr<_PrimCacheEntry::PrimvarDescriptorsArray> expected =
        std::atomic_load(&(it->second.primvarDescriptors));
    if (expected) {
        return (*expected)[interpolation];
    }

    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (!prim.dataSource) {
        return {};
    }

    std::shared_ptr<_PrimCacheEntry::PrimvarDescriptorsArray> desired =
        _ComputePrimvarDescriptors(prim.dataSource);

    // Since multiple threads may arrive here at once, we only let the first one
    // write, and later threads discard their version.
    if (std::atomic_compare_exchange_strong(
            &(it->second.primvarDescriptors),
            &expected,
            desired)) {
        return (*desired)[interpolation];
    } else {
        return (*expected)[interpolation];
    }
}

std::shared_ptr<HdSceneIndexAdapterSceneDelegate::_PrimCacheEntry::PrimvarDescriptorsArray>
HdSceneIndexAdapterSceneDelegate::_ComputePrimvarDescriptors(
    const HdContainerDataSourceHandle &primDataSource)
{
    if (!TF_VERIFY(primDataSource)) {
        return nullptr;
    }

    _PrimCacheEntry::PrimvarDescriptorsArray descriptors;

    if (HdPrimvarsSchema primvars =
        HdPrimvarsSchema::GetFromParent(primDataSource)) {
        for (const TfToken &name : primvars.GetPrimvarNames()) {
            HdPrimvarSchema primvar = primvars.GetPrimvar(name);
            if (!primvar) {
                continue;
            }

            HdTokenDataSourceHandle interpolationDataSource =
                primvar.GetInterpolation();

            if (!interpolationDataSource) {
                TF_WARN("HdSceneIndexAdapterSceneDelegate: Skipping primvar "
                        "'%s' due to missing interpolation data source",
                        name.GetText());
                continue;
            }

            TfToken interpolationToken =
                interpolationDataSource->GetTypedValue(0.0f);
            HdInterpolation interpolation =
                Hd_InterpolationAsEnum(interpolationToken);

            if (interpolation >= HdInterpolationCount) {
                TF_WARN("HdSceneIndexAdapterSceneDelegate: Skipping primvar "
                        "'%s' due to invalid interpolation value %i",
                        name.GetText(), interpolation);
                continue;
            }

            TfToken roleToken;
            if (HdTokenDataSourceHandle roleDataSource =
                    primvar.GetRole()) {
                roleToken = roleDataSource->GetTypedValue(0.0f);
            }

            bool indexed = primvar.IsIndexed();

            descriptors[interpolation].push_back(
                {name, interpolation, roleToken, indexed});
        }
    }

    return std::make_shared<_PrimCacheEntry::PrimvarDescriptorsArray>(
            std::move(descriptors));
}

HdExtComputationPrimvarDescriptorVector
HdSceneIndexAdapterSceneDelegate::GetExtComputationPrimvarDescriptors(
    SdfPath const &id, HdInterpolation interpolation)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _PrimCacheTable::iterator it = _primCache.find(id);
    if (it == _primCache.end()) {
        return {};
    }

    std::shared_ptr<_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray> expected =
        std::atomic_load(&(it->second.extCmpPrimvarDescriptors));
    if (expected) {
        return (*expected)[interpolation];
    }

    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (!prim.dataSource) {
        return {};
    }

    std::shared_ptr<_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray> desired =
        _ComputeExtCmpPrimvarDescriptors(prim.dataSource);

    // Since multiple threads may arrive here at once, we only let the first one
    // write, and later threads discard their version.
    if (std::atomic_compare_exchange_strong(
            (&it->second.extCmpPrimvarDescriptors),
            &expected,
            desired)) {
        return (*desired)[interpolation];
    } else {
        return (*expected)[interpolation];
    }
}

std::shared_ptr<HdSceneIndexAdapterSceneDelegate::_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray>
HdSceneIndexAdapterSceneDelegate::_ComputeExtCmpPrimvarDescriptors(
    const HdContainerDataSourceHandle &primDataSource)
{
    if (!TF_VERIFY(primDataSource)) {
        return nullptr;
    }

    _PrimCacheEntry::ExtCmpPrimvarDescriptorsArray descriptors;

    if (HdExtComputationPrimvarsSchema primvars =
        HdExtComputationPrimvarsSchema::GetFromParent(primDataSource)) {
        for (const TfToken &name : primvars.GetExtComputationPrimvarNames()) {
            HdExtComputationPrimvarSchema primvar =
                primvars.GetExtComputationPrimvar(name);
            if (!primvar) {
                continue;
            }

            HdTokenDataSourceHandle interpolationDataSource =
                primvar.GetInterpolation();
            if (!interpolationDataSource) {
                continue;
            }

            TfToken interpolationToken =
                interpolationDataSource->GetTypedValue(0.0f);
            HdInterpolation interpolation =
                Hd_InterpolationAsEnum(interpolationToken);

            if (interpolation >= HdInterpolationCount) {
                continue;
            }

            TfToken roleToken;
            if (HdTokenDataSourceHandle roleDataSource =
                    primvar.GetRole()) {
                roleToken = roleDataSource->GetTypedValue(0.0f);
            }

            SdfPath sourceComputation;
            if (HdPathDataSourceHandle sourceComputationDs =
                    primvar.GetSourceComputation()) {
                sourceComputation = sourceComputationDs->GetTypedValue(0.0f);
            }

            TfToken sourceComputationOutputName;
            if (HdTokenDataSourceHandle sourceComputationOutputDs =
                    primvar.GetSourceComputationOutputName()) {
                sourceComputationOutputName =
                    sourceComputationOutputDs->GetTypedValue(0.0f);
            }

            HdTupleType valueType;
            if (HdTupleTypeDataSourceHandle valueTypeDs =
                    primvar.GetValueType()) {
                valueType = valueTypeDs->GetTypedValue(0.0f);
            }

            descriptors[interpolation].push_back(
                    {name, interpolation, roleToken, sourceComputation,
                     sourceComputationOutputName, valueType});
        }
    }

    return std::make_shared<_PrimCacheEntry::ExtCmpPrimvarDescriptorsArray>(
            std::move(descriptors));
}

VtValue
HdSceneIndexAdapterSceneDelegate::Get(SdfPath const &id, TfToken const &key)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (!prim.dataSource) {
        return VtValue();
    }

    // simpleLight use of Get().
    if (prim.primType == HdPrimTypeTokens->simpleLight) {
        return GetLightParamValue(id, key);
    }

    // camera use of Get().
    if (prim.primType == HdPrimTypeTokens->camera) {
        return GetCameraParamValue(id, key);
    }

    // drawTarget use of Get().
    if (prim.primType == HdPrimTypeTokens->drawTarget) {
        if (HdContainerDataSourceHandle drawTarget =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdPrimTypeTokens->drawTarget))) {
            if (HdSampledDataSourceHandle valueDs =
                    HdSampledDataSource::Cast(drawTarget->Get(key))) {
                return valueDs->GetValue(0.0f);
            }
        }

        return VtValue();
    }

    // volume field use of Get().
    if (HdLegacyPrimTypeIsVolumeField(prim.primType)) {
        HdContainerDataSourceHandle volumeField =
            HdContainerDataSource::Cast(
                prim.dataSource->Get(HdVolumeFieldSchemaTokens->volumeField));
        if (!volumeField) {
            return VtValue();
        }

        HdSampledDataSourceHandle valueDs =
            HdSampledDataSource::Cast(
                volumeField->Get(key));
        if (!valueDs) {
            return VtValue();
        }

        return valueDs->GetValue(0);
    }

    // renderbuffer use of Get().
    if (prim.primType == HdPrimTypeTokens->renderBuffer) {
        if (HdContainerDataSourceHandle renderBuffer =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(
                        HdRenderBufferSchemaTokens->renderBuffer))) {
            if (HdSampledDataSourceHandle valueDs =
                    HdSampledDataSource::Cast(renderBuffer->Get(key))) {
                return valueDs->GetValue(0);
            }
        }

        return VtValue();
    }

    // renderSettings use of Get().
    if (prim.primType == HdPrimTypeTokens->renderSettings) {
        return _GetRenderSettings(prim, key);
    }

    // integrator use of Get().
    if (prim.primType == HdPrimTypeTokens->integrator) {
        if (key == HdIntegratorSchemaTokens->resource) {
            return _GetRenderTerminalResource<HdIntegratorSchema>(prim);
        }
        return VtValue();
    }

    // sampleFilter use of Get().
    if (prim.primType == HdPrimTypeTokens->sampleFilter) {
        if (key == HdSampleFilterSchemaTokens->resource) {
            return _GetRenderTerminalResource<HdSampleFilterSchema>(prim);
        }
        return VtValue();
    }

    // displayFilter use of Get().
    if (prim.primType == HdPrimTypeTokens->displayFilter) {
        if (key == HdDisplayFilterSchemaTokens->resource) {
            return _GetRenderTerminalResource<HdDisplayFilterSchema>(prim);
        }
        return VtValue();
    }

    if (prim.primType == HdPrimTypeTokens->imageShader) {
        return _GetImageShaderValue(prim, key);
    }

    if (prim.primType == HdPrimTypeTokens->cube) {
        if (HdContainerDataSourceHandle cubeSrc =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdCubeSchemaTokens->cube))) {
            if (HdSampledDataSourceHandle valueSrc =
                    HdSampledDataSource::Cast(cubeSrc->Get(key))) {
                return valueSrc->GetValue(0);
            }
        }
    }

    if (prim.primType == HdPrimTypeTokens->sphere) {
        if (HdContainerDataSourceHandle sphereSrc =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdSphereSchemaTokens->sphere))) {
            if (HdSampledDataSourceHandle valueSrc =
                    HdSampledDataSource::Cast(sphereSrc->Get(key))) {
                return valueSrc->GetValue(0);
            }
        }
    }

    if (prim.primType == HdPrimTypeTokens->cylinder) {
        if (HdContainerDataSourceHandle cylinderSrc =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdCylinderSchemaTokens->cylinder))) {
            if (HdSampledDataSourceHandle valueSrc =
                    HdSampledDataSource::Cast(cylinderSrc->Get(key))) {
                return valueSrc->GetValue(0);
            }
        }
    }

    if (prim.primType == HdPrimTypeTokens->cone) {
        if (HdContainerDataSourceHandle coneSrc =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdConeSchemaTokens->cone))) {
            if (HdSampledDataSourceHandle valueSrc =
                    HdSampledDataSource::Cast(coneSrc->Get(key))) {
                return valueSrc->GetValue(0);
            }
        }
    }

    if (prim.primType == HdPrimTypeTokens->capsule) {
        if (HdContainerDataSourceHandle capsuleSrc =
                HdContainerDataSource::Cast(
                    prim.dataSource->Get(HdCapsuleSchemaTokens->capsule))) {
            if (HdSampledDataSourceHandle valueSrc =
                    HdSampledDataSource::Cast(capsuleSrc->Get(key))) {
                return valueSrc->GetValue(0);
            }
        }
    }

    if (prim.primType == HdPrimTypeTokens->coordSys) {
        static TfToken nameKey(
            SdfPath::JoinIdentifier(
                TfTokenVector{HdCoordSysSchema::GetSchemaToken(),
                              HdCoordSysSchemaTokens->name}));
        if (key == nameKey) {
            if (HdTokenDataSourceHandle const nameDs =
                    HdCoordSysSchema::GetFromParent(prim.dataSource)
                        .GetName()) {
                return nameDs->GetValue(0.0f);
            }
        }
    }

    // "primvars" use of Get()
    if (HdPrimvarsSchema primvars =
            HdPrimvarsSchema::GetFromParent(prim.dataSource)) {

        VtValue result = _GetPrimvar(primvars.GetContainer(), key, nullptr);
        if (!result.IsEmpty()) {
            return result;
        }
    }

    // For tasks.
    if (const HdLegacyTaskSchema task =
            HdLegacyTaskSchema::GetFromParent(prim.dataSource)) {
        if (key == HdTokens->params) {
            if (HdSampledDataSourceHandle const ds = task.GetParameters()) {
                return ds->GetValue(0.0f);
            }
        }
        if (key == HdTokens->collection) {
            if (HdRprimCollectionDataSourceHandle const ds = task.GetCollection()) {
                return ds->GetValue(0.0f);
            }
        }
        if (key == HdTokens->renderTags) {
            if (HdTokenVectorDataSourceHandle const ds = task.GetRenderTags()) {
                return ds->GetValue(0.0f);
            }
        }
    }

    // Fallback for unknown prim conventions provided by emulated scene
    // delegate.
    if (HdTypedSampledDataSource<HdSceneDelegate*>::Handle sdDs =
            HdTypedSampledDataSource<HdSceneDelegate*>::Cast(
                prim.dataSource->Get(
                    HdSceneIndexEmulationTokens->sceneDelegate))) {
        if (HdSceneDelegate *delegate = sdDs->GetTypedValue(0.0f)) {
            return delegate->Get(id, key);
        }
    }

    return VtValue();
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetIndexedPrimvar(SdfPath const &id,
    TfToken const &key, VtIntArray *outIndices)
{
    return _GetPrimvar(id, key, outIndices);
}

VtValue
HdSceneIndexAdapterSceneDelegate::_GetPrimvar(SdfPath const &id,
    TfToken const &key, VtIntArray *outIndices)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    if (outIndices) {
        outIndices->clear();
    }
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (!prim.dataSource) {
        return VtValue();
    }

    return _GetPrimvar(
        HdPrimvarsSchema::GetFromParent(prim.dataSource).GetContainer(),
            key, outIndices);

    return VtValue();
}

VtValue
HdSceneIndexAdapterSceneDelegate::_GetPrimvar(
        const HdContainerDataSourceHandle &primvarsDataSource,
        TfToken const &key,
        VtIntArray *outIndices)
{
    HdPrimvarsSchema primvars(primvarsDataSource);
    if (primvars) {
        if (HdPrimvarSchema primvar = primvars.GetPrimvar(key)) {

            if (outIndices) {
                if (HdSampledDataSourceHandle valueDataSource =
                    primvar.GetIndexedPrimvarValue()) {
                    if (HdIntArrayDataSourceHandle
                        indicesDataSource = primvar.GetIndices()) {
                        *outIndices = indicesDataSource->GetTypedValue(0.f);
                    }
                    return valueDataSource->GetValue(0.0f);
                }
            } else {
                if (HdSampledDataSourceHandle valueDataSource =
                    primvar.GetPrimvarValue()) {
                    return valueDataSource->GetValue(0.0f);
                }
            }
        }
    }

    return VtValue();
}

size_t
HdSceneIndexAdapterSceneDelegate::SamplePrimvar(
        SdfPath const &id, TfToken const &key,
        size_t maxSampleCount, float *sampleTimes,
        VtValue *sampleValues)
{
    return _SamplePrimvar(
        id, key, _fallbackStartTime, _fallbackEndTime,
        maxSampleCount, sampleTimes, sampleValues,
        nullptr);
}

size_t
HdSceneIndexAdapterSceneDelegate::SamplePrimvar(
        SdfPath const &id, TfToken const &key,
        float startTime, float endTime,
        size_t maxSampleCount, float *sampleTimes,
        VtValue *sampleValues)
{
    return _SamplePrimvar(
        id, key, startTime, endTime,
        maxSampleCount, sampleTimes, sampleValues,
        nullptr);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleIndexedPrimvar(
        SdfPath const &id, TfToken const &key,
        size_t maxSampleCount, float *sampleTimes,
        VtValue *sampleValues, VtIntArray *sampleIndices)
{
    return _SamplePrimvar(
        id, key, _fallbackStartTime, _fallbackEndTime,
        maxSampleCount, sampleTimes, sampleValues,
        sampleIndices);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleIndexedPrimvar(
        SdfPath const &id, TfToken const &key,
        float startTime, float endTime,
        size_t maxSampleCount, float *sampleTimes,
        VtValue *sampleValues, VtIntArray *sampleIndices)
{
    return _SamplePrimvar(
        id, key, startTime, endTime,
        maxSampleCount, sampleTimes, sampleValues,
        sampleIndices);
}

size_t
HdSceneIndexAdapterSceneDelegate::_SamplePrimvar(
        SdfPath const &id, TfToken const &key,
        float startTime, float endTime,
        size_t maxSampleCount, float *sampleTimes,
        VtValue *sampleValues, VtIntArray *sampleIndices)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdSampledDataSourceHandle valueSource = nullptr;
    HdIntArrayDataSourceHandle indicesSource = nullptr;

    HdPrimvarsSchema primvars =
        HdPrimvarsSchema::GetFromParent(prim.dataSource);
    if (primvars) {
        HdPrimvarSchema primvar = primvars.GetPrimvar(key);
        if (primvar) {
            if (sampleIndices) {
                valueSource = primvar.GetIndexedPrimvarValue();
                indicesSource = primvar.GetIndices();
            } else {
                valueSource = primvar.GetPrimvarValue();
            }
        }
    }

    // NOTE: SamplePrimvar is used by some render delegates to get multiple
    //       samples from camera parameters. While this works from UsdImaging,
    //       it's not due to intentional scene delegate specification but
    //       by UsdImaging fallback behavior which goes directly to USD attrs
    //       in absence of a matching primvar.
    //       In order to support legacy uses of this, we will also check
    //       camera parameter datasources
    if (!valueSource && prim.primType == HdPrimTypeTokens->camera) {
        if (HdCameraSchema cameraSchema =
                HdCameraSchema::GetFromParent(prim.dataSource)) {
            // Ask for the key directly from the schema's container data source
            // as immediate child data source names match the legacy camera
            // parameter names (e.g. focalLength)
            // For a native data source, this will naturally have time samples.
            // For a emulated data source, we are accounting for the possibly
            // that it needs to call SamplePrimvar
            valueSource = HdSampledDataSource::Cast(
                cameraSchema.GetContainer()->Get(key));
        }
    }

    if (!valueSource) {
        return 0;
    }

    std::vector<HdSampledDataSource::Time> times;
    // XXX: If the input prim is a legacy prim, the scene delegate is
    // responsible for setting the shutter window.  We can't query it, but
    // we pass the infinite window to accept all time samples from the
    // scene delegate.
    if (prim.dataSource->Get(HdSceneIndexEmulationTokens->sceneDelegate)) {
        valueSource->GetContributingSampleTimesForInterval(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), &times);

        // XXX fallback to include a single sample
        if (times.empty()) {
            times.push_back(0.0f);
        }
    } else {
        const bool isVarying =
            valueSource->GetContributingSampleTimesForInterval(
                startTime, endTime, &times);
        if (isVarying) {
            if (times.empty()) {
                TF_CODING_ERROR("No contributing sample times returned for "
                                "%s %s even though "
                                "GetContributingSampleTimesForInterval "
                                "indicated otherwise.",
                                id.GetText(), key.GetText());
                times.push_back(0.0f);
            }
        } else {
            times = { 0.0f };
        }
    }

    const size_t authoredSamples = times.size();
    if (authoredSamples > maxSampleCount) {
        times.resize(maxSampleCount);
    }

    for (size_t i = 0; i < times.size(); ++i) {
        sampleTimes[i] = times[i];
        sampleValues[i] = valueSource->GetValue(times[i]);
        if (sampleIndices) {
            if (indicesSource) {
                // Can assume indices source has same sample times as primvar
                // value source.
                sampleIndices[i] = indicesSource->GetTypedValue(times[i]);
            } else {
                sampleIndices[i].clear();
            }
        }

    }

    return authoredSamples;
}

GfMatrix4d
HdSceneIndexAdapterSceneDelegate::GetTransform(SdfPath const & id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    GfMatrix4d m;
    m.SetIdentity();

    HdSceneIndexPrim prim = _GetInputPrim(id);

    if (HdXformSchema xformSchema = HdXformSchema::GetFromParent(
            prim.dataSource)) {
        if (HdMatrixDataSourceHandle matrixSource =
                xformSchema.GetMatrix()) {

            m = matrixSource->GetTypedValue(0.0f);
        }
    }

    return m;
}

GfMatrix4d
HdSceneIndexAdapterSceneDelegate::GetInstancerTransform(SdfPath const & id)
{
    return GetTransform(id);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleTransform(
        SdfPath const &id, size_t maxSampleCount,
        float *sampleTimes, GfMatrix4d *sampleValues)
{
    return SampleTransform(
        id, _fallbackStartTime, _fallbackEndTime,
        maxSampleCount, sampleTimes, sampleValues);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleTransform(
        SdfPath const &id, float startTime, float endTime,
        size_t maxSampleCount,
        float *sampleTimes, GfMatrix4d *sampleValues)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(id);

    HdXformSchema xformSchema =
        HdXformSchema::GetFromParent(prim.dataSource);
    if (!xformSchema) {
        return 0;
    }
    HdMatrixDataSourceHandle matrixSource = xformSchema.GetMatrix();
    if (!matrixSource) {
        return 0;
    }

    std::vector<HdSampledDataSource::Time> times;
    // XXX: If the input prim is a legacy prim, the scene delegate is
    // responsible for setting the shutter window.  We can't query it, but
    // we pass the infinite window to accept all time samples from the
    // scene delegate.
    if (prim.dataSource->Get(HdSceneIndexEmulationTokens->sceneDelegate)) {
        matrixSource->GetContributingSampleTimesForInterval(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), &times);
    } else {
        matrixSource->GetContributingSampleTimesForInterval(
            startTime, endTime, &times);
    }

    // XXX fallback to include a single sample
    if (times.empty()) {
        times.push_back(0.0f);
    }

    size_t authoredSamples = times.size();
    if (authoredSamples > maxSampleCount) {
        times.resize(maxSampleCount);
    }

    for (size_t i = 0; i < times.size(); ++i) {
        sampleTimes[i] = times[i];
        sampleValues[i] = matrixSource->GetTypedValue(times[i]);
    }

    return authoredSamples;
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleInstancerTransform(
        SdfPath const &id, size_t maxSampleCount,
        float *sampleTimes, GfMatrix4d *sampleValues)
{
    return SampleTransform(id, maxSampleCount, sampleTimes, sampleValues);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleInstancerTransform(
        SdfPath const &id, float startTime, float endTime,
        size_t maxSampleCount,
        float *sampleTimes, GfMatrix4d *sampleValues)
{
    return SampleTransform(
        id, startTime, endTime, maxSampleCount, sampleTimes, sampleValues);
}

std::vector<VtArray<TfToken>>
HdSceneIndexAdapterSceneDelegate::GetInstanceCategories(
    SdfPath const &instancerId) {
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    std::vector<VtArray<TfToken>> result;

    HdSceneIndexPrim prim = _GetInputPrim(instancerId);

    if (HdInstanceCategoriesSchema instanceCategories =
            HdInstanceCategoriesSchema::GetFromParent(prim.dataSource)) {

        if (HdVectorDataSourceHandle values =
                instanceCategories.GetCategoriesValues())
        {
            static const VtArray<TfToken> emptyValue;
            if (values) {
                result.reserve(values->GetNumElements());
                for (size_t i = 0, e = values->GetNumElements(); i != e; ++i) {

                    if (HdCategoriesSchema value = HdContainerDataSource::Cast(
                            values->GetElement(i))) {
                        // TODO, deduplicate by address
                        result.push_back(value.GetIncludedCategoryNames());
                    } else {
                        result.push_back(emptyValue);
                    }
                }
            }
        }
    }

    return result;
}


VtIntArray
HdSceneIndexAdapterSceneDelegate::GetInstanceIndices(
    SdfPath const &instancerId,
    SdfPath const &prototypeId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    VtIntArray indices;

    HdSceneIndexPrim prim = _GetInputPrim(instancerId);

    if (HdInstancerTopologySchema instancerTopology =
            HdInstancerTopologySchema::GetFromParent(prim.dataSource)) {
        indices = instancerTopology.ComputeInstanceIndicesForProto(prototypeId);
    }

    return indices;
}

SdfPathVector
HdSceneIndexAdapterSceneDelegate::GetInstancerPrototypes(
        SdfPath const &instancerId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    SdfPathVector prototypes;

    HdSceneIndexPrim prim = _GetInputPrim(instancerId);

    if (HdInstancerTopologySchema instancerTopology =
            HdInstancerTopologySchema::GetFromParent(prim.dataSource)) {
        HdPathArrayDataSourceHandle protoDs =
            instancerTopology.GetPrototypes();
        if (protoDs) {
            VtArray<SdfPath> protoArray = protoDs->GetTypedValue(0);
            prototypes.assign(protoArray.begin(), protoArray.end());
        }
    }

    return prototypes;
}

SdfPath
HdSceneIndexAdapterSceneDelegate::GetInstancerId(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath instancerId;

    HdSceneIndexPrim prim = _GetInputPrim(id);

    if (HdInstancedBySchema instancedBy =
            HdInstancedBySchema::GetFromParent(prim.dataSource)) {
        VtArray<SdfPath> instancerIds;
        if (HdPathArrayDataSourceHandle instancerIdsDs =
                instancedBy.GetPaths()) {
            instancerIds = instancerIdsDs->GetTypedValue(0);
        }

        // XXX: Right now the scene delegate can't handle multiple
        // instancers, so we rely on upstream ops to make the size <= 1.
        if (instancerIds.size() > 1) {
            TF_CODING_ERROR("Prim <%s> has multiple instancer ids, using first.",
                id.GetText());
        }

        if (instancerIds.size() > 0) {
            instancerId = instancerIds.cfront();
        }
    }

    return instancerId;
}

TfTokenVector
HdSceneIndexAdapterSceneDelegate::GetExtComputationSceneInputNames(
        SdfPath const &computationId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    const HdSampledDataSourceContainerSchema inputValues =
        extComputation.GetInputValues();
    return inputValues.GetNames();
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetExtComputationInput(
        SdfPath const &computationId, TfToken const &input)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    if (!extComputation) {
        return {};
    }

    if (input == HdTokens->dispatchCount) {
        HdSizetDataSourceHandle const dispatchDs =
            extComputation.GetDispatchCount();
        if (!dispatchDs) {
            return {};
        }
        return dispatchDs->GetValue(0);
    }

    if (input == HdTokens->elementCount) {
        HdSizetDataSourceHandle const elementDs =
            extComputation.GetElementCount();
        if (!elementDs) {
            return {};
        }
        return elementDs->GetValue(0);
    }

    const HdSampledDataSourceContainerSchema inputValues =
        extComputation.GetInputValues();
    HdSampledDataSourceHandle const valueDs = inputValues.Get(input);
    if (!valueDs) {
        return {};
    }
    return valueDs->GetValue(0);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleExtComputationInput(
        SdfPath const &computationId, TfToken const &input,
        size_t maxSampleCount,
        float *sampleTimes, VtValue *sampleValues)
{
    return
        SampleExtComputationInput(
            computationId, input,
            _fallbackStartTime, _fallbackEndTime,
            maxSampleCount, sampleTimes, sampleValues);
}

size_t
HdSceneIndexAdapterSceneDelegate::SampleExtComputationInput(
        SdfPath const &computationId, TfToken const &input,
        float startTime, float endTime,
        size_t maxSampleCount,
        float *sampleTimes, VtValue *sampleValues)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    if (!extComputation) {
        return 0;
    }

    const HdSampledDataSourceContainerSchema inputValues =
        extComputation.GetInputValues();
    HdSampledDataSourceHandle const valueDs = inputValues.Get(input);
    if (!valueDs) {
        return 0;
    }

    std::vector<HdSampledDataSource::Time> times;
    // XXX: If the input prim is a legacy prim, the scene delegate is
    // responsible for setting the shutter window.  We can't query it, but
    // we pass the infinite window to accept all time samples from the
    // scene delegate.
    if (prim.dataSource->Get(HdSceneIndexEmulationTokens->sceneDelegate)) {
        valueDs->GetContributingSampleTimesForInterval(
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::max(), &times);
    } else {
        valueDs->GetContributingSampleTimesForInterval(
                startTime, endTime, &times);
    }

    size_t authoredSamples = times.size();
    if (authoredSamples > maxSampleCount) {
        times.resize(maxSampleCount);
    }

    // XXX fallback to include a single sample
    if (times.empty()) {
        times.push_back(0.0f);
    }

    for (size_t i = 0; i < times.size(); ++i) {
        sampleTimes[i] = times[i];
        sampleValues[i] = valueDs->GetValue(times[i]);
    }

    return authoredSamples;
}

HdExtComputationInputDescriptorVector
HdSceneIndexAdapterSceneDelegate::GetExtComputationInputDescriptors(
        SdfPath const &computationId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdExtComputationInputDescriptorVector result;

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    const HdExtComputationInputComputationContainerSchema inputComputations =
        extComputation.GetInputComputations();
    const TfTokenVector names = inputComputations.GetNames();
    result.reserve(names.size());
    for (const TfToken &name : names) {
        const HdExtComputationInputComputationSchema input =
            inputComputations.Get(name);
        if (!input) {
            continue;
        }
        HdExtComputationInputDescriptor desc;
        desc.name = name;
        if (HdPathDataSourceHandle const srcDs =
                input.GetSourceComputation()) {
            desc.sourceComputationId = srcDs->GetTypedValue(0);
        }
        if (HdTokenDataSourceHandle const srcNameDs =
                input.GetSourceComputationOutputName()) {
            desc.sourceComputationOutputName = srcNameDs->GetTypedValue(0);
        }
        result.push_back(desc);
    }

    return result;
}

HdExtComputationOutputDescriptorVector
HdSceneIndexAdapterSceneDelegate::GetExtComputationOutputDescriptors(
        SdfPath const &computationId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdExtComputationOutputDescriptorVector result;

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    const HdExtComputationOutputContainerSchema outputs =
        extComputation.GetOutputs();
    const TfTokenVector names = outputs.GetNames();
    result.reserve(names.size());
    for (const TfToken &name : names) {
        const HdExtComputationOutputSchema output = outputs.Get(name);
        if (!output) {
            continue;
        }

        HdExtComputationOutputDescriptor desc;
        desc.name = name;
        if (HdTupleTypeDataSourceHandle const typeDs = output.GetValueType()) {
            desc.valueType = typeDs->GetTypedValue(0);
        }
        result.push_back(desc);
    }

    return result;
}

std::string
HdSceneIndexAdapterSceneDelegate::GetExtComputationKernel(
        SdfPath const &computationId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdSceneIndexPrim prim = _GetInputPrim(computationId);
    if (HdExtComputationSchema extComputation =
            HdExtComputationSchema::GetFromParent(prim.dataSource)) {
        HdStringDataSourceHandle ds = extComputation.GetGlslKernel();
        if (ds) {
            return ds->GetTypedValue(0);
        }
    }
    return std::string();
}

void
HdSceneIndexAdapterSceneDelegate::InvokeExtComputation(
        SdfPath const &computationId, HdExtComputationContext * const context)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const HdSceneIndexPrim prim = _GetInputPrim(computationId);
    const HdExtComputationSchema extComputation =
        HdExtComputationSchema::GetFromParent(prim.dataSource);
    HdExtComputationCpuCallbackDataSourceHandle const ds =
        extComputation.GetCpuCallback();
    if (!ds) {
        return;
    }
    HdExtComputationCpuCallbackSharedPtr const callback =
        ds->GetTypedValue(0.0f);
    if (!callback) {
        return;
    }
    callback->Compute(context);
}

TfTokenVector
HdSceneIndexAdapterSceneDelegate::GetTaskRenderTags(SdfPath const &taskId)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();
    const HdSceneIndexPrim prim = _GetInputPrim(taskId);
    const HdLegacyTaskSchema task = HdLegacyTaskSchema::GetFromParent(prim.dataSource);
    HdTokenVectorDataSourceHandle const ds = task.GetRenderTags();
    if (!ds) {
        return {};
    }
    return ds->GetTypedValue(0.0f);
}

void 
HdSceneIndexAdapterSceneDelegate::Sync(HdSyncRequestVector* request)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!request || request->IDs.size() == 0) {
        return;
    }

    // Drop per-thread scene index input prim cache
    _inputPrimCache.clear();

    if (!_sceneDelegatesBuilt) {
        tbb::concurrent_unordered_set<HdSceneDelegate*> sds;
        _primCache.ParallelForEach(
            [this, &sds](const SdfPath &k, const _PrimCacheEntry &v) {
                HdSceneIndexPrim prim = _inputSceneIndex->GetPrim(k);
                if (!prim.dataSource) {
                    return;
                }

                HdTypedSampledDataSource<HdSceneDelegate*>::Handle ds =
                    HdTypedSampledDataSource<HdSceneDelegate*>::Cast(
                        prim.dataSource->Get(
                            HdSceneIndexEmulationTokens->sceneDelegate));
                if (!ds) {
                    return;
                }

                sds.insert(ds->GetTypedValue(0));
            });
        _sceneDelegates.assign(sds.begin(), sds.end());
        _sceneDelegatesBuilt = true;
    }

    for (auto sd : _sceneDelegates) {
        if (TF_VERIFY(sd != nullptr)) {
            sd->Sync(request);
        }
    }
}

void
HdSceneIndexAdapterSceneDelegate::PostSyncCleanup()
{
    if (!_sceneDelegatesBuilt) {
        return;
    }

    for (auto sd : _sceneDelegates) {
        if (TF_VERIFY(sd != nullptr)) {
            sd->PostSyncCleanup();
        }
    }

    // Drop per-thread scene index input prim cache
    _inputPrimCache.clear();
}

// ----------------------------------------------------------------------------

HdDisplayStyle
HdSceneIndexAdapterSceneDelegate::GetDisplayStyle(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdDisplayStyle result;
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (HdLegacyDisplayStyleSchema styleSchema =
            HdLegacyDisplayStyleSchema::GetFromParent(prim.dataSource)) {

        if (HdIntDataSourceHandle ds =
                styleSchema.GetRefineLevel()) {
            result.refineLevel = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetFlatShadingEnabled()) {
            result.flatShadingEnabled = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetDisplacementEnabled()) {
            result.displacementEnabled = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetDisplayInOverlay()) {
            result.displayInOverlay = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetOccludedSelectionShowsThrough()) {
            result.occludedSelectionShowsThrough = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetPointsShadingEnabled()) {
            result.pointsShadingEnabled = ds->GetTypedValue(0.0f);
        }

        if (HdBoolDataSourceHandle ds =
                styleSchema.GetMaterialIsFinal()) {
            result.materialIsFinal = ds->GetTypedValue(0.0f);
        }
    }

    return result;
}

VtValue
HdSceneIndexAdapterSceneDelegate::GetShadingStyle(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    VtValue result;
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (HdLegacyDisplayStyleSchema styleSchema =
            HdLegacyDisplayStyleSchema::GetFromParent(prim.dataSource)) {

        if (HdTokenDataSourceHandle ds =
                styleSchema.GetShadingStyle()) {
            TfToken st = ds->GetTypedValue(0.0f);
            result = VtValue(st);
        }
    }

    return result;
}

HdReprSelector
HdSceneIndexAdapterSceneDelegate::GetReprSelector(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdReprSelector result;
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (HdLegacyDisplayStyleSchema styleSchema =
            HdLegacyDisplayStyleSchema::GetFromParent(prim.dataSource)) {

        if (HdTokenArrayDataSourceHandle ds =
                styleSchema.GetReprSelector()) {
            VtArray<TfToken> ar = ds->GetTypedValue(0.0f);
            ar.resize(HdReprSelector::MAX_TOPOLOGY_REPRS);
            result = HdReprSelector(ar[0], ar[1], ar[2]);
        }
    }

    return result;
}

HdCullStyle
HdSceneIndexAdapterSceneDelegate::GetCullStyle(SdfPath const &id)
{
    TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    HdCullStyle result = HdCullStyleDontCare;
    HdSceneIndexPrim prim = _GetInputPrim(id);
    if (HdLegacyDisplayStyleSchema styleSchema =
            HdLegacyDisplayStyleSchema::GetFromParent(prim.dataSource)) {

        if (HdTokenDataSourceHandle ds =
                styleSchema.GetCullStyle()) {
            TfToken ct = ds->GetTypedValue(0.0f);
            if (ct == HdCullStyleTokens->nothing) {
                result = HdCullStyleNothing;
            } else if (ct == HdCullStyleTokens->back) {
                result = HdCullStyleBack;
            } else if (ct == HdCullStyleTokens->front) {
                result = HdCullStyleFront;
            } else if (ct == HdCullStyleTokens->backUnlessDoubleSided) {
                result = HdCullStyleBackUnlessDoubleSided;
            } else if (ct == HdCullStyleTokens->frontUnlessDoubleSided) {
                result = HdCullStyleFrontUnlessDoubleSided;
            } else {
                result = HdCullStyleDontCare;
            }
        }
    }

    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
