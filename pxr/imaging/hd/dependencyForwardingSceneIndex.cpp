//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/dependencyForwardingSceneIndex.h"
#include "pxr/imaging/hd/dependenciesSchema.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/work/loops.h"

PXR_NAMESPACE_OPEN_SCOPE

//----------------------------------------------------------------------------

HdDependencyForwardingSceneIndex::HdDependencyForwardingSceneIndex(
    HdSceneIndexBaseRefPtr inputScene)
: HdSingleInputFilteringSceneIndexBase(inputScene)
, _manualGarbageCollect(false)
{
}

void
HdDependencyForwardingSceneIndex::SetManualGarbageCollect(
    bool manualGarbageCollect) {
    _manualGarbageCollect = manualGarbageCollect;
}

HdSceneIndexPrim
HdDependencyForwardingSceneIndex::GetPrim(const SdfPath &primPath) const
{
    if (_affectedPrimToDependsOnPathsMap.find(primPath) == 
            _affectedPrimToDependsOnPathsMap.end()) {
        _UpdateDependencies(primPath);
    }

    return _GetInputSceneIndex()->GetPrim(primPath);
}

SdfPathVector
HdDependencyForwardingSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    // pass through without change
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdDependencyForwardingSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    TRACE_FUNCTION();

    _VisitedNodeSet visited;
    _AdditionalDirtiedVector additionalDirtied;
    _PathSet rebuildDependencies;

    WorkParallelForN(entries.size(),
    [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const HdSceneIndexObserver::AddedPrimEntry &entry = entries[i];

        // If this prim shows up in the dependency map, make sure we clear
        // cached dependency data.

        // Clear out the affected prim map.
        _AffectedPrimToDependsOnPathsEntryMap::iterator ait =
            _affectedPrimToDependsOnPathsMap.find(entry.primPath);
        if (ait != _affectedPrimToDependsOnPathsMap.end()) {
            static const HdDataSourceLocator &depsLoc =
                HdDependenciesSchema::GetDefaultLocator();
            _VisitedNode visitedNode = { entry.primPath, depsLoc };
            visited.insert(visitedNode);
            _ClearDependencies(entry.primPath);
        }

        // Clear out the depended on map, sending out additional dirty notices
        // if necessary.
        _DependedOnPrimsAffectedPrimsMap::iterator dit =
            _dependedOnPrimToDependentsMap.find(entry.primPath);
        if (dit != _dependedOnPrimToDependentsMap.end()) {
            _potentiallyDeletedDependedOnPaths.insert(entry.primPath);

            for (auto &affectedPair : (*dit).second) {
                affectedPair.second.flaggedForDeletion = true;
                _potentiallyDeletedAffectedPaths.insert(affectedPair.first);
                const SdfPath &affectedPrimPath = affectedPair.first;

                // Filter out self-dependencies.
                if (affectedPrimPath == entry.primPath) {
                    continue;
                }

                // Dirty affected prims.
                HdDataSourceLocatorSet affectedLocators;
                for (const auto &keyEntryPair :
                        affectedPair.second.locatorsEntryMap) {
                    const _LocatorsEntry &entry = keyEntryPair.second;
                    affectedLocators.insert(entry.affectedDataSourceLocator);
                }

                if (!affectedLocators.IsEmpty()) {
                    additionalDirtied.emplace_back(affectedPrimPath,
                            affectedLocators);
                    _PrimDirtied(affectedPrimPath, affectedLocators,
                            &visited, &additionalDirtied, &rebuildDependencies);
                }
            }
        }
    }});

    for (SdfPath const& rebuildPath : rebuildDependencies) {
        _ClearDependencies(rebuildPath);
        _UpdateDependencies(rebuildPath);
    }

    if (!_manualGarbageCollect) {
        RemoveDeletedEntries(nullptr, nullptr);
    }

    _SendPrimsAdded(entries);

    if (!additionalDirtied.empty()) {
        HdSceneIndexObserver::DirtiedPrimEntries flattened(
            additionalDirtied.begin(), additionalDirtied.end());
        _SendPrimsDirtied(flattened);
    }
}

