//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hd/unitTestDelegate.h"

#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/imaging/hd/sprim.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/meshTopology.h"
#include "pxr/imaging/hd/points.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/gf/matrix3d.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec2h.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4h.h"
#include "pxr/base/gf/vec4i.h"
#include "pxr/base/gf/rotation.h"

PXR_NAMESPACE_OPEN_SCOPE


template <typename T>
static VtArray<T>
_BuildArray(T values[], int numValues)
{
    VtArray<T> result(numValues);
    std::copy(values, values+numValues, result.begin());
    return result;
}

HdUnitTestDelegate::HdUnitTestDelegate(HdRenderIndex *parentIndex,
                                         SdfPath const& delegateID)
  : HdSceneDelegate(parentIndex, delegateID)
  , _hasInstancePrimvars(true), _refineLevel(0), _visibility(true)
{
}

void
HdUnitTestDelegate::SetRefineLevel(int level)
{
    _refineLevel = level;
    TF_FOR_ALL (it, _meshes) {
        GetRenderIndex().GetChangeTracker().MarkRprimDirty(
            it->first, HdChangeTracker::DirtyDisplayStyle);
    }
    TF_FOR_ALL (it, _curves) {
        GetRenderIndex().GetChangeTracker().MarkRprimDirty(
            it->first, HdChangeTracker::DirtyDisplayStyle);
    }
    TF_FOR_ALL (it, _refineLevels) {
        it->second = level;
    }
}

void
HdUnitTestDelegate::SetVisibility(bool vis)
{
    _visibility = vis;
    TF_FOR_ALL(it, _meshes) {
        GetRenderIndex().GetChangeTracker().MarkRprimDirty(
            it->first, HdChangeTracker::DirtyVisibility);
    }
    TF_FOR_ALL(it, _meshes) {
        GetRenderIndex().GetChangeTracker().MarkRprimDirty(
            it->first, HdChangeTracker::DirtyVisibility);
    }
    TF_FOR_ALL(it, _visibilities) {
        it->second = vis;
    }
}

void
HdUnitTestDelegate::AddMesh(SdfPath const &id)
{
    GfMatrix4f transform(1);
    VtVec3fArray points;
    VtIntArray numVerts;
    VtIntArray verts;
    bool guide = false;
    SdfPath instancerId;
    TfToken scheme = PxOsdOpenSubdivTokens->catmullClark;

    AddMesh(id, transform, points, numVerts, verts, guide, instancerId, scheme);
}

void
HdUnitTestDelegate::AddMesh(SdfPath const &id,
                             GfMatrix4f const &transform,
                             VtVec3fArray const &points,
                             VtIntArray const &numVerts,
                             VtIntArray const &verts,
                             bool guide,
                             SdfPath const &instancerId,
                             TfToken const &scheme,
                             TfToken const &orientation,
                             bool doubleSided)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    index.InsertRprim(HdPrimTypeTokens->mesh, this, id);

    _meshes[id] = _Mesh(scheme, orientation, transform,
                        points, numVerts, verts, /*holes*/VtIntArray(),
                        PxOsdSubdivTags(), guide, doubleSided);
    
    // Add color and opacity as a constant primvar
    _primvars[id] = { _Primvar(HdTokens->displayColor,
                               VtValue(GfVec3f(1)),
                               HdInterpolationConstant,
                               HdPrimvarRoleTokens->color),
                      _Primvar(HdTokens->displayOpacity,
                               VtValue(1.0f),
                               HdInterpolationConstant,
                               HdPrimvarRoleTokens->color) };

    if (!instancerId.IsEmpty()) {
        _instancerBindings[id] = instancerId;
        _instancers[instancerId].prototypes.push_back(id);
    }
}

void
HdUnitTestDelegate::AddMesh(SdfPath const &id,
                             GfMatrix4f const &transform,
                             VtVec3fArray const &points,
                             VtIntArray const &numVerts,
                             VtIntArray const &verts,
                             VtIntArray const &holes,
                             PxOsdSubdivTags const &subdivTags,
                             VtValue const &color,
                             HdInterpolation colorInterpolation,
                             VtValue const &opacity,
                             HdInterpolation opacityInterpolation,
                             bool guide,
                             SdfPath const &instancerId,
                             TfToken const &scheme,
                             TfToken const &orientation,
                             bool doubleSided)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    index.InsertRprim(HdPrimTypeTokens->mesh, this, id);

    _meshes[id] = _Mesh(scheme, orientation, transform,
                        points, numVerts, verts, holes, subdivTags,
                        guide, doubleSided);

    _primvars[id] = { _Primvar(HdTokens->displayColor,
                               color,
                               colorInterpolation,
                               HdPrimvarRoleTokens->color),
                      _Primvar(HdTokens->displayOpacity,
                               opacity,
                               opacityInterpolation,
                               HdPrimvarRoleTokens->color) };

    if (!instancerId.IsEmpty()) {
        _instancerBindings[id] = instancerId;
        _instancers[instancerId].prototypes.push_back(id);
    }
}

void
HdUnitTestDelegate::AddMesh(SdfPath const &id,
                             GfMatrix4f const &transform,
                             VtVec3fArray const &points,
                             VtIntArray const &numVerts,
                             VtIntArray const &verts,
                             VtIntArray const &holes,
                             PxOsdSubdivTags const &subdivTags,
                             VtValue const &color,
                             VtIntArray const &colorIndices,
                             HdInterpolation colorInterpolation,
                             VtValue const &opacity,
                             VtIntArray const &opacityIndices,
                             HdInterpolation opacityInterpolation,
                             bool guide,
                             SdfPath const &instancerId,
                             TfToken const &scheme,
                             TfToken const &orientation,
                             bool doubleSided)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    index.InsertRprim(HdPrimTypeTokens->mesh, this, id);

    _meshes[id] = _Mesh(scheme, orientation, transform,
                        points, numVerts, verts, holes, subdivTags,
                        guide, doubleSided);

    _primvars[id] = { _Primvar(HdTokens->displayColor,
                               color,
                               colorInterpolation,
                               HdPrimvarRoleTokens->color,
                               colorIndices),
                      _Primvar(HdTokens->displayOpacity,
                               opacity,
                               opacityInterpolation,
                               HdPrimvarRoleTokens->color,
                               opacityIndices) };

    if (!instancerId.IsEmpty()) {
        _instancerBindings[id] = instancerId;
        _instancers[instancerId].prototypes.push_back(id);
    }
}

void
HdUnitTestDelegate::SetMeshCullStyle(
    SdfPath const &id, HdCullStyle const &cullStyle)
{
    auto it = _meshes.find(id);
    if (it != _meshes.end()) {
        if (it->second.cullStyle != cullStyle) {
            it->second.cullStyle = cullStyle;
            HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
            tracker.MarkRprimDirty(id, HdChangeTracker::DirtyCullStyle);
        }
    } else {
        TF_WARN("Could not find mesh Rprim named %s. \n", id.GetText());
    }
}

void
HdUnitTestDelegate::AddBasisCurves(SdfPath const &id,
                                    VtVec3fArray const &points,
                                    VtIntArray const &curveVertexCounts,
                                    VtIntArray const &curveIndices,
                                    VtVec3fArray const &normals,
                                    TfToken const &type,
                                    TfToken const &basis,
                                    VtValue const &color,
                                    HdInterpolation colorInterpolation,
                                    VtValue const &opacity,
                                    HdInterpolation opacityInterpolation,
                                    VtValue const &width,
                                    HdInterpolation widthInterpolation,
                                    SdfPath const &instancerId)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    index.InsertRprim(HdPrimTypeTokens->basisCurves, this, id);

    _curves[id] = _Curves(points, curveVertexCounts, curveIndices,
                          type,
                          basis);

    _primvars[id] = {
        _Primvar(HdTokens->displayColor,
                 color,
                 colorInterpolation,
                 HdPrimvarRoleTokens->color),
        _Primvar(HdTokens->displayOpacity,
                 opacity,
                 opacityInterpolation,
                 HdPrimvarRoleTokens->color),
        _Primvar(HdTokens->widths,
                 width,
                 widthInterpolation,
                 HdPrimvarRoleTokens->none)};

    if (!normals.empty()) {
        _primvars[id].emplace_back(
            _Primvar(HdTokens->normals,
                     VtValue(normals),
                     HdInterpolationVertex,
                     HdPrimvarRoleTokens->normal) );
    }

    if (!instancerId.IsEmpty()) {
        _instancerBindings[id] = instancerId;
        _instancers[instancerId].prototypes.push_back(id);
    }
}

