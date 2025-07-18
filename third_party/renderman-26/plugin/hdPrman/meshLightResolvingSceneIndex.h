//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
#ifndef EXT_RMANPKG_PLUGIN_RENDERMAN_PLUGIN_HD_PRMAN_MESHLIGHT_RESOLVING_SCENE_INDEX_H
#define EXT_RMANPKG_PLUGIN_RENDERMAN_PLUGIN_HD_PRMAN_MESHLIGHT_RESOLVING_SCENE_INDEX_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"


PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(HdPrmanMeshLightResolvingSceneIndex);

// Pixar-only, Prman-specific Hydra scene index to resolve mesh lights.
class HdPrmanMeshLightResolvingSceneIndex : 
    public  HdSingleInputFilteringSceneIndexBase
{
public:
    static HdPrmanMeshLightResolvingSceneIndexRefPtr
    New(const HdSceneIndexBaseRefPtr& inputSceneIndex);

    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:
    HdPrmanMeshLightResolvingSceneIndex(
        const HdSceneIndexBaseRefPtr& inputSceneIndex);
    ~HdPrmanMeshLightResolvingSceneIndex();

    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;

private:
    void _AddMeshLight(
        const SdfPath& primPath,
        const HdSceneIndexPrim& prim,
        HdSceneIndexObserver::AddedPrimEntries* added);

    void _RemoveMeshLight(
        const SdfPath& primPath,
        HdSceneIndexObserver::RemovedPrimEntries* removed);
  
private:
    std::unordered_map<SdfPath, bool, SdfPath::Hash> _meshLights;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //EXT_RMANPKG_PLUGIN_RENDERMAN_PLUGIN_HD_PRMAN_MESHLIGHT_RESOLVING_SCENE_INDEX_H