void
HdDependencyForwardingSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    TRACE_FUNCTION();

    // Fastpath for root delete.
    // Note we only special case Remove { "/" }, since searching the whole
    // entry vector for root would add a bunch of processing time.
    if (entries.size() > 0 && entries[0].primPath.IsAbsoluteRootPath()) {
        if (!_manualGarbageCollect) {
            _dependedOnPrimToDependentsMap.clear();
            _affectedPrimToDependsOnPathsMap.clear();
            _potentiallyDeletedDependedOnPaths.clear();
            _potentiallyDeletedAffectedPaths.clear();
        } else {
            for (auto &pair : _dependedOnPrimToDependentsMap) {
                _potentiallyDeletedDependedOnPaths.insert(pair.first);
                for (auto &locPair : pair.second) {
                    locPair.second.flaggedForDeletion = true;
                }
            }
            for (auto &pair : _affectedPrimToDependsOnPathsMap) {
                _potentiallyDeletedAffectedPaths.insert(pair.first);
                pair.second.flaggedForDeletion = true;
            }
        }
        _SendPrimsRemoved(entries);
        return;
    }

    // Normal path
    _VisitedNodeSet visited;
    _AdditionalDirtiedVector additionalDirtied;
    _PathSet rebuildDependencies;

    WorkParallelForN(entries.size(),
    [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const HdSceneIndexObserver::RemovedPrimEntry &entry = entries[i];

        // entry.primPath is recursive so we need to iterate both maps looking
        // for entries to invalidate.

        // Iterate _affectedPrimToDependsOnPathsMap looking for:
        // - Affected prims that are descendants of primPath.
        // Clear their dependency info out.
        WorkParallelForTBBRange(_affectedPrimToDependsOnPathsMap.range(),
        [&](const _AffectedPrimToDependsOnPathsEntryMap::range_type& range) {
        for (auto it = range.begin(); it != range.end(); ++it) {
            _AffectedPrimToDependsOnPathsEntryMap::value_type& pair = *it;
            if (pair.first.HasPrefix(entry.primPath)) {
                static const HdDataSourceLocator &depsLoc =
                    HdDependenciesSchema::GetDefaultLocator();
                _VisitedNode visitedNode = { pair.first, depsLoc };
                visited.insert(visitedNode);
                _ClearDependencies(pair.first);
            }
        }});

        // Iterate _dependedOnPrimToDependentsMap looking for:
        // - Depended on prims that are descendants of primPath.
        // Clear their dependency info out.
        // Additionally:
        // - Affected prims that are *not* descendants of primPath.
        // Send out relevant dirty notices.
        WorkParallelForTBBRange(_dependedOnPrimToDependentsMap.range(),
        [&](const _DependedOnPrimsAffectedPrimsMap::range_type& range) {
        for (auto it = range.begin(); it != range.end(); ++it) {
            _DependedOnPrimsAffectedPrimsMap::value_type& depPair = *it;
            if (!depPair.first.HasPrefix(entry.primPath)) {
                continue;
            }

            // Mark the dependedOnPrim for deletion.
            _potentiallyDeletedDependedOnPaths.insert(depPair.first);

            WorkParallelForTBBRange(depPair.second.range(),
            [&](const _AffectedPrimsDependencyMap::range_type& range) {
            for (auto it = range.begin(); it != range.end(); ++it) {
                _AffectedPrimsDependencyMap::value_type& affPair = *it;
                // Mark the dependedOnPrim->affectedPrim entry for deletion.
                affPair.second.flaggedForDeletion = true;
                _potentiallyDeletedAffectedPaths.insert(affPair.first);

                // Don't send invalidations to the deleted subtree.
                if (affPair.first.HasPrefix(entry.primPath)) {
                    continue;
                }

                // send invalidations
                HdDataSourceLocatorSet affectedLocators;
                for (const auto &keyEntryPair :
                        affPair.second.locatorsEntryMap) {
                    const _LocatorsEntry &entry = keyEntryPair.second;
                    affectedLocators.insert(entry.affectedDataSourceLocator);
                }

                if (!affectedLocators.IsEmpty()) {
                    additionalDirtied.emplace_back(affPair.first,
                        affectedLocators);
                    _PrimDirtied(affPair.first, affectedLocators,
                        &visited, &additionalDirtied, &rebuildDependencies);
                }
            }});
        }});
    }});

    // Dependency rebuild done after _PrimDirtied for consistent semantics,
    // and so that we don't invalidate iterators everywhere...
    for (SdfPath const& rebuildPath : rebuildDependencies) {
        _ClearDependencies(rebuildPath);
        _UpdateDependencies(rebuildPath);
    }

    if (!_manualGarbageCollect) {
        RemoveDeletedEntries(nullptr, nullptr);
    }

    _SendPrimsRemoved(entries);

    if (!additionalDirtied.empty()) {
        HdSceneIndexObserver::DirtiedPrimEntries flattened(
            additionalDirtied.begin(), additionalDirtied.end());
        _SendPrimsDirtied(flattened);
    }
}