void
HdUnitTestDelegate::AddPoints(SdfPath const &id,
                               VtVec3fArray const &points,
                               VtValue const &color,
                               HdInterpolation colorInterpolation,
                               VtValue const &opacity,
                               HdInterpolation opacityInterpolation,
                               VtValue const &width,
                               HdInterpolation widthInterpolation,
                               SdfPath const &instancerId)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    index.InsertRprim(HdPrimTypeTokens->points, this, id);

    _points[id] = _Points(points);

    _primvars[id] = {
        _Primvar(HdTokens->displayColor,
                 color,
                 colorInterpolation,
                 HdPrimvarRoleTokens->color),
        _Primvar(HdTokens->displayOpacity,
                 opacity,
                 opacityInterpolation,
                 HdPrimvarRoleTokens->color),
        _Primvar(HdTokens->widths,
                 width,
                 widthInterpolation,
                 HdPrimvarRoleTokens->none)};

    if (!instancerId.IsEmpty()) {
        _instancerBindings[id] = instancerId;
        _instancers[instancerId].prototypes.push_back(id);
    }
}

void
HdUnitTestDelegate::AddInstancer(SdfPath const &id,
                                  SdfPath const &parentId,
                                  GfMatrix4f const &rootTransform)
{
    HD_TRACE_FUNCTION();

    HdRenderIndex& index = GetRenderIndex();
    // add instancer
    index.InsertInstancer(this, id);
    _instancers[id] = _Instancer();
    _instancers[id].rootTransform = rootTransform;

    if (!parentId.IsEmpty()) {
        _instancerBindings[id] = parentId;
        _instancers[parentId].prototypes.push_back(id);
    }

    // Instancers don't have initial dirty bits, so we need to add them.
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkInstancerDirty(id, HdChangeTracker::DirtyTransform |
                                   HdChangeTracker::DirtyPrimvar |
                                   HdChangeTracker::DirtyInstanceIndex);
}

void
HdUnitTestDelegate::SetInstancerProperties(SdfPath const &id,
                                            VtIntArray const &prototypeIndex,
                                            VtVec3fArray const &scale,
                                            VtVec4fArray const &rotate,
                                            VtVec3fArray const &translate)
{
    HD_TRACE_FUNCTION();

    if (!TF_VERIFY(prototypeIndex.size() == scale.size())   || 
        !TF_VERIFY(prototypeIndex.size() == rotate.size())  ||
        !TF_VERIFY(prototypeIndex.size() == translate.size())) {
        return;
    }

    _instancers[id].scale = scale;
    _instancers[id].rotate = rotate;
    _instancers[id].translate = translate;
    _instancers[id].prototypeIndices = prototypeIndex;

    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkInstancerDirty(id, HdChangeTracker::DirtyPrimvar |
                                   HdChangeTracker::DirtyInstanceIndex);
}

void
HdUnitTestDelegate::UpdateInstancer(SdfPath const& rprimId, 
                                    SdfPath const& instancerId)
{
    if(_meshes.find(rprimId) != _meshes.end()) {
        if (!instancerId.IsEmpty()) {
            _instancerBindings[rprimId] = instancerId;
            _instancers[instancerId].prototypes.push_back(rprimId);

            HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
            tracker.MarkRprimDirty(rprimId, HdChangeTracker::DirtyInstancer);
        }
    }
}

void
HdUnitTestDelegate::AddPrimvar(SdfPath const& id,
                               TfToken const& name,
                               VtValue const& value,
                               HdInterpolation const& interp,
                               TfToken const& role,
                               VtIntArray const& indices)
{
    _Primvars::iterator pvIt;
    if (_FindPrimvar(id, name, &pvIt)) {
        TF_WARN("Rprim %s already has a primvar named %s. Skipping.\n",
            id.GetText(), name.GetText());
        return;
    }

    _primvars[id].emplace_back(name, value, interp, role, indices);
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    // XXX: Using DirtyPrimvar even though this is a descriptor change.
    tracker.MarkRprimDirty(id, HdChangeTracker::DirtyPrimvar);
}

void
HdUnitTestDelegate::UpdatePrimvarValue(SdfPath const& id,
                                       TfToken const& name,
                                       VtValue const& value,
                                       VtIntArray const& indices)
{
    _Primvars::iterator pvIt;
    if (_FindPrimvar(id, name, &pvIt)) {
        pvIt->value = value;
        pvIt->indices = indices;

        HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
        tracker.MarkRprimDirty(id, HdChangeTracker::DirtyPrimvar);
    } else {
        TF_WARN("Rprim %s has no primvar named %s.\n",
            id.GetText(), name.GetText());
    }
}

void
HdUnitTestDelegate::RemovePrimvar(SdfPath const& id, TfToken const& name)
{
    _Primvars::iterator pvIt;
    if (_FindPrimvar(id, name, &pvIt)) {
        _primvars[id].erase(pvIt);

        HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
        // XXX: Using DirtyPrimvar even though this is a descriptor change.
        tracker.MarkRprimDirty(id, HdChangeTracker::DirtyPrimvar);
    } else {
        TF_WARN("Rprim %s has no primvar named %s.\n",
            id.GetText(), name.GetText());
    }
}

void
HdUnitTestDelegate::UpdateTransform(SdfPath const& id,
                                    GfMatrix4f const& mat)
{
    if(_meshes.find(id) != _meshes.end()) {
        _meshes[id].transform = mat;
        HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
        tracker.MarkRprimDirty(id, HdChangeTracker::DirtyTransform);
    }
    if (_cameras.find(id) != _cameras.end()) {
        _cameras[id].transform = mat;
        HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
        tracker.MarkSprimDirty(id, HdChangeTracker::DirtyTransform);
    }        
}

void 
HdUnitTestDelegate::AddMaterialResource(SdfPath const &id,
                                         VtValue materialResource)
{
    HdRenderIndex& index = GetRenderIndex();
    index.InsertSprim(HdPrimTypeTokens->material, this, id);
    _materials[id] = materialResource;
}

void 
HdUnitTestDelegate::UpdateMaterialResource(SdfPath const &materialId, 
                                            VtValue materialResource)
{
    _materials[materialId] = materialResource;

    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkSprimDirty(materialId, HdMaterial::DirtyResource);

    /// XXX : Make sure all rprims know they have an invalid binding,
    //        some backends need to be notified when a material has
    //        been updated. This is a temp solution.
    for (auto const &p : _materialBindings) {
      if (p.second == materialId) {
        tracker.MarkRprimDirty(p.first, HdChangeTracker::DirtyMaterialId);
      }
    }
}

void 
HdUnitTestDelegate::BindMaterial(SdfPath const &rprimId, 
                                  SdfPath const &materialId)
{
    _materialBindings[rprimId] = materialId;
}

void 
HdUnitTestDelegate::RebindMaterial(SdfPath const &rprimId, 
                                    SdfPath const &materialId)
{
    BindMaterial(rprimId, materialId);

    // Mark the rprim material binding as dirty so sync gets
    // called on that rprim and also increase 
    // the version of the global bindings so batches get rebuild (if needed)
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkRprimDirty(rprimId, HdChangeTracker::DirtyMaterialId);
}

void
HdUnitTestDelegate::HideRprim(SdfPath const& id) 
{
    _hiddenRprims.insert(id);
    MarkRprimDirty(id, HdChangeTracker::DirtyRenderTag);
}

void
HdUnitTestDelegate::UnhideRprim(SdfPath const& id) 
{
    _hiddenRprims.erase(id);
    MarkRprimDirty(id, HdChangeTracker::DirtyRenderTag);
}

void
HdUnitTestDelegate::SetReprSelector(SdfPath const &id,
                    HdReprSelector const &reprSelector)
{
    // XXX: Add repr support for curves and points
    if (_meshes.find(id) != _meshes.end()) {
        _meshes[id].reprSelector = reprSelector;
        HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
        tracker.MarkRprimDirty(id, HdChangeTracker::DirtyRepr);
    }
}

void
HdUnitTestDelegate::SetRefineLevel(SdfPath const &id, int refineLevel)
{
    _refineLevels[id] = refineLevel;
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkRprimDirty(id, HdChangeTracker::DirtyDisplayStyle);
}

void
HdUnitTestDelegate::SetVisibility(SdfPath const &id, bool vis)
{
    _visibilities[id] = vis;
    GetRenderIndex().GetChangeTracker().MarkRprimDirty(id,
        HdChangeTracker::DirtyVisibility);
}

static VtVec3fArray _AnimatePositions(VtVec3fArray const &positions, float time)
{
    VtVec3fArray result = positions;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] += GfVec3f((float)(0.5*sin(0.5*i + time)), 0, 0);
    }
    return result;
}

void
HdUnitTestDelegate::UpdatePositions(SdfPath const &id, float time)
{
   if (_meshes.find(id) != _meshes.end()) {
       _meshes[id].points = _AnimatePositions(_meshes[id].points, time);
   }
   else if(_curves.find(id) != _curves.end()) {
       _curves[id].points = _AnimatePositions(_curves[id].points, time);
   }
   else if(_points.find(id) != _points.end()) {
       _points[id].points = _AnimatePositions(_points[id].points, time);
   } else {
       return;
   }
   HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
   tracker.MarkRprimDirty(id, HdChangeTracker::DirtyPoints);
}

