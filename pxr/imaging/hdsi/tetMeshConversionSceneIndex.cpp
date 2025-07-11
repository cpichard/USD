//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#include "pxr/imaging/hdsi/tetMeshConversionSceneIndex.h"

#include "pxr/imaging/hd/meshSchema.h"
#include "pxr/imaging/hd/meshTopologySchema.h"
#include "pxr/imaging/hd/tetMeshSchema.h"
#include "pxr/imaging/hd/tetMeshTopologySchema.h"

#include "pxr/imaging/hd/overlayContainerDataSource.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/pxOsd/tokens.h"


PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Compute the MeshTopology Data Source translating the TetVertexIndices into 
// FaceVertexIndices and FaceVertexCounts
HdContainerDataSourceHandle
_ComputeMeshTopologyDataSource(
    HdContainerDataSourceHandle const &primDataSource)
{
    const HdTetMeshTopologySchema meshTopoSchema = 
        HdTetMeshSchema::GetFromParent(primDataSource).GetTopology();
    // Get the TetVertexIndices
    const HdVec3iArrayDataSourceHandle surfaceFaceIndicesDS =
        meshTopoSchema.GetSurfaceFaceVertexIndices();

    if (!surfaceFaceIndicesDS) {
        return HdMeshTopologySchema::Builder()
            .SetOrientation(meshTopoSchema.GetOrientation())
            .Build();
    }

    // Compute the FaceVertexIndices and FaceVertexIndicesCounts
    const VtVec3iArray surfaceFaceIndices = 
            surfaceFaceIndicesDS->GetTypedValue(0.0f);

    const size_t n = surfaceFaceIndices.size();
    const VtIntArray faceVertexCounts(n, 3);

    VtIntArray faceVertexIndices;
    faceVertexIndices.reserve(n*3);
    for (const GfVec3i &faceIndices : surfaceFaceIndices) {
        faceVertexIndices.push_back(faceIndices[0]);
        faceVertexIndices.push_back(faceIndices[1]);
        faceVertexIndices.push_back(faceIndices[2]);
    }

    return
        HdMeshTopologySchema::Builder()
            .SetFaceVertexCounts(
                HdRetainedTypedSampledDataSource<VtIntArray>::New(
                    faceVertexCounts))
            .SetFaceVertexIndices(
                HdRetainedTypedSampledDataSource<VtIntArray>::New(
                    faceVertexIndices))
            .SetOrientation(meshTopoSchema.GetOrientation())
            .Build();
}

// Data source for locator mesh
class _MeshDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_MeshDataSource);

    TfTokenVector GetNames() override {
        static const TfTokenVector result = {
            HdMeshSchemaTokens->topology,
            HdMeshSchemaTokens->subdivisionScheme,
            HdMeshSchemaTokens->doubleSided
        };
        return result;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override {
        if (name == HdMeshSchemaTokens->topology) {
            return _ComputeMeshTopologyDataSource(_primDataSource);
        }
        if (name == HdMeshSchemaTokens->subdivisionScheme) {
            static HdDataSourceBaseHandle const result =
                HdRetainedTypedSampledDataSource<TfToken>::New(
                    PxOsdOpenSubdivTokens->catmullClark);
            return result;
        }
        if (name == HdMeshSchemaTokens->doubleSided) {
            return
                HdTetMeshSchema::GetFromParent(_primDataSource)
                    .GetDoubleSided();
        }
        return nullptr;
    }

private:
    _MeshDataSource(HdContainerDataSourceHandle const &primDataSource)
     : _primDataSource(primDataSource)
    {
    }

    HdContainerDataSourceHandle const _primDataSource;
};

HdContainerDataSourceHandle
_ComputePrimDataSource(
    const SdfPath &primPath,
    const HdContainerDataSourceHandle &primDataSource)
{
    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdMeshSchemaTokens->mesh,
            _MeshDataSource::New(primDataSource)),
        primDataSource);
}

}

HdsiTetMeshConversionSceneIndexRefPtr
HdsiTetMeshConversionSceneIndex::New(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(
        new HdsiTetMeshConversionSceneIndex(inputSceneIndex));
}

HdsiTetMeshConversionSceneIndex::HdsiTetMeshConversionSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
  : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
}

HdSceneIndexPrim
HdsiTetMeshConversionSceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (prim.primType == HdPrimTypeTokens->tetMesh) {
        return {
            HdPrimTypeTokens->mesh,
            _ComputePrimDataSource(primPath, prim.dataSource) };
    }
    return prim;
}

SdfPathVector
HdsiTetMeshConversionSceneIndex::GetChildPrimPaths(
    const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}


void
HdsiTetMeshConversionSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    // Gather prims added indices for Tet Mesh prims
    std::vector<size_t> tetMeshIndices;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].primType == HdPrimTypeTokens->tetMesh) {
            tetMeshIndices.push_back(i);
        }
    }
    
    if (tetMeshIndices.empty()) {
        _SendPrimsAdded(entries);
        return;
    }

    // Change the prim types for Tet Meshes to Mesh
    HdSceneIndexObserver::AddedPrimEntries entriesToAdd(entries);
    for (const size_t i : tetMeshIndices) {
        entriesToAdd[i].primType = HdPrimTypeTokens->mesh;
    }

    _SendPrimsAdded(entriesToAdd);
}

void
HdsiTetMeshConversionSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    _SendPrimsRemoved(entries);
}

void
HdsiTetMeshConversionSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    if (!_IsObserved()) {
        return;
    }

    // Gather indices for Tet Mesh locators that need to be updated.
    std::vector<size_t> tetMeshIndices;
    for (size_t i = 0; i < entries.size(); ++i) {
        const HdDataSourceLocatorSet& dirtyLocators = entries[i].dirtyLocators;
        if (dirtyLocators.Contains(HdDataSourceLocator::EmptyLocator())) {
            // If the locator set contains the empty locator, it means it will
            // intersect everything.  It does not need to be updated.
            continue;
        }
        if (dirtyLocators.Intersects(HdTetMeshSchema::GetDefaultLocator())) {
            tetMeshIndices.push_back(i);
        }
    }
    
    if (tetMeshIndices.empty()) {
        _SendPrimsDirtied(entries);
        return;
    }

    // Convert TetMesh Locators to Mesh Locators
    HdSceneIndexObserver::DirtiedPrimEntries newEntries(entries);
    for (const size_t i : tetMeshIndices) {
        HdDataSourceLocatorSet& dirtyLocators = newEntries[i].dirtyLocators;
        // The code that populated tetMeshIndices skips anything that
        // contains the empty locator.
        TF_VERIFY(!dirtyLocators.Contains(HdDataSourceLocator::EmptyLocator()));

        if (dirtyLocators.Intersects(HdTetMeshSchema::GetDoubleSidedLocator())) {
            dirtyLocators.append(HdMeshSchema::GetDoubleSidedLocator());
        }
        if (dirtyLocators.Intersects(HdTetMeshSchema::GetTopologyLocator())) {
            dirtyLocators.append(HdMeshSchema::GetTopologyLocator());
        }
    }

    _SendPrimsDirtied(newEntries);
}


PXR_NAMESPACE_CLOSE_SCOPE