void
HdDependencyForwardingSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    TRACE_FUNCTION();

    _VisitedNodeSet visited;
    _AdditionalDirtiedVector additionalEntries;
    _PathSet rebuildDependencies;

    static const HdDataSourceLocator &depsLoc =
            HdDependenciesSchema::GetDefaultLocator();

    WorkParallelForN(entries.size(),
    [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const HdSceneIndexObserver::DirtiedPrimEntry &entry = entries[i];
        // Check if dependencies were directly invalidated.
        if (entry.dirtyLocators.Intersects(depsLoc)) {
            _VisitedNode visitedNode = {entry.primPath, depsLoc};
            if (visited.find(visitedNode) == visited.end()) {
                visited.insert(visitedNode);
                rebuildDependencies.insert(entry.primPath);
            }
        }
        _PrimDirtied(entry.primPath, entry.dirtyLocators,
            &visited, &additionalEntries, &rebuildDependencies);
    }});

    // Dependency rebuild done after _PrimDirtied for consistent semantics,
    // and so that we don't invalidate iterators everywhere...
    for (SdfPath const& rebuildPath : rebuildDependencies) {
        _ClearDependencies(rebuildPath);
        _UpdateDependencies(rebuildPath);
    }

    if (!_manualGarbageCollect) {
        RemoveDeletedEntries(nullptr, nullptr);
    }

    if (additionalEntries.empty()) {
        _SendPrimsDirtied(entries);
    } else {
        HdSceneIndexObserver::DirtiedPrimEntries flattened(
            entries.begin(), entries.end());
        flattened.insert(flattened.end(), additionalEntries.begin(),
            additionalEntries.end());
        _SendPrimsDirtied(flattened);
    }
}

void
HdDependencyForwardingSceneIndex::_PrimDirtied(
    const SdfPath &primPath,
    const HdDataSourceLocatorSet &sourceLocatorSet,
    _VisitedNodeSet *visited,
    _AdditionalDirtiedVector *moreDirtiedEntries,
    _PathSet *rebuildDependencies)
{
    if (!TF_VERIFY(visited) || !TF_VERIFY(rebuildDependencies)) {
        return;
    }

    static const HdDataSourceLocator &depsLoc =
            HdDependenciesSchema::GetDefaultLocator();

    // check me in the reverse update table
    _DependedOnPrimsAffectedPrimsMap::iterator it =
        _dependedOnPrimToDependentsMap.find(primPath);
    if (it == _dependedOnPrimToDependentsMap.end()) {
        return;
    }

    WorkParallelForTBBRange(it->second.range(),
    [&](const _AffectedPrimsDependencyMap::range_type& range) {
    for (auto it = range.begin(); it != range.end(); ++it) {
        const _AffectedPrimsDependencyMap::value_type& affectedPair = *it;
        const SdfPath &affectedPrimPath = affectedPair.first;
        HdDataSourceLocatorSet affectedLocators;

        // now dirty any dependencies
        for (const auto &keyEntryPair : affectedPair.second.locatorsEntryMap) {
            const _LocatorsEntry &entry = keyEntryPair.second;

            if (!sourceLocatorSet.Intersects(
                    entry.dependedOnDataSourceLocator)) {
                continue;
            }

            // Don't recurse if we've seen this invalidation before...
            _VisitedNode visitedNode =
                {affectedPrimPath, entry.affectedDataSourceLocator};
            if (visited->find(visitedNode) == visited->end()) {
                visited->insert(visitedNode);
                affectedLocators.insert(entry.affectedDataSourceLocator);
                if (entry.affectedDataSourceLocator == depsLoc) {
                    rebuildDependencies->insert(affectedPrimPath);
                }
                else if (entry.affectedDataSourceLocator.Intersects(depsLoc)) {
                    _VisitedNode depsNode = { affectedPrimPath, depsLoc };
                    if (visited->find(depsNode) == visited->end()) {
                        visited->insert(depsNode);
                        rebuildDependencies->insert(affectedPrimPath);
                    }
                }
            }
        }

        // Add the dependent invalidations & recurse
        if (!affectedLocators.IsEmpty()) {
            moreDirtiedEntries->emplace_back(affectedPrimPath,
                affectedLocators);

            _PrimDirtied(affectedPrimPath, affectedLocators,
                visited, moreDirtiedEntries, rebuildDependencies);
        }
    }});
}

//----------------------------------------------------------------------------

// when called?
// 1) when our own __dependencies are dirtied
// 2) when someone asks for our prim