void
HdUnitTestDelegate::UpdateRprims(float time)
{
    // update prims
    float delta = 0.01f;
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    TF_FOR_ALL (it, _meshes) {
        SdfPath const& id = it->first;
        tracker.MarkRprimDirty(id, HdChangeTracker::DirtyPrimvar);
        
        // Update constant interp color on each invocation
        _Primvars::iterator pvIt;
        if (_FindPrimvar(id, HdTokens->displayColor, &pvIt)) {
            if (pvIt->interp == HdInterpolationConstant) {
                GfVec4f color = pvIt->value.Get<GfVec4f>();
                color[0] = fmod(color[0] + delta, 1.0f);
                color[1] = fmod(color[1] + delta*2, 1.0f);    
                pvIt->value = VtValue(color);
            }
        }
    }
}

void
HdUnitTestDelegate::UpdateInstancerPrimvars(float time)
{
    // update instancers
    TF_FOR_ALL (it, _instancers) {
        for (size_t i = 0; i < it->second.rotate.size(); ++i) {
            GfQuaternion q = GfRotation(GfVec3d(1, 0, 0), i*time).GetQuaternion();
            GfVec4f quat(q.GetReal(),
                         q.GetImaginary()[0],
                         q.GetImaginary()[1],
                         q.GetImaginary()[2]);
            it->second.rotate[i] = quat;
        }

        GetRenderIndex().GetChangeTracker().MarkInstancerDirty(
            it->first, HdChangeTracker::DirtyPrimvar);
    }
}

void
HdUnitTestDelegate::UpdateInstancerPrototypes(float time)
{
    // update instancer prototypes
    TF_FOR_ALL (it, _instancers) {
        // rotate prototype indices
        int numInstances = it->second.prototypeIndices.size();
        if (numInstances > 0) {
            int firstPrototype = it->second.prototypeIndices[0];
            for (int i = 1; i < numInstances; ++i) {
                it->second.prototypeIndices[i-1] = it->second.prototypeIndices[i];
            }
            it->second.prototypeIndices[numInstances-1] = firstPrototype;
        }

        // invalidate instance index
        GetRenderIndex().GetChangeTracker().MarkInstancerDirty(
            it->first, HdChangeTracker::DirtyInstanceIndex);
    }
}

void
HdUnitTestDelegate::AddRenderBuffer(SdfPath const &id,
                                    HdRenderBufferDescriptor const &desc)
{
    HdRenderIndex& index = GetRenderIndex();
    index.InsertBprim(HdPrimTypeTokens->renderBuffer, this, id);
    _renderBuffers[id] = _RenderBuffer(
        desc.dimensions, desc.format, desc.multiSampled);
}

void
HdUnitTestDelegate::UpdateRenderBuffer(SdfPath const &id, 
                                       HdRenderBufferDescriptor const &desc)
{
    _renderBuffers[id] = _RenderBuffer(
        desc.dimensions, desc.format, desc.multiSampled);
    HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
    tracker.MarkBprimDirty(id, HdRenderBuffer::DirtyDescription);
}

void
HdUnitTestDelegate::AddCamera(SdfPath const &id)
{
    HdRenderIndex& index = GetRenderIndex();
    index.InsertSprim(HdPrimTypeTokens->camera, this, id);
    _cameras[id] = _Camera();
}

void
HdUnitTestDelegate::UpdateCamera(SdfPath const &id,
                                  TfToken const &key,
                                  VtValue value)
{
    _cameras[id].params[key] = value;
   HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
   tracker.MarkSprimDirty(id, HdChangeTracker::AllDirty);
}

void
HdUnitTestDelegate::UpdateTask(SdfPath const &id,
                                TfToken const &key,
                                VtValue value)
{
    _tasks[id].params[key] = value;

   // Update dirty bits for tokens we recognize.
   HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
   if (key == HdTokens->params) {
       tracker.MarkTaskDirty(id, HdChangeTracker::DirtyParams);
   } else if (key == HdTokens->collection) {
       tracker.MarkTaskDirty(id, HdChangeTracker::DirtyCollection);
   } else if (key == HdTokens->renderTags) {
       tracker.MarkTaskDirty(id, HdChangeTracker::DirtyRenderTags);
   } else {
       TF_CODING_ERROR("Unknown key %s", key.GetText());
   }
}

/*virtual*/
TfToken
HdUnitTestDelegate::GetRenderTag(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    if (_hiddenRprims.find(id) != _hiddenRprims.end()) {
        return HdRenderTagTokens->hidden;
    }

    if (_Mesh *mesh = TfMapLookupPtr(_meshes, id)) {
        if (mesh->guide) {
            return HdRenderTagTokens->guide;
        } else {
            return HdRenderTagTokens->geometry;
        }
    } else if (_curves.count(id) > 0) {
        return HdRenderTagTokens->geometry;
    } else if (_points.count(id) > 0) {
        return HdRenderTagTokens->geometry;
    }

    return HdRenderTagTokens->hidden;
}

/*virtual*/
TfTokenVector
HdUnitTestDelegate::GetTaskRenderTags(SdfPath const& id)
{
    const auto it = _tasks.find(id);
    if (it == _tasks.end()) {
        return TfTokenVector();
    }

    const VtDictionary &dict = it->second.params;
    return VtDictionaryGet<TfTokenVector>(dict, HdTokens->renderTags, VtDefault = TfTokenVector());
}

/*virtual*/
HdMeshTopology 
HdUnitTestDelegate::GetMeshTopology(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    HdMeshTopology topology;
    const _Mesh &mesh = _meshes[id];

    return HdMeshTopology(mesh.scheme,
                          mesh.orientation,
                          mesh.numVerts,
                          mesh.verts,
                          mesh.holes);
}

/*virtual*/
HdBasisCurvesTopology 
HdUnitTestDelegate::GetBasisCurvesTopology(SdfPath const& id)
{
    HD_TRACE_FUNCTION();
    const _Curves &curve = _curves[id];

    // Need to implement testing support for basis curves
    return HdBasisCurvesTopology(curve.type,
                                 curve.basis,
                                 curve.wrap,
                                 curve.curveVertexCounts,
                                 curve.curveIndices);
}

/*virtual*/
PxOsdSubdivTags
HdUnitTestDelegate::GetSubdivTags(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    const _Mesh &mesh = _meshes[id];

    return mesh.subdivTags;
}

/*virtual*/
GfRange3d
HdUnitTestDelegate::GetExtent(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    GfRange3d range;
    VtVec3fArray points;
    if(_meshes.find(id) != _meshes.end()) {
        points = _meshes[id].points; 
    }
    else if(_curves.find(id) != _curves.end()) {
        points = _curves[id].points; 
    }
    else if(_points.find(id) != _points.end()) {
        points = _points[id].points; 
    }
    TF_FOR_ALL(it, points) {
        range.UnionWith(*it);
    }

    return range;
}

/*virtual*/
bool
HdUnitTestDelegate::GetDoubleSided(SdfPath const& id)
{
    if (_meshes.find(id) != _meshes.end()) {
        return _meshes[id].doubleSided;
    }
    return false;
}

/*virtual*/
HdDisplayStyle 
HdUnitTestDelegate::GetDisplayStyle(SdfPath const& id)
{
    if (_refineLevels.find(id) != _refineLevels.end()) {
        return HdDisplayStyle(_refineLevels[id]);
    }
    // returns fallback refinelevel
    return HdDisplayStyle(_refineLevel);
}

/*virtual*/
HdCullStyle 
HdUnitTestDelegate::GetCullStyle(SdfPath const& id)
{
    if (_meshes.find(id) != _meshes.end()) {
        return _meshes[id].cullStyle;
    }
    return HdCullStyleDontCare;
}

/*virtual*/
VtIntArray
HdUnitTestDelegate::GetInstanceIndices(SdfPath const& instancerId,
                                        SdfPath const& prototypeId)
{
    HD_TRACE_FUNCTION();
    VtIntArray indices(0);
    //
    // XXX: this is very naive implementation for unit test.
    //
    //   transpose prototypeIndices/instances to instanceIndices/prototype
    if (_Instancer *instancer = TfMapLookupPtr(_instancers, instancerId)) {
        size_t prototypeIndex = 0;
        for (;prototypeIndex < instancer->prototypes.size(); ++prototypeIndex) {
            if (instancer->prototypes[prototypeIndex] == prototypeId) break;
        }
        if (prototypeIndex == instancer->prototypes.size()) return indices;

        // XXX use const_ptr
        for (size_t i = 0; i < instancer->prototypeIndices.size(); ++i) {
            if (instancer->prototypeIndices[i] == static_cast<int>(prototypeIndex)) {
                indices.push_back(i);
            }
        }
    }
    return indices;
}

/*virtual*/
SdfPathVector
HdUnitTestDelegate::GetInstancerPrototypes(SdfPath const& instancerId)
{
    HD_TRACE_FUNCTION();

    if (_Instancer *instancer = TfMapLookupPtr(_instancers, instancerId)) {
        return instancer->prototypes;
    }
    return SdfPathVector();
}

