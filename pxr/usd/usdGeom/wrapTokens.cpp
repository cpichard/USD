//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include "pxr/usd/usdGeom/tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return UsdGeomTokens->name.GetString(); });

void wrapUsdGeomTokens()
{
    pxr_boost::python::class_<UsdGeomTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, accelerations);
    _ADD_TOKEN(cls, all);
    _ADD_TOKEN(cls, angularVelocities);
    _ADD_TOKEN(cls, axis);
    _ADD_TOKEN(cls, basis);
    _ADD_TOKEN(cls, bezier);
    _ADD_TOKEN(cls, bilinear);
    _ADD_TOKEN(cls, boundaries);
    _ADD_TOKEN(cls, bounds);
    _ADD_TOKEN(cls, box);
    _ADD_TOKEN(cls, bspline);
    _ADD_TOKEN(cls, cards);
    _ADD_TOKEN(cls, catmullClark);
    _ADD_TOKEN(cls, catmullRom);
    _ADD_TOKEN(cls, clippingPlanes);
    _ADD_TOKEN(cls, clippingRange);
    _ADD_TOKEN(cls, closed);
    _ADD_TOKEN(cls, constant);
    _ADD_TOKEN(cls, cornerIndices);
    _ADD_TOKEN(cls, cornerSharpnesses);
    _ADD_TOKEN(cls, cornersOnly);
    _ADD_TOKEN(cls, cornersPlus1);
    _ADD_TOKEN(cls, cornersPlus2);
    _ADD_TOKEN(cls, creaseIndices);
    _ADD_TOKEN(cls, creaseLengths);
    _ADD_TOKEN(cls, creaseSharpnesses);
    _ADD_TOKEN(cls, cross);
    _ADD_TOKEN(cls, cubic);
    _ADD_TOKEN(cls, curveVertexCounts);
    _ADD_TOKEN(cls, default_);
    _ADD_TOKEN(cls, doubleSided);
    _ADD_TOKEN(cls, edge);
    _ADD_TOKEN(cls, edgeAndCorner);
    _ADD_TOKEN(cls, edgeOnly);
    _ADD_TOKEN(cls, elementSize);
    _ADD_TOKEN(cls, elementType);
    _ADD_TOKEN(cls, exposure);
    _ADD_TOKEN(cls, exposureFStop);
    _ADD_TOKEN(cls, exposureIso);
    _ADD_TOKEN(cls, exposureResponsivity);
    _ADD_TOKEN(cls, exposureTime);
    _ADD_TOKEN(cls, extent);
    _ADD_TOKEN(cls, extentsHint);
    _ADD_TOKEN(cls, face);
    _ADD_TOKEN(cls, faceVarying);
    _ADD_TOKEN(cls, faceVaryingLinearInterpolation);
    _ADD_TOKEN(cls, faceVertexCounts);
    _ADD_TOKEN(cls, faceVertexIndices);
    _ADD_TOKEN(cls, familyName);
    _ADD_TOKEN(cls, focalLength);
    _ADD_TOKEN(cls, focusDistance);
    _ADD_TOKEN(cls, fromTexture);
    _ADD_TOKEN(cls, fStop);
    _ADD_TOKEN(cls, guide);
    _ADD_TOKEN(cls, guideVisibility);
    _ADD_TOKEN(cls, height);
    _ADD_TOKEN(cls, hermite);
    _ADD_TOKEN(cls, holeIndices);
    _ADD_TOKEN(cls, horizontalAperture);
    _ADD_TOKEN(cls, horizontalApertureOffset);
    _ADD_TOKEN(cls, ids);
    _ADD_TOKEN(cls, inactiveIds);
    _ADD_TOKEN(cls, indices);
    _ADD_TOKEN(cls, inherited);
    _ADD_TOKEN(cls, interpolateBoundary);
    _ADD_TOKEN(cls, interpolation);
    _ADD_TOKEN(cls, invisible);
    _ADD_TOKEN(cls, invisibleIds);
    _ADD_TOKEN(cls, knots);
    _ADD_TOKEN(cls, left);
    _ADD_TOKEN(cls, leftHanded);
    _ADD_TOKEN(cls, length);
    _ADD_TOKEN(cls, linear);
    _ADD_TOKEN(cls, loop);
    _ADD_TOKEN(cls, metersPerUnit);
    _ADD_TOKEN(cls, modelApplyDrawMode);
    _ADD_TOKEN(cls, modelCardGeometry);
    _ADD_TOKEN(cls, modelCardTextureXNeg);
    _ADD_TOKEN(cls, modelCardTextureXPos);
    _ADD_TOKEN(cls, modelCardTextureYNeg);
    _ADD_TOKEN(cls, modelCardTextureYPos);
    _ADD_TOKEN(cls, modelCardTextureZNeg);
    _ADD_TOKEN(cls, modelCardTextureZPos);
    _ADD_TOKEN(cls, modelDrawMode);
    _ADD_TOKEN(cls, modelDrawModeColor);
    _ADD_TOKEN(cls, mono);
    _ADD_TOKEN(cls, motionBlurScale);
    _ADD_TOKEN(cls, motionNonlinearSampleCount);
    _ADD_TOKEN(cls, motionVelocityScale);
    _ADD_TOKEN(cls, none);
    _ADD_TOKEN(cls, nonOverlapping);
    _ADD_TOKEN(cls, nonperiodic);
    _ADD_TOKEN(cls, normals);
    _ADD_TOKEN(cls, open);
    _ADD_TOKEN(cls, order);
    _ADD_TOKEN(cls, orientation);
    _ADD_TOKEN(cls, orientations);
    _ADD_TOKEN(cls, orientationsf);
    _ADD_TOKEN(cls, origin);
    _ADD_TOKEN(cls, orthographic);
    _ADD_TOKEN(cls, partition);
    _ADD_TOKEN(cls, periodic);
    _ADD_TOKEN(cls, perspective);
    _ADD_TOKEN(cls, pinned);
    _ADD_TOKEN(cls, pivot);
    _ADD_TOKEN(cls, point);
    _ADD_TOKEN(cls, points);
    _ADD_TOKEN(cls, pointWeights);
    _ADD_TOKEN(cls, positions);
    _ADD_TOKEN(cls, power);
    _ADD_TOKEN(cls, primvarsDisplayColor);
    _ADD_TOKEN(cls, primvarsDisplayOpacity);
    _ADD_TOKEN(cls, projection);
    _ADD_TOKEN(cls, protoIndices);
    _ADD_TOKEN(cls, prototypes);
    _ADD_TOKEN(cls, proxy);
    _ADD_TOKEN(cls, proxyPrim);
    _ADD_TOKEN(cls, proxyVisibility);
    _ADD_TOKEN(cls, purpose);
    _ADD_TOKEN(cls, radius);
    _ADD_TOKEN(cls, radiusBottom);
    _ADD_TOKEN(cls, radiusTop);
    _ADD_TOKEN(cls, ranges);
    _ADD_TOKEN(cls, render);
    _ADD_TOKEN(cls, renderVisibility);
    _ADD_TOKEN(cls, right);
    _ADD_TOKEN(cls, rightHanded);
    _ADD_TOKEN(cls, scales);
    _ADD_TOKEN(cls, segment);
    _ADD_TOKEN(cls, shutterClose);
    _ADD_TOKEN(cls, shutterOpen);
    _ADD_TOKEN(cls, size);
    _ADD_TOKEN(cls, smooth);
    _ADD_TOKEN(cls, stereoRole);
    _ADD_TOKEN(cls, subdivisionScheme);
    _ADD_TOKEN(cls, surfaceFaceVertexIndices);
    _ADD_TOKEN(cls, tangents);
    _ADD_TOKEN(cls, tetrahedron);
    _ADD_TOKEN(cls, tetVertexIndices);
    _ADD_TOKEN(cls, triangleSubdivisionRule);
    _ADD_TOKEN(cls, trimCurveCounts);
    _ADD_TOKEN(cls, trimCurveKnots);
    _ADD_TOKEN(cls, trimCurveOrders);
    _ADD_TOKEN(cls, trimCurvePoints);
    _ADD_TOKEN(cls, trimCurveRanges);
    _ADD_TOKEN(cls, trimCurveVertexCounts);
    _ADD_TOKEN(cls, type);
    _ADD_TOKEN(cls, uForm);
    _ADD_TOKEN(cls, uKnots);
    _ADD_TOKEN(cls, unauthoredValuesIndex);
    _ADD_TOKEN(cls, uniform);
    _ADD_TOKEN(cls, unrestricted);
    _ADD_TOKEN(cls, uOrder);
    _ADD_TOKEN(cls, upAxis);
    _ADD_TOKEN(cls, uRange);
    _ADD_TOKEN(cls, uVertexCount);
    _ADD_TOKEN(cls, varying);
    _ADD_TOKEN(cls, velocities);
    _ADD_TOKEN(cls, vertex);
    _ADD_TOKEN(cls, verticalAperture);
    _ADD_TOKEN(cls, verticalApertureOffset);
    _ADD_TOKEN(cls, vForm);
    _ADD_TOKEN(cls, visibility);
    _ADD_TOKEN(cls, visible);
    _ADD_TOKEN(cls, vKnots);
    _ADD_TOKEN(cls, vOrder);
    _ADD_TOKEN(cls, vRange);
    _ADD_TOKEN(cls, vVertexCount);
    _ADD_TOKEN(cls, width);
    _ADD_TOKEN(cls, widths);
    _ADD_TOKEN(cls, wrap);
    _ADD_TOKEN(cls, x);
    _ADD_TOKEN(cls, xformOpOrder);
    _ADD_TOKEN(cls, y);
    _ADD_TOKEN(cls, z);
    _ADD_TOKEN(cls, BasisCurves);
    _ADD_TOKEN(cls, Boundable);
    _ADD_TOKEN(cls, Camera);
    _ADD_TOKEN(cls, Capsule);
    _ADD_TOKEN(cls, Capsule_1);
    _ADD_TOKEN(cls, Cone);
    _ADD_TOKEN(cls, Cube);
    _ADD_TOKEN(cls, Curves);
    _ADD_TOKEN(cls, Cylinder);
    _ADD_TOKEN(cls, Cylinder_1);
    _ADD_TOKEN(cls, GeomModelAPI);
    _ADD_TOKEN(cls, GeomSubset);
    _ADD_TOKEN(cls, Gprim);
    _ADD_TOKEN(cls, HermiteCurves);
    _ADD_TOKEN(cls, Imageable);
    _ADD_TOKEN(cls, Mesh);
    _ADD_TOKEN(cls, MotionAPI);
    _ADD_TOKEN(cls, NurbsCurves);
    _ADD_TOKEN(cls, NurbsPatch);
    _ADD_TOKEN(cls, Plane);
    _ADD_TOKEN(cls, PointBased);
    _ADD_TOKEN(cls, PointInstancer);
    _ADD_TOKEN(cls, Points);
    _ADD_TOKEN(cls, PrimvarsAPI);
    _ADD_TOKEN(cls, Scope);
    _ADD_TOKEN(cls, Sphere);
    _ADD_TOKEN(cls, TetMesh);
    _ADD_TOKEN(cls, VisibilityAPI);
    _ADD_TOKEN(cls, Xform);
    _ADD_TOKEN(cls, Xformable);
    _ADD_TOKEN(cls, XformCommonAPI);
}