void 
HdDependencyForwardingSceneIndex::_ClearDependencies(const SdfPath &primPath)
{
    _AffectedPrimToDependsOnPathsEntryMap::iterator it =
        _affectedPrimToDependsOnPathsMap.find(primPath);
    if (it == _affectedPrimToDependsOnPathsMap.end()) {
        return;
    }

    // Mark the affected entry for garbage collection.
    _AffectedPrimToDependsOnPathsEntry &affectedPrimEntry = (*it).second;
    affectedPrimEntry.flaggedForDeletion = true;
    _potentiallyDeletedAffectedPaths.insert(primPath);

    // Flag entries within our depended-on prims and add those prims to the
    // set of paths which should be checked during RemoveDeletedEntries
    const _PathSet &dependsOnPaths = affectedPrimEntry.dependsOnPaths;
    for (const SdfPath &dependedOnPrimPath : dependsOnPaths) {
        _DependedOnPrimsAffectedPrimsMap::iterator dependedOnPrimIt =
                _dependedOnPrimToDependentsMap.find(dependedOnPrimPath);

        if (dependedOnPrimIt == _dependedOnPrimToDependentsMap.end()) {
            continue;
        }

        _AffectedPrimsDependencyMap &_affectedPrimsMap =
            (*dependedOnPrimIt).second;


        _AffectedPrimsDependencyMap::iterator thisAffectedPrimEntryIt =
            _affectedPrimsMap.find(primPath);
        if (thisAffectedPrimEntryIt == _affectedPrimsMap.end()) {
            continue;
        }

        (*thisAffectedPrimEntryIt).second.flaggedForDeletion = true;
        _potentiallyDeletedDependedOnPaths.insert(dependedOnPrimPath);
    }
}

//----------------------------------------------------------------------------

void
HdDependencyForwardingSceneIndex::_UpdateDependencies(
    const SdfPath &primPath) const
{
    HdContainerDataSourceHandle primDataSource =
        _GetInputSceneIndex()->GetPrim(primPath).dataSource;

    if (!primDataSource) {
        return;
    }

    HdDependenciesSchema dependenciesSchema =
            HdDependenciesSchema::GetFromParent(primDataSource);

    // NOTE: This early exit prevents addition of an entry within
    //       _affectedPrimToDependsOnPathsMap if there isn't one already.
    //       The trade-off is repeatedly doing this check vs adding an entry
    //       for every prim which doesn't have dependencies.
    if (!dependenciesSchema.IsDefined()) {
        return;
    }

    // presence (even if empty) indicates we've been checked
    // NOTE: we only add to this set. We'll remove entries (and the map itself)
    //       as part of single-threaded clearing.
    
    _AffectedPrimToDependsOnPathsEntry &dependsOnPathsEntry = 
        _affectedPrimToDependsOnPathsMap[primPath];

    dependsOnPathsEntry.flaggedForDeletion = false;

    _PathSet &dependsOnPaths = dependsOnPathsEntry.dependsOnPaths;

    for (HdDependenciesSchema::EntryPair &entryPair :
            dependenciesSchema.GetEntries()) {

        TfToken &entryName = entryPair.first;
        HdDependencySchema &depSchema = entryPair.second;

        if (!depSchema.IsDefined()) {
           continue;
        }

        SdfPath dependedOnPrimPath;
        if (HdPathDataSourceHandle dependedOnPrimPathDataSource =
                depSchema.GetDependedOnPrimPath()) {
            dependedOnPrimPath =
                dependedOnPrimPathDataSource->GetTypedValue(0.0f);
        }

        HdDataSourceLocator dependedOnDataSourceLocator;
        HdDataSourceLocator affectedSourceLocator;

        if (HdLocatorDataSourceHandle lds =
                depSchema.GetDependedOnDataSourceLocator()) {
            dependedOnDataSourceLocator = lds->GetTypedValue(0.0f);
        }

        if (HdLocatorDataSourceHandle lds =
                depSchema.GetAffectedDataSourceLocator()) {
            affectedSourceLocator = lds->GetTypedValue(0.0f);
        }

        // self dependency
        if (dependedOnPrimPath.IsEmpty()) {
            dependedOnPrimPath = primPath;
        }

        dependsOnPaths.insert(dependedOnPrimPath);

        _AffectedPrimsDependencyMap &reverseDependencies =
            _dependedOnPrimToDependentsMap[dependedOnPrimPath];

        _AffectedPrimDependencyEntry &reverseDependenciesEntry =
            reverseDependencies[primPath];

        _LocatorsEntry &entry =
                reverseDependenciesEntry.locatorsEntryMap[entryName];
        entry.dependedOnDataSourceLocator = dependedOnDataSourceLocator;
        entry.affectedDataSourceLocator = affectedSourceLocator;

        reverseDependenciesEntry.flaggedForDeletion = false;
    }

}