/*virtual*/
GfMatrix4d
HdUnitTestDelegate::GetInstancerTransform(SdfPath const& instancerId)
{
    HD_TRACE_FUNCTION();
    if (_Instancer *instancer = TfMapLookupPtr(_instancers, instancerId)) {
        return GfMatrix4d(instancer->rootTransform);
    }
    return GfMatrix4d(1);
}

/*virtual*/
SdfPath 
HdUnitTestDelegate::GetMaterialId(SdfPath const& rprimId)
{
    SdfPath materialId;
    TfMapLookup(_materialBindings, rprimId, &materialId);
    return materialId;
}

/*virtual*/
SdfPath
HdUnitTestDelegate::GetInstancerId(SdfPath const& primId)
{
    SdfPath instancerId;
    TfMapLookup(_instancerBindings, primId, &instancerId);
    return instancerId;
}

/*virtual*/
VtValue 
HdUnitTestDelegate::GetMaterialResource(SdfPath const &materialId)
{
    if (VtValue *material = TfMapLookupPtr(_materials, materialId)){
        return *material;
    }
    return VtValue();
}

/*virtual*/
VtValue
HdUnitTestDelegate::GetCameraParamValue(SdfPath const &cameraId, 
                                        TfToken const &paramName)
{
    if (_cameras.find(cameraId) != _cameras.end()) {
        return _cameras[cameraId].params[paramName];
    } 
    return VtValue();
}

/*virtual*/
HdRenderBufferDescriptor
HdUnitTestDelegate::GetRenderBufferDescriptor(SdfPath const& id)
{
    if (_RenderBuffer *rb = TfMapLookupPtr(_renderBuffers, id)) {
        return HdRenderBufferDescriptor(
                rb->dims, rb->format, rb->multiSampled);
    }
    return HdRenderBufferDescriptor();
}

/*virtual*/
GfMatrix4d
HdUnitTestDelegate::GetTransform(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    if(_meshes.find(id) != _meshes.end()) {
        return GfMatrix4d(_meshes[id].transform);
    }
    if (_cameras.find(id) != _cameras.end()) {
        return GfMatrix4d(_cameras[id].transform);
    }

    return GfMatrix4d(1);
}

/*virtual*/
bool
HdUnitTestDelegate::GetVisible(SdfPath const& id)
{
    HD_TRACE_FUNCTION();

    if (_visibilities.find(id) != _visibilities.end()) {
        return _visibilities[id];
    }
    // returns fallback refinelevel
    return _visibility;
}

namespace {

template<typename T>
VtValue 
_ComputeFlattened(VtValue const &value, VtIntArray const &indices) {
    VtArray<T> array = value.Get<VtArray<T>>();
    VtArray<T> result = VtArray<T>(indices.size());

    for (size_t i = 0; i < indices.size(); ++i) {
        int index = indices[i];
        if (index >= 0 && (size_t)index < array.size()) {
            result[i] = array[index];
        } else {
            TF_CODING_ERROR("Invalid indices");
        }
    }

    return VtValue(result);
}

VtValue 
_ComputeFlattenedValue(VtValue const &value, VtIntArray const &indices) {
    if (value.IsHolding<VtVec2fArray>()) {
        return _ComputeFlattened<GfVec2f>(value, indices);
    } else if (value.IsHolding<VtVec2dArray>()) {
        return _ComputeFlattened<GfVec2d>(value, indices);
    } else if (value.IsHolding<VtVec2iArray>()) {
        return _ComputeFlattened<GfVec2i>(value, indices);
    } else if (value.IsHolding<VtVec2hArray>()) {
        return _ComputeFlattened<GfVec2h>(value, indices);             
    } else if (value.IsHolding<VtVec3fArray>()) {
        return _ComputeFlattened<GfVec3f>(value, indices);   
    } else if (value.IsHolding<VtVec3dArray>()) {
        return _ComputeFlattened<GfVec3d>(value, indices); 
     } else if (value.IsHolding<VtVec3iArray>()) {
        return _ComputeFlattened<GfVec3i>(value, indices);
    } else if (value.IsHolding<VtVec3hArray>()) {
        return _ComputeFlattened<GfVec3h>(value, indices);           
    } else if (value.IsHolding<VtVec4fArray>()) {
        return _ComputeFlattened<GfVec4f>(value, indices);
    } else if (value.IsHolding<VtVec4dArray>()) {
        return _ComputeFlattened<GfVec4d>(value, indices);                
    } else if (value.IsHolding<VtVec4iArray>()) {
        return _ComputeFlattened<GfVec4i>(value, indices);
    } else if (value.IsHolding<VtVec4hArray>()) {
        return _ComputeFlattened<GfVec4h>(value, indices); 
    } else if (value.IsHolding<VtMatrix3dArray>()) {
        return _ComputeFlattened<GfMatrix3d>(value, indices);  
    } else if (value.IsHolding<VtMatrix4dArray>()) {
        return _ComputeFlattened<GfMatrix4d>(value, indices);  
    } else if (value.IsHolding<VtStringArray>()) {
        return _ComputeFlattened<std::string>(value, indices);  
    } else if (value.IsHolding<VtDoubleArray>()) {
        return _ComputeFlattened<double>(value, indices);  
    } else if (value.IsHolding<VtIntArray>()) {
        return _ComputeFlattened<int>(value, indices); 
    } else if (value.IsHolding<VtUIntArray>()) {
        return _ComputeFlattened<unsigned int>(value, indices); 
    } else if (value.IsHolding<VtFloatArray>()) {
        return _ComputeFlattened<float>(value, indices);
    } else if (value.IsHolding<VtHalfArray>()) {
        return _ComputeFlattened<GfHalf>(value, indices); 
    } else {
        TF_WARN("Type of primvar not yet fully supported");
    }
    return value;
}

}

/*virtual*/
VtValue
HdUnitTestDelegate::Get(SdfPath const& id, TfToken const& key)
{
    HD_TRACE_FUNCTION();

    // camera, light, tasks
    if (_tasks.find(id) != _tasks.end()) {
        return _tasks[id].params[key];
    } else if (_lights.find(id) != _lights.end()) {
        return _lights[id].params[key];
    }

    VtValue value;
    if (key == HdTokens->points) {
        // Each of the prim types hold onto their points
        if(_meshes.find(id) != _meshes.end()) {
            return VtValue(_meshes[id].points);
        }
        else if(_curves.find(id) != _curves.end()) {
            return VtValue(_curves[id].points);
        }
        else if(_points.find(id) != _points.end()) {
            return VtValue(_points[id].points);
        }
    } else if (key == HdInstancerTokens->instanceScales) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].scale);
        }
    } else if (key == HdInstancerTokens->instanceRotations) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].rotate);
        }
    } else if (key == HdInstancerTokens->instanceTranslations) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].translate);
        }
    } else {
        // Check if key is a primvar
        _Primvars::iterator pvIt;
        if (_FindPrimvar(id, key, &pvIt)) {
            if (pvIt->indices.empty()) {
                value = pvIt->value;
            } else {
                // Flatten primvar
                value = pvIt->value;
                if (value.IsArrayValued()) {
                    value = _ComputeFlattenedValue(value, pvIt->indices);
                }
            }
              
        }
    }
    return value;
}

VtValue
HdUnitTestDelegate::GetIndexedPrimvar(SdfPath const& id, TfToken const& key, 
                                      VtIntArray *outIndices) {
    VtValue value;
    if (key == HdTokens->points) {
        // Each of the prim types hold onto their points
        if(_meshes.find(id) != _meshes.end()) {
            return VtValue(_meshes[id].points);
        }
        else if(_curves.find(id) != _curves.end()) {
            return VtValue(_curves[id].points);
        }
        else if(_points.find(id) != _points.end()) {
            return VtValue(_points[id].points);
        }
    } else if (key == HdInstancerTokens->instanceScales) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].scale);
        }
    } else if (key == HdInstancerTokens->instanceRotations) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].rotate);
        }
    } else if (key == HdInstancerTokens->instanceTranslations) {
        if (_instancers.find(id) != _instancers.end()) {
            return VtValue(_instancers[id].translate);
        }
    } else {
        // Check if key is a primvar
        _Primvars::iterator pvIt;
        if (_FindPrimvar(id, key, &pvIt)) {
            value = pvIt->value;
            if (outIndices) {
                *outIndices = pvIt->indices;
            }
        }
    }
    return value;
}

/* virtual */
HdReprSelector
HdUnitTestDelegate::GetReprSelector(SdfPath const &id)
{
    HD_TRACE_FUNCTION();

    if (_meshes.find(id) != _meshes.end()) {
        return _meshes[id].reprSelector;
    }
    return HdReprSelector();
}

