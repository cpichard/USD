//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HDSI_MATERIAL_OVERRIDE_RESOLVING_SCENE_INDEX_H
#define PXR_IMAGING_HDSI_MATERIAL_OVERRIDE_RESOLVING_SCENE_INDEX_H

#include "pxr/base/tf/declarePtrs.h"
#include "pxr/imaging/hd/filteringSceneIndex.h"
#include "pxr/imaging/hdsi/api.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_WEAK_AND_REF_PTRS(HdsiMaterialOverrideResolvingSceneIndex);

/*
HdsiMaterialOverrideResolvingSceneIndex applies material overrides in the form
of edits to a material's interface or directly to parameters of its shader nodes.

Overrides to a material's interface are only applied if their interface mappings 
are found.
The overrides for a particular 'publicUIName' are specified at path like this:

materialOverride.interfaceValues.<publicUIName>.value 
    -> overrideValueDataSource

The 'overrideValueDataSource' is copied over a network node parameter's original
'valueDataSource' at paths like this:

material.<renderContext>.nodes.<nodePath>.parameters.<inputName>.value
    -> valueDataSource

The scene index identifies which network node parameter to override by using
the material's interface mappings which are defined at paths like this:

material.<renderContext>.interface.parameters.<publicUIName>.mappings 
    -> [(nodePath, inputName), (nodePath, inputName), ...]

Edits of input parameters of shaders within the material are specified at a path 
like this:
materialOverride.parameterValues.<shaderNodeName>.<parameterName>.value ->
    paramEditValueDataSource

The 'paramEditValueDataSource' is copied over a network node parameter's original
'valueDataSource' at paths like this:

material.<renderContext>.nodes.<nodePath>.parameters.<inputName>.value
    -> valueDataSource

If the same input parameter is overridden both by an edit to the material
interface and by a direct parameter edit, the interface override will take 
precedence.

Below is a diagram of the expected attributes needed for material interface
and parameter edits on a scene index prim of type 'material':

MaterialPrim
|
+------materialOverride
|      |
|      +----interfaceValues
|      |    |
|      |    +-publicUIName
|      |    |   |
|      |    |   +---value -> overrideValueDataSource
|      |    |
|      |    +-publicUIName
|      |    |   |
|      |    |   +---value -> overrideValueDataSource
|      |    |
|      |    +-...
|      |
|      +----parameterValues  
|           |
|           +-nodePath
|           |   |
|           |   +---inputName
|           |   |   |
|           |   |   +---value -> paramEditValueDataSource
|           |   |
|           |   +---inputName
|           |   |   |
|           |   |   +---value -> paramEditValueDataSource
|           |   |
|           |   +---...
|           |  
|           +-...
|
+------material
       |
       +----ri
            |
            +--nodes
            |    |
            |    +-nodePath
            |    |     |
            |    |     +-parameters
            |    |           |
            |    |           +-inputName
            |    |           |   |
            |    |           |   +-value -> valueDataSource
            |    |           |
            |    |           +-inputName
            |    |               |
            |    |               +-value -> valueDataSource
            |    |
            |    +-nodePath  
            |          |
            |          +-parameters           
            |                |
            |                +-...       
            |
            +--interface
                 |
                 +-parameters
                 |     |
                 |     +-publicUIName
                 |     |     |
                 |     |     +-mappings
                 |     |         +-i0
                 |     |         |  |
                 |     |         |  +----nodePath  
                 |     |         |  |
                 |     |         |  +----inputName  
                 |     |         |
                 |     |         +-i1
                 |     |         |  |
                 |     |         |  +----nodePath  
                 |     |         |  |
                 |     |         |  +----inputName  
                 |     |         |
                 |     |         +-...
                 |     |
                 |     +-publicUIName 
                 |     |     |
                 |     |     +-mappings
                 |     |         +-i0
                 |     |         |  |
                 |     |         |  +----nodePath  
                 |     |         |  |
                 |     |         |  +----inputName  
                 |     |         |
                 |     |         +-i1
                 |     |         |  |
                 |     |         |  +----nodePath  
                 |     |         |  |
                 |     |         |  +----inputName  
                 |     |         |
                 |     |         +-...
                 |     +-...
                 |
                 +-parameterOrder = ...
*/

class HdsiMaterialOverrideResolvingSceneIndex final : 
    public HdSingleInputFilteringSceneIndexBase
{
public:
    static HdsiMaterialOverrideResolvingSceneIndexRefPtr New(
        const HdSceneIndexBaseRefPtr &inputScene)
    {
        return TfCreateRefPtr(
            new HdsiMaterialOverrideResolvingSceneIndex(inputScene));
    }

    // HdSceneIndexBase overrides
    HDSI_API 
    HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override;

    HDSI_API
    SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override;

protected:

    HDSI_API
    HdsiMaterialOverrideResolvingSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene);

    // HdSingleInputFilteringSceneIndexBase overrides
    void _PrimsAdded(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::AddedPrimEntries &entries) override;

    void _PrimsRemoved(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::RemovedPrimEntries &entries) override;

    void _PrimsDirtied(
        const HdSceneIndexBase &sender,
        const HdSceneIndexObserver::DirtiedPrimEntries &entries) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HDSI_MATERIAL_OVERRIDE_RESOLVING_SCENE_INDEX_H