// ---------------------------------------------------------------------------

void
HdDependencyForwardingSceneIndex::RemoveDeletedEntries(
    SdfPathVector *removedAffectedPrimPaths,
    SdfPathVector *removedDependedOnPrimPaths)
{
    SdfPathVector entriesToRemove;

    for (const SdfPath &dependedOnPrimPath :
            _potentiallyDeletedDependedOnPaths) {

        _DependedOnPrimsAffectedPrimsMap::iterator dependedOnPrimIt = 
            _dependedOnPrimToDependentsMap.find(dependedOnPrimPath);

        if (dependedOnPrimIt == _dependedOnPrimToDependentsMap.end()) {
            continue;
        }

        _AffectedPrimsDependencyMap &_affectedPrimsMap =
            (*dependedOnPrimIt).second;

        entriesToRemove.clear();

        std::function<void(_AffectedPrimsDependencyMap::value_type&)>
            processAffectedPrim =
            [&](_AffectedPrimsDependencyMap::value_type& affectedPrimPair) {
            const SdfPath &affectedPrimPath = affectedPrimPair.first;
            _AffectedPrimDependencyEntry &affectedPrimDependencyEntry =
                    affectedPrimPair.second;

            if (!affectedPrimDependencyEntry.flaggedForDeletion) {
                return;
            }

            entriesToRemove.push_back(affectedPrimPath);

            // now remove dependedOn prim from affected prim entry
            // if that removal leaves it empty, then remove the whole thing

            _AffectedPrimToDependsOnPathsEntryMap::iterator affectedPrimIt = 
                _affectedPrimToDependsOnPathsMap.find(affectedPrimPath);

            if (affectedPrimIt == _affectedPrimToDependsOnPathsMap.end()) {
                return;
            }

            _AffectedPrimToDependsOnPathsEntry &affectedPrimEntry =
                    (*affectedPrimIt).second;

            if (affectedPrimEntry.dependsOnPaths.find(dependedOnPrimPath)
                    == affectedPrimEntry.dependsOnPaths.end()) {
                return;
            }

            if (affectedPrimEntry.dependsOnPaths.size() == 1) {
                // If I'm the only thing in there, remove the whole entry
                _affectedPrimToDependsOnPathsMap.unsafe_erase(
                        affectedPrimPath);

                if (removedAffectedPrimPaths) {
                    removedAffectedPrimPaths->push_back(affectedPrimPath);
                }

            
            } else {
                affectedPrimEntry.dependsOnPaths.unsafe_erase(
                        dependedOnPrimPath);
            }
        };

        // Note: some prims have pretty wild fan-out, so we iterate over
        // whichever set of potentially affected prims is smaller...
        if (_affectedPrimsMap.size() < _potentiallyDeletedAffectedPaths.size()) {
            for (auto &affectedPrimPair : _affectedPrimsMap) {
                processAffectedPrim(affectedPrimPair);
            }
        } else {
            for (auto &path : _potentiallyDeletedAffectedPaths) {
                auto it = _affectedPrimsMap.find(path);
                if (it != _affectedPrimsMap.end()) {
                    processAffectedPrim(*it);
                }
            }
        }

        if (entriesToRemove.size() == _affectedPrimsMap.size()) {
            // removing everything?, just erase the dependedOn prim entry
            _dependedOnPrimToDependentsMap.unsafe_erase(dependedOnPrimPath);

            if (removedDependedOnPrimPaths) {
                removedDependedOnPrimPaths->push_back(dependedOnPrimPath);
            }

        } else {
            for (const SdfPath &affectedPrimPath : entriesToRemove) {
                _affectedPrimsMap.unsafe_erase(affectedPrimPath);
            }
        }
    }


    for (const SdfPath &affectedPrimPath :
            _potentiallyDeletedAffectedPaths) {

        _AffectedPrimToDependsOnPathsEntryMap::iterator it =
            _affectedPrimToDependsOnPathsMap.find(affectedPrimPath);

        if (it == _affectedPrimToDependsOnPathsMap.end()) {
            continue;
        }

        if ((*it).second.flaggedForDeletion) {
            if (removedAffectedPrimPaths) {
                removedAffectedPrimPaths->push_back(affectedPrimPath);
            }
            _affectedPrimToDependsOnPathsMap.unsafe_erase(it);
        }
    }

    _potentiallyDeletedDependedOnPaths.clear();
    _potentiallyDeletedAffectedPaths.clear();
}

PXR_NAMESPACE_CLOSE_SCOPE