/* virtual */
HdPrimvarDescriptorVector
HdUnitTestDelegate::GetPrimvarDescriptors(SdfPath const& id,
                                          HdInterpolation interpolation)
{
    HD_TRACE_FUNCTION();

    HdPrimvarDescriptorVector primvars;

    if (interpolation == HdInterpolationVertex) {
        primvars.emplace_back(HdTokens->points, interpolation,
                              HdPrimvarRoleTokens->point);
    }
    if (interpolation == HdInterpolationInstance && _hasInstancePrimvars &&
        _instancers.find(id) != _instancers.end()) {
        primvars.emplace_back(HdInstancerTokens->instanceScales,
            interpolation);
        primvars.emplace_back(HdInstancerTokens->instanceRotations,
            interpolation);
        primvars.emplace_back(HdInstancerTokens->instanceTranslations,
            interpolation);
    }

    auto const cit = _primvars.find(id);
    if (cit != _primvars.end()) {
        _Primvars const& pvs = cit->second;
        for (auto const& pv : pvs) {
            if (pv.interp == interpolation) {
                primvars.emplace_back(pv.name, pv.interp, pv.role, 
                                      !pv.indices.empty());
            }
        }
    }

    return primvars;
}

void
HdUnitTestDelegate::AddCube(SdfPath const &id, GfMatrix4f const &transform, bool guide,
                             SdfPath const &instancerId, TfToken const &scheme)
{
    GfVec3f points[] = {
        GfVec3f( 1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f,-1.0f, 1.0f ),
        GfVec3f( 1.0f,-1.0f, 1.0f ),
        GfVec3f(-1.0f,-1.0f,-1.0f ),
        GfVec3f(-1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f,-1.0f,-1.0f ),
    };

    if (scheme == PxOsdOpenSubdivTokens->loop) {
        int numVerts[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
        int verts[] = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            0, 6, 5, 0, 5, 1,
            4, 7, 3, 4, 3, 2,
            0, 3, 7, 0, 7, 6,
            4, 2, 1, 4, 1, 5,
        };
        AddMesh(
            id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            guide,
            instancerId,
            scheme);
    } else {
        int numVerts[] = { 4, 4, 4, 4, 4, 4 };
        int verts[] = {
            0, 1, 2, 3,
            4, 5, 6, 7,
            0, 6, 5, 1,
            4, 7, 3, 2,
            0, 3, 7, 6,
            4, 2, 1, 5,
        };
        AddMesh(
            id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            guide,
            instancerId,
            scheme);
    }
}

void
HdUnitTestDelegate::AddCube(SdfPath const &id, GfMatrix4f const &transform,
                            bool guide, SdfPath const &instancerId,
                            TfToken const &scheme, VtValue const &color,
                            HdInterpolation colorInterpolation,
                            VtValue const &opacity,
                            HdInterpolation opacityInterpolation)
{
    GfVec3f points[] = {
        GfVec3f( 1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f,-1.0f, 1.0f ),
        GfVec3f( 1.0f,-1.0f, 1.0f ),
        GfVec3f(-1.0f,-1.0f,-1.0f ),
        GfVec3f(-1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f,-1.0f,-1.0f ),
    };

    if (scheme == PxOsdOpenSubdivTokens->loop) {
        int numVerts[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
        int verts[] = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            0, 6, 5, 0, 5, 1,
            4, 7, 3, 4, 3, 2,
            0, 3, 7, 0, 7, 6,
            4, 2, 1, 4, 1, 5,
        };
        AddMesh(
            id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            /*holes*/VtIntArray(),
            PxOsdSubdivTags(),
            color,
            colorInterpolation,
            opacity,
            opacityInterpolation,
            guide,
            instancerId,
            scheme);
    } else {
        int numVerts[] = { 4, 4, 4, 4, 4, 4 };
        int verts[] = {
            0, 1, 2, 3,
            4, 5, 6, 7,
            0, 6, 5, 1,
            4, 7, 3, 2,
            0, 3, 7, 6,
            4, 2, 1, 5,
        };
        AddMesh(
            id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            /*holes*/VtIntArray(),
            PxOsdSubdivTags(),
            color,
            colorInterpolation,
            opacity,
            opacityInterpolation,
            guide,
            instancerId,
            scheme);
    }
}

void
HdUnitTestDelegate::AddPolygons(
    SdfPath const &id, GfMatrix4f const &transform,
    HdInterpolation colorInterp,
    SdfPath const &instancerId)
{
    int numVerts[] = { 3, 4, 5 };
    int verts[] = {
        0, 1, 2,
        1, 3, 4, 2,
        3, 5, 6, 7, 4
    };
    GfVec3f points[] = {
        GfVec3f(-2.0f,  0.0f, -0.5f ),
        GfVec3f(-1.0f, -1.0f,  0.0f ),
        GfVec3f(-1.0f,  1.0f,  0.0f ),
        GfVec3f( 0.0f, -1.0f,  0.2f ),
        GfVec3f( 0.0f,  1.0f,  0.2f ),
        GfVec3f( 1.0f, -1.0f,  0.0f ),
        GfVec3f( 2.0f,  0.0f, -0.5f ),
        GfVec3f( 1.0f,  1.0f,  0.0f ),
    };


    PxOsdSubdivTags subdivTags;
    VtIntArray holes;
    VtValue color;

    if (colorInterp == HdInterpolationConstant) {
        color = VtValue(GfVec3f(1, 1, 0));
    } else if (colorInterp == HdInterpolationUniform) {
        GfVec3f colors[] = { GfVec3f(1, 0, 0),
                             GfVec3f(0, 0, 1),
                             GfVec3f(0, 1, 0) };
        color = VtValue(_BuildArray(&colors[0], sizeof(colors)/sizeof(colors[0])));
    } else if (colorInterp == HdInterpolationVertex) {
        VtVec3fArray colorArray(sizeof(points)/sizeof(points[0]));
        for (size_t i = 0; i < colorArray.size(); ++i) {
            colorArray[i] = GfVec3f(fabs(sin(0.5*i)),
                                    fabs(cos(0.7*i)),
                                    fabs(sin(0.9*i)*cos(0.25*i)));
        }
        color = VtValue(colorArray);
    } else if (colorInterp == HdInterpolationFaceVarying) {
        VtVec3fArray colorArray(sizeof(verts)/sizeof(verts[0]));
        for (size_t i = 0; i < colorArray.size(); ++i) {
            colorArray[i] = GfVec3f(fabs(sin(0.5*i)),
                                    fabs(cos(0.7*i)),
                                    fabs(sin(0.9*i)*cos(0.25*i)));
        }
        color = VtValue(colorArray);
    }

    VtValue opacity = VtValue(1.0f);

    AddMesh(id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            holes,
            subdivTags,
            color,
            colorInterp,
            opacity,
            HdInterpolationConstant,
            false,
            instancerId);
}

void
HdUnitTestDelegate::AddFaceVaryingPolygons(
    SdfPath const &id, GfMatrix4f const &transform,
    SdfPath const &instancerId)
{
    int numVerts[] = { 3, 4, 5 };
    int verts[] = {
        0, 1, 2,
        1, 3, 4, 2,
        3, 5, 6, 7, 4
    };
    GfVec3f points[] = {
        GfVec3f(-2.0f,  0.0f, -0.5f ),
        GfVec3f(-1.0f, -1.0f,  0.0f ),
        GfVec3f(-1.0f,  1.0f,  0.0f ),
        GfVec3f( 0.0f, -1.0f,  0.2f ),
        GfVec3f( 0.0f,  1.0f,  0.2f ),
        GfVec3f( 1.0f, -1.0f,  0.0f ),
        GfVec3f( 2.0f,  0.0f, -0.5f ),
        GfVec3f( 1.0f,  1.0f,  0.0f ),
    };

    PxOsdSubdivTags subdivTags;
    VtIntArray holes;

    VtVec3fArray colorArray = {
        GfVec3f(1, 0, 0),
        GfVec3f(1, 0.3, 0), 
        GfVec3f(1, 1, 0), 
        GfVec3f(0, 1, 0), 
        GfVec3f(0, 1, 1),
        GfVec3f(0, 0, 1), 
        GfVec3f(0.5, 0, 0.5), 
        GfVec3f(1, 0.4, 0.7), 
        GfVec3f(1, 1, 1),
        GfVec3f(1, 0, 0),
        GfVec3f(1, 0.3, 0), 
        GfVec3f(1, 1, 0) 
    };

    VtFloatArray opacityArray = { 
        1.0f, 1.0f, 1.0f, 0.6f, 0.6f, 0.6f, 0.6f,
        0.75f, 0.75f, 0.75f, 0.75f, 0.75f
    };
    VtIntArray indices = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    AddMesh(id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            holes,
            subdivTags,
            VtValue(colorArray),
            indices,
            HdInterpolationFaceVarying,
            VtValue(opacityArray),
            indices,
            HdInterpolationFaceVarying,
            false,
            instancerId);
}

static void
_CreateGrid(int nx, int ny, std::vector<GfVec3f> *points,
            std::vector<int> *numVerts, std::vector<int> *verts,
            GfMatrix4f const &transform)
{
    if (nx == 0 && ny == 0) return;
    // create a unit plane (-1 ~ 1)
    for (int y = 0; y <= ny; ++y) {
        for (int x = 0; x <= nx; ++x) {
            GfVec3f p(2.0*x/float(nx) - 1.0,
                      2.0*y/float(ny) - 1.0,
                      0);
            points->push_back(p);
        }
    }
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            numVerts->push_back(4);
            verts->push_back(    y*(nx+1) + x    );
            verts->push_back(    y*(nx+1) + x + 1);
            verts->push_back((y+1)*(nx+1) + x + 1);
            verts->push_back((y+1)*(nx+1) + x    );
        }
    }
}

void
HdUnitTestDelegate::AddGrid(SdfPath const &id, int nx, int ny,
                             GfMatrix4f const &transform,
                             bool rightHanded, bool doubleSided,
                             SdfPath const &instancerId)
{
    std::vector<GfVec3f> points;
    std::vector<int> numVerts;
    std::vector<int> verts;
    _CreateGrid(nx, ny, &points, &numVerts, &verts, transform);

    AddMesh(id,
            transform,
            _BuildArray(&points[0], points.size()),
            _BuildArray(&numVerts[0], numVerts.size()),
            _BuildArray(&verts[0], verts.size()),
            false,
            instancerId,
            PxOsdOpenSubdivTokens->catmullClark,
            rightHanded ? HdTokens->rightHanded : HdTokens->leftHanded,
            doubleSided);
}

void
HdUnitTestDelegate::AddGridWithCustomColor(SdfPath const &id, int nx, int ny,
                                           GfMatrix4f const &transform,
                                           VtValue const &color,
                                           HdInterpolation  colorInterpolation,
                                           bool rightHanded, bool doubleSided,
                                           SdfPath const &instancerId)
{
    std::vector<GfVec3f> points;
    std::vector<int> numVerts;
    std::vector<int> verts;
    VtIntArray holes;
    PxOsdSubdivTags subdivTags;
    _CreateGrid(nx, ny, &points, &numVerts, &verts, transform);
    VtValue opacity = VtValue(1.0f);

    AddMesh(id,
            transform,
            _BuildArray(&points[0], points.size()),
            _BuildArray(&numVerts[0], numVerts.size()),
            _BuildArray(&verts[0], verts.size()),
            holes,
            subdivTags,
            color,
            colorInterpolation,
            opacity,
            HdInterpolationConstant,
            false,
            instancerId,
            PxOsdOpenSubdivTokens->catmullClark,
            rightHanded ? HdTokens->rightHanded : HdTokens->leftHanded,
            doubleSided);
}

void
HdUnitTestDelegate::AddGridWithFaceColor(SdfPath const &id, int nx, int ny,
                                          GfMatrix4f const &transform,
                                          bool rightHanded, bool doubleSided,
                                          SdfPath const &instancerId)
{
    std::vector<GfVec3f> points;
    std::vector<int> numVerts;
    std::vector<int> verts;
    VtIntArray holes;
    PxOsdSubdivTags subdivTags;
    _CreateGrid(nx, ny, &points, &numVerts, &verts, transform);

    VtVec3fArray colorArray(numVerts.size());
    for (size_t i = 0; i < numVerts.size(); ++i) {
        colorArray[i] = GfVec3f(fabs(sin(0.1*i)),
                                fabs(cos(0.3*i)),
                                fabs(sin(0.7*i)*cos(0.25*i)));
    }

    AddMesh(id,
            transform,
            _BuildArray(&points[0], points.size()),
            _BuildArray(&numVerts[0], numVerts.size()),
            _BuildArray(&verts[0], verts.size()),
            holes,
            subdivTags,
            VtValue(colorArray),
            HdInterpolationUniform,
            VtValue(1.0f),
            HdInterpolationConstant,
            false,
            instancerId,
            PxOsdOpenSubdivTokens->catmullClark,
            rightHanded ? HdTokens->rightHanded : HdTokens->leftHanded,
            doubleSided);
}

void
HdUnitTestDelegate::AddGridWithVertexColor(SdfPath const &id, int nx, int ny,
                                            GfMatrix4f const &transform,
                                            bool rightHanded, bool doubleSided,
                                            SdfPath const &instancerId)
{
    std::vector<GfVec3f> points;
    std::vector<int> numVerts;
    std::vector<int> verts;
    VtIntArray holes;
    PxOsdSubdivTags subdivTags;
    _CreateGrid(nx, ny, &points, &numVerts, &verts, transform);

    VtVec3fArray colorArray(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        colorArray[i] = GfVec3f(fabs(sin(0.1*i)),
                                fabs(cos(0.3*i)),
                                fabs(sin(0.7*i)*cos(0.25*i)));
    }

    AddMesh(id,
            transform,
            _BuildArray(&points[0], points.size()),
            _BuildArray(&numVerts[0], numVerts.size()),
            _BuildArray(&verts[0], verts.size()),
            holes,
            subdivTags,
            VtValue(colorArray),
            HdInterpolationVertex,
            VtValue(1.0f),
            HdInterpolationConstant,
            false,
            instancerId,
            PxOsdOpenSubdivTokens->catmullClark,
            rightHanded ? HdTokens->rightHanded : HdTokens->leftHanded,
            doubleSided);
}

void
HdUnitTestDelegate::AddGridWithFaceVaryingColor(SdfPath const &id, int nx, int ny,
                                                 GfMatrix4f const &transform,
                                                 bool rightHanded, bool doubleSided,
                                                 SdfPath const &instancerId)
{
    std::vector<GfVec3f> points;
    std::vector<int> numVerts;
    std::vector<int> verts;
    VtIntArray holes;
    PxOsdSubdivTags subdivTags;
    _CreateGrid(nx, ny, &points, &numVerts, &verts, transform);

    VtVec3fArray colorArray(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        colorArray[i] = GfVec3f(fabs(sin(0.1*i)),
                                fabs(cos(0.3*i)),
                                fabs(sin(0.7*i)*cos(0.25*i)));
    }

    AddMesh(id,
            transform,
            _BuildArray(&points[0], points.size()),
            _BuildArray(&numVerts[0], numVerts.size()),
            _BuildArray(&verts[0], verts.size()),
            holes,
            subdivTags,
            VtValue(colorArray),
            HdInterpolationFaceVarying,
            VtValue(1.0f),
            HdInterpolationConstant,
            false,
            instancerId,
            PxOsdOpenSubdivTokens->catmullClark,
            rightHanded ? HdTokens->rightHanded : HdTokens->leftHanded,
            doubleSided);
}

void
HdUnitTestDelegate::AddCurves(
    SdfPath const &id, TfToken const &type, 
    TfToken const &basis, GfMatrix4f const &transform,
    HdInterpolation colorInterp,
    HdInterpolation widthInterp,
    bool authoredNormals,
    SdfPath const &instancerId)
{
    int curveVertexCounts[] = { 4, 4 };

    GfVec3f points[] = {
        GfVec3f( 1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f, 1.0f, 1.0f ),
        GfVec3f(-1.0f,-1.0f, 1.0f ),
        GfVec3f( 1.0f,-1.0f, 1.0f ),

        GfVec3f(-1.0f,-1.0f,-1.0f ),
        GfVec3f(-1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f, 1.0f,-1.0f ),
        GfVec3f( 1.0f,-1.0f,-1.0f ),
    };

    VtVec3fArray authNormals;
    if (authoredNormals) {
        GfVec3f normals[] = {
            GfVec3f(   .0f, -.7f,  .7f ),
            GfVec3f(   .0f,  .0f, 1.0f ),
            GfVec3f(  .0f,  .7f,  .7f ),
            GfVec3f(  .7f,  .7f,  .0f ),
            GfVec3f(   .0f,  .0f, 1.0f ),
            GfVec3f(   .0f,  .0f, 1.0f ),
            GfVec3f( -1.0f,  .0f,  .0f ),
            GfVec3f( -1.0f,  .0f,  .0f )
        };
        authNormals = _BuildArray(normals, sizeof(normals)/sizeof(normals[0]));
    }

    for(size_t i = 0;i < sizeof(points) / sizeof(points[0]); ++ i) {
        GfVec4f tmpPoint = GfVec4f(points[i][0], points[i][1], points[i][2], 1.0f);
        tmpPoint = tmpPoint * transform;
        points[i] = GfVec3f(tmpPoint[0], tmpPoint[1], tmpPoint[2]);
    }

    VtValue color;
    if (colorInterp == HdInterpolationConstant) {
        color = VtValue(GfVec3f(1));
    } else if (colorInterp == HdInterpolationUniform) {
        GfVec3f colors[] = { GfVec3f(1, 0, 0), GfVec3f(0, 0, 1) };
        color = VtValue(_BuildArray(&colors[0], sizeof(colors)/sizeof(colors[0])));
    } else if (colorInterp == HdInterpolationVertex) {
        GfVec3f colors[] = { GfVec3f(0, 0, 1),
                             GfVec3f(0, 1, 0),
                             GfVec3f(0, 1, 1),
                             GfVec3f(1, 0, 0),
                             GfVec3f(1, 0, 1),
                             GfVec3f(1, 1, 0),
                             GfVec3f(1, 1, 1),
                             GfVec3f(0.5, 0.5, 1) };
        color = VtValue(_BuildArray(&colors[0], sizeof(colors)/sizeof(colors[0])));
    }

    VtValue width;

    if (widthInterp == HdInterpolationConstant) {
        width = VtValue(0.1f);
    } else if (widthInterp == HdInterpolationUniform) {
        float widths[] = { 0.1f, 0.4f };
        width = VtValue(_BuildArray(&widths[0], sizeof(widths)/sizeof(widths[0])));
    } else if (widthInterp == HdInterpolationVertex) {
        float widths[] = { 0, 0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.2f, 0.1f };
        width = VtValue(_BuildArray(&widths[0], sizeof(widths)/sizeof(widths[0])));
    } else if (type == HdTokens->cubic && widthInterp == HdInterpolationVarying) {
        float widths[] = { 0, 0.1f, 0.2f, 0.3f};
        width = VtValue(_BuildArray(&widths[0], sizeof(widths)/sizeof(widths[0])));
    } else if (type == HdTokens->linear && widthInterp == HdInterpolationVarying) {
        float widths[] = { 0, 0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.2f, 0.1f };
        width = VtValue(_BuildArray(&widths[0], sizeof(widths)/sizeof(widths[0])));
    }

    AddBasisCurves(
        id,
        _BuildArray(points, sizeof(points)/sizeof(points[0])),
        _BuildArray(curveVertexCounts,
                    sizeof(curveVertexCounts)/sizeof(curveVertexCounts[0])),
        /*curveIndices=*/VtIntArray(),
        authNormals,
        type,
        basis,
        color, colorInterp,
        VtValue(1.0f), HdInterpolationConstant,
        width, widthInterp,
        instancerId);
}

void
HdUnitTestDelegate::SetCurveWrapMode(
    SdfPath const &id, TfToken const &wrap)
{
    if (_curves.find(id) != _curves.end()) {
        if (_curves[id].wrap != wrap) {
            _curves[id].wrap = wrap;
            HdChangeTracker& tracker = GetRenderIndex().GetChangeTracker();
            tracker.MarkRprimDirty(id, HdChangeTracker::DirtyTopology);
        }
    } else {
        TF_WARN("Could not find Rprim named %s.\n", id.GetText());
    }
}

void
HdUnitTestDelegate::AddPoints(
    SdfPath const &id, GfMatrix4f const &transform,
    HdInterpolation colorInterp,
    HdInterpolation widthInterp,
    SdfPath const &instancerId)
{
    int numPoints = 500;

    VtVec3fArray points(numPoints);
    float s = 0, t = 0;
    for (int i = 0; i < numPoints; ++i) {
        GfVec4f p (sin(s)*cos(t), sin(s)*sin(t), cos(s), 1);
        p = p * transform;
        points[i] = GfVec3f(p[0], p[1], p[2]);;
        s += 0.10;
        t += 0.34;
    }

    VtValue color;
    if (colorInterp == HdInterpolationConstant ||
        colorInterp == HdInterpolationUniform) {
        color = VtValue(GfVec3f(1, 1, 1));
    } else if (colorInterp == HdInterpolationVertex) {
        VtVec3fArray colors(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            colors[i] = GfVec3f(fabs(sin(0.1*i)),
                                fabs(cos(0.3*i)),
                                fabs(sin(0.7*i)*cos(0.25*i)));
        }
        color = VtValue(colors);
    }

    VtValue width;
    if (widthInterp == HdInterpolationConstant ||
        widthInterp == HdInterpolationUniform) {
        width = VtValue(0.1f);
    } else { // VERTEX
        VtFloatArray widths(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            widths[i] = 0.1*fabs(sin(0.1*i));
        }
        width = VtValue(widths);
    }

    AddPoints(
        id, points,
        color, colorInterp,
        VtValue(1.0f), HdInterpolationConstant,
        width, widthInterp,
        instancerId);
}

void
HdUnitTestDelegate::AddSubdiv(SdfPath const &id, GfMatrix4f const &transform,
                               SdfPath const &instancerId)
{
/*

     0-----3-------4-----7
     |     ||      |     |
     |     || hole |     |
     |     ||       \    |
     1-----2--------[5]--6
           |        /    |
           |       |     |
           |       |     |
           8-------9----10

       =  : creased edge
       [] : corner vertex

*/
    int numVerts[] = { 4, 4, 4, 4, 4};
    int verts[] = {
        0, 1, 2, 3,
        3, 2, 5, 4,
        4, 5, 6, 7,
        2, 8, 9, 5,
        5, 9, 10, 6,
    };
    GfVec3f points[] = {
        GfVec3f(-1.0f, 0.0f,  1.0f ),
        GfVec3f(-1.0f, 0.0f,  0.0f ),
        GfVec3f(-0.5f, 0.0f,  0.0f ),
        GfVec3f(-0.5f, 0.0f,  1.0f ),
        GfVec3f( 0.0f, 0.0f,  1.0f ),
        GfVec3f( 0.5f, 0.0f,  0.0f ),
        GfVec3f( 1.0f, 0.0f,  0.0f ),
        GfVec3f( 1.0f, 0.0f,  1.0f ),
        GfVec3f(-0.5f, 0.0f, -1.0f ),
        GfVec3f( 0.0f, 0.0f, -1.0f ),
        GfVec3f( 1.0f, 0.0f, -1.0f ),
    };
    int holes[] = { 1 };
    int creaseLengths[] = { 2 };
    int creaseIndices[] = { 2, 3 };
    float creaseSharpnesses[] = { 5.0f };
    int cornerIndices[] = { 5 };
    float cornerSharpnesses[] = { 5.0f };

    PxOsdSubdivTags subdivTags;
    subdivTags.SetCreaseLengths(_BuildArray(creaseLengths,
        sizeof(creaseLengths)/sizeof(creaseLengths[0])));
    subdivTags.SetCreaseIndices(_BuildArray(creaseIndices,
        sizeof(creaseIndices)/sizeof(creaseIndices[0])));
    subdivTags.SetCreaseWeights(_BuildArray(creaseSharpnesses,
        sizeof(creaseSharpnesses)/sizeof(creaseSharpnesses[0])));
    subdivTags.SetCornerIndices(_BuildArray(cornerIndices,
        sizeof(cornerIndices)/sizeof(cornerIndices[0])));
    subdivTags.SetCornerWeights(_BuildArray(cornerSharpnesses,
        sizeof(cornerSharpnesses)/sizeof(cornerSharpnesses[0])));

    subdivTags.SetVertexInterpolationRule(PxOsdOpenSubdivTokens->edgeOnly);
    subdivTags.SetFaceVaryingInterpolationRule(PxOsdOpenSubdivTokens->edgeOnly);

    AddMesh(id,
            transform,
            _BuildArray(points, sizeof(points)/sizeof(points[0])),
            _BuildArray(numVerts, sizeof(numVerts)/sizeof(numVerts[0])),
            _BuildArray(verts, sizeof(verts)/sizeof(verts[0])),
            _BuildArray(holes, sizeof(holes)/sizeof(holes[0])),
            subdivTags,
            /*color=*/VtValue(GfVec3f(1)),
            /*colorInterpolation=*/HdInterpolationConstant,
            /*opacity=*/VtValue(1.0f),
            HdInterpolationConstant,
            false,
            instancerId);
}

void
HdUnitTestDelegate::Remove(SdfPath const &id)
{
    GetRenderIndex().RemoveRprim(id);
}

void
HdUnitTestDelegate::Clear()
{
    GetRenderIndex().Clear();
}

void
HdUnitTestDelegate::MarkRprimDirty(SdfPath path, HdDirtyBits flag)
{
    GetRenderIndex().GetChangeTracker().MarkRprimDirty(path, flag);
}

GfVec3f
HdUnitTestDelegate::PopulateBasicTestSet()
{
    GfMatrix4d dmat;
    double xPos = 0.0;

    // grids
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddGrid(SdfPath("/grid1"), 10, 10, GfMatrix4f(dmat));

        dmat.SetTranslate(GfVec3d(xPos,  0.0, 0.0));
        AddGridWithFaceColor(SdfPath("/grid2"), 10, 10, GfMatrix4f(dmat));

        dmat.SetTranslate(GfVec3d(xPos,  3.0, 0.0));
        AddGridWithVertexColor(SdfPath("/grid3"), 10, 10, GfMatrix4f(dmat));

        dmat.SetTranslate(GfVec3d(xPos,  6.0, 0.0));
        AddGridWithFaceVaryingColor(SdfPath("/grid3a"), 3, 3, GfMatrix4f(dmat));

        xPos += 3.0;
    }

    // non-quads
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddPolygons(SdfPath("/nonquads1"), GfMatrix4f(dmat),
                             HdInterpolationConstant);

        dmat.SetTranslate(GfVec3d(xPos,  0.0, 0.0));
        AddPolygons(SdfPath("/nonquads2"), GfMatrix4f(dmat),
                             HdInterpolationUniform);

        dmat.SetTranslate(GfVec3d(xPos,  3.0, 0.0));
        AddPolygons(SdfPath("/nonquads3"), GfMatrix4f(dmat),
                             HdInterpolationVertex);

        dmat.SetTranslate(GfVec3d(xPos,  6.0, 0.0));
        AddPolygons(SdfPath("/nonquads4"), GfMatrix4f(dmat),
                             HdInterpolationFaceVarying);

        xPos += 3.0;
    }

    // more grids (backface, single sided)
    {
        // rotate X 180
        dmat.SetRotate(GfRotation(GfVec3d(1, 0, 0), 180.0));
        dmat.SetTranslateOnly(GfVec3d(xPos, -3.0, 0.0));
        AddGrid(SdfPath("/grid4"), 10, 10, GfMatrix4f(dmat));

        // inverse X
        dmat.SetScale(GfVec3d(-1, 1, 1));
        dmat.SetTranslateOnly(GfVec3d(xPos, 0.0, 0.0));
        AddGridWithFaceColor(SdfPath("/grid5"), 10, 10,
                                      GfMatrix4f(dmat));

        // inverse Z
        dmat.SetScale(GfVec3d(1, 1, -1));
        dmat.SetTranslateOnly(GfVec3d(xPos, 3.0, 0.0));
        AddGridWithVertexColor(SdfPath("/grid6"), 10, 10,
                                      GfMatrix4f(dmat));

        // left-handed
        dmat.SetTranslate(GfVec3d(xPos, 6.0, 0.0));
        AddGridWithFaceVaryingColor(SdfPath("/grid7"), 3, 3,
                                             GfMatrix4f(dmat),
                                             /*rightHanded=*/false);

        xPos += 3.0;
    }

    // more grids (backface, double sided)
    {
        // rotate X 180
        dmat.SetRotate(GfRotation(GfVec3d(1, 0, 0), 180.0));
        dmat.SetTranslateOnly(GfVec3d(xPos, -3.0, 0.0));
        AddGrid(SdfPath("/grid8"), 10, 10, GfMatrix4f(dmat),
                         /*rightHanded=*/true, /*doubleSided=*/true);

        // inverse X
        dmat.SetScale(GfVec3d(-1, 1, 1));
        dmat.SetTranslateOnly(GfVec3d(xPos, 0.0, 0.0));
        AddGridWithFaceColor(SdfPath("/grid9"), 10, 10,
                                      GfMatrix4f(dmat),
                                      /*rightHanded=*/true,
                                      /*doubleSided=*/true);

        // inverse Z
        dmat.SetScale(GfVec3d(1, 1, -1));
        dmat.SetTranslateOnly(GfVec3d(xPos, 3.0, 0.0));
        AddGridWithVertexColor(SdfPath("/grid10"), 10, 10,
                                      GfMatrix4f(dmat),
                                      /*rightHanded=*/true,
                                      /*doubleSided=*/true);

        // left-handed
        dmat.SetTranslate(GfVec3d(xPos, 6.0, 0.0));
        AddGridWithFaceVaryingColor(SdfPath("/grid11"), 3, 3,
                                             GfMatrix4f(dmat),
                                             /*righthanded=*/false,
                                             /*doubleSided=*/true);

        xPos += 3.0;
    }

    // cubes
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddCube(SdfPath("/cube1"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->loop);

        dmat.SetTranslate(GfVec3d(xPos, 0.0, 0.0));
        AddCube(SdfPath("/cube2"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->catmullClark);

        dmat.SetTranslate(GfVec3d(xPos, 3.0, 0.0));
        AddCube(SdfPath("/cube3"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->bilinear);

        xPos += 3.0;
    }

    // cubes with authored reprs
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddCube(SdfPath("/cube4"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->catmullClark);
        SetReprSelector(SdfPath("/cube4"),
                HdReprSelector(HdReprTokens->smoothHull));

        dmat.SetTranslate(GfVec3d(xPos, 0.0, 0.0));
        AddCube(SdfPath("/cube5"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->catmullClark);
        SetReprSelector(SdfPath("/cube5"),
                HdReprSelector(HdReprTokens->hull));

        dmat.SetTranslate(GfVec3d(xPos, 3.0, 0.0));
        AddCube(SdfPath("/cube6"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->catmullClark);
        SetReprSelector(SdfPath("/cube6"),
                HdReprSelector(HdReprTokens->refined));
        SetRefineLevel(SdfPath("/cube6"), std::max(1, _refineLevel));

        dmat.SetTranslate(GfVec3d(xPos, 6.0, 0.0));
        AddCube(SdfPath("/cube7"), GfMatrix4f(dmat), false, SdfPath(),
                         PxOsdOpenSubdivTokens->catmullClark);
        SetReprSelector(SdfPath("/cube7"),
                HdReprSelector(HdReprTokens->wireOnSurf));

        xPos += 3.0;
    }

    // curves
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddCurves(SdfPath("/curve1"), HdTokens->linear, TfToken(), GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationVertex);

        dmat.SetTranslate(GfVec3d(xPos, 0.0, 0.0));
        AddCurves(SdfPath("/curve2"), HdTokens->cubic, HdTokens->bezier, GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationVertex);

        dmat.SetTranslate(GfVec3d(xPos, 3.0, 0.0));
        AddCurves(SdfPath("/curve3"), HdTokens->cubic, HdTokens->bspline, GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationConstant);

        dmat.SetTranslate(GfVec3d(xPos, 6.0, 0.0));
        AddCurves(SdfPath("/curve4"), HdTokens->cubic, HdTokens->catmullRom, GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationConstant);

        xPos += 3.0;
    }

    // points
    {
        dmat.SetTranslate(GfVec3d(xPos, -3.0, 0.0));
        AddPoints(SdfPath("/points1"), GfMatrix4f(dmat),
                       HdInterpolationConstant, HdInterpolationConstant);

        dmat.SetTranslate(GfVec3d(xPos, 0.0, 0.0));
        AddPoints(SdfPath("/points2"), GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationConstant);

        dmat.SetTranslate(GfVec3d(xPos, 3.0, 0.0));
        AddPoints(SdfPath("/points3"), GfMatrix4f(dmat),
                           HdInterpolationVertex, HdInterpolationVertex);
    }

    return GfVec3f(xPos/2.0, 0, 0);
}

GfVec3f
HdUnitTestDelegate::PopulateInvalidPrimsSet()
{
    // empty mesh
    AddGrid(SdfPath("/empty_mesh"), 0, 0, GfMatrix4f(1));

    // empty curve
    AddBasisCurves(SdfPath("/empty_curve"),
                            VtVec3fArray(),
                            VtIntArray(),
                            VtIntArray(),
                            VtVec3fArray(),
                            HdTokens->linear,
                            TfToken(),
                            VtValue(GfVec3f(1)), HdInterpolationConstant,
                            VtValue(1.0f), HdInterpolationConstant,
                            VtValue(1.0f), HdInterpolationConstant);

    // empty point
    AddPoints(SdfPath("/empty_points"),
                       VtVec3fArray(),
                       VtValue(GfVec4f(1)), HdInterpolationConstant,
                       VtValue(1.0f), HdInterpolationConstant,
                       VtValue(1.0f), HdInterpolationConstant);

    return GfVec3f(0);
}

VtValue
HdUnitTestDelegate::_GetPrimvarValue(SdfPath const& id, TfToken const& name)
{
    _Primvars::iterator pvIt;
    if (_FindPrimvar(id, name, &pvIt)) {
        return pvIt->value;
    }

    TF_WARN("Rprim %s has no primvar named %s.\n",
        id.GetText(), name.GetText());
    return VtValue();
}

bool
HdUnitTestDelegate::_FindPrimvar(SdfPath const& id,
                                 TfToken const& name,
                                 _Primvars::iterator *pvIt)
{
    auto it = _primvars.find(id);
    if (it == _primvars.end()) {
        return false;
    }

    _Primvars& primvars = it->second;

    HdUnitTestDelegate::_Primvars::iterator it2 =
        std::find_if(primvars.begin(), primvars.end(),
            [&name](const _Primvar& x) { return x.name == name; });

    if (it2 == primvars.end()) {
        return false;
    }

    if (pvIt) {
        *pvIt = it2;
    }
    return true;
}


PXR_NAMESPACE_CLOSE_SCOPE

