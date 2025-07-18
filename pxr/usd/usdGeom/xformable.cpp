//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdGeomXformable,
        TfType::Bases< UsdGeomImageable > >();
    
}

/* virtual */
UsdGeomXformable::~UsdGeomXformable()
{
}

/* static */
UsdGeomXformable
UsdGeomXformable::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdGeomXformable();
    }
    return UsdGeomXformable(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdGeomXformable::_GetSchemaKind() const
{
    return UsdGeomXformable::schemaKind;
}

/* static */
const TfType &
UsdGeomXformable::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdGeomXformable>();
    return tfType;
}

/* static */
bool 
UsdGeomXformable::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdGeomXformable::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdGeomXformable::GetXformOpOrderAttr() const
{
    return GetPrim().GetAttribute(UsdGeomTokens->xformOpOrder);
}

UsdAttribute
UsdGeomXformable::CreateXformOpOrderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdGeomTokens->xformOpOrder,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
UsdGeomXformable::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdGeomTokens->xformOpOrder,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdGeomImageable::GetSchemaAttributeNames(true),
            localNames);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

#include "pxr/base/tf/envSetting.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (transform)
    ((invertPrefix, "!invert!"))
);

using std::vector;

TF_MAKE_STATIC_DATA(GfMatrix4d, _IDENTITY) {
    *_IDENTITY = GfMatrix4d(1.0);
}

bool 
UsdGeomXformable::_GetXformOpOrderValue(VtTokenArray *xformOpOrder) const
{
    UsdAttribute xformOpOrderAttr = GetXformOpOrderAttr();
    if (!xformOpOrderAttr)
        return false;

    xformOpOrderAttr.Get(xformOpOrder, UsdTimeCode::Default());
    return true;
}

UsdGeomXformOp 
UsdGeomXformable::AddXformOp(
    UsdGeomXformOp::Type const opType, 
    UsdGeomXformOp::Precision const precision, 
    TfToken const &opSuffix, 
    bool isInverseOp) const
{
    VtTokenArray xformOpOrder;
    _GetXformOpOrderValue(&xformOpOrder);

    // Check if the xformOp we're about to add already exists in xformOpOrder
    TfToken opName = UsdGeomXformOp::GetOpName(opType, opSuffix, isInverseOp);
    VtTokenArray::iterator it = std::find(xformOpOrder.begin(), 
        xformOpOrder.end(), opName);
    if (it != xformOpOrder.end()) {
        TF_CODING_ERROR("The xformOp '%s' already exists in xformOpOrder [%s].",
            opName.GetText(), TfStringify(xformOpOrder).c_str());
        return UsdGeomXformOp();
    }

    TfToken const &xformOpAttrName = UsdGeomXformOp::GetOpName(opType, opSuffix);
    UsdGeomXformOp result;
    if (UsdAttribute xformOpAttr = GetPrim().GetAttribute(xformOpAttrName)) {
        // Check if the attribute's typeName has the requested precision level.
        UsdGeomXformOp::Precision existingPrecision = 
            UsdGeomXformOp::GetPrecisionFromValueTypeName(
                xformOpAttr.GetTypeName());

        if (existingPrecision != precision) {
            TF_CODING_ERROR("XformOp <%s> has typeName '%s' which does not "
                            "match the requested precision '%s'. Proceeding to "
                            "use existing typeName / precision.",
                            xformOpAttr.GetPath().GetText(),
                            xformOpAttr.GetTypeName().GetAsToken().GetText(),
                            TfEnum::GetName(precision).c_str());
        }

        result = UsdGeomXformOp(xformOpAttr, isInverseOp);
    } else {
        result = UsdGeomXformOp(GetPrim(), opType, precision, opSuffix, 
                                isInverseOp);
    }

    if (result) {
        xformOpOrder.push_back(result.GetOpName());
        CreateXformOpOrderAttr().Set(xformOpOrder);
    } else {
        TF_CODING_ERROR("Unable to add xform op of type %s and precision %s on "
            "prim at path <%s>. opSuffix=%s, isInverseOp=%d", 
            TfEnum::GetName(opType).c_str(), TfEnum::GetName(precision).c_str(), 
            GetPath().GetText(), opSuffix.GetText(), isInverseOp);
        return UsdGeomXformOp();
    }

    return result;
}

UsdGeomXformOp
UsdGeomXformable::GetXformOp(UsdGeomXformOp::Type const opType, 
    TfToken const &opSuffix, bool isInverseOp) const
{
    VtTokenArray xformOpOrder;
    _GetXformOpOrderValue(&xformOpOrder);

    // Check if the xformOp exists in xformOpOrder
    TfToken opName = UsdGeomXformOp::GetOpName(opType, opSuffix, isInverseOp);
    VtTokenArray::iterator it = std::find(xformOpOrder.begin(), 
        xformOpOrder.end(), opName);
    if (it == xformOpOrder.end()) {
        return UsdGeomXformOp();
    }
    
    TfToken const &xformOpAttrName = UsdGeomXformOp::GetOpName(opType, opSuffix);
    UsdAttribute xformOpAttr = GetPrim().GetAttribute(xformOpAttrName);

    return UsdGeomXformOp(xformOpAttr, isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::AddTranslateXOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeTranslateX, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::AddTranslateYOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeTranslateY, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::AddTranslateZOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeTranslateZ, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddTranslateOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeTranslate, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::GetTranslateXOp(
    TfToken const &opSuffix, bool isInverseOp) const 
{
    return GetXformOp(UsdGeomXformOp::TypeTranslateX, opSuffix, isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::GetTranslateYOp(
    TfToken const &opSuffix, bool isInverseOp) const 
{
    return GetXformOp(UsdGeomXformOp::TypeTranslateY, opSuffix, isInverseOp);
}

UsdGeomXformOp
UsdGeomXformable::GetTranslateZOp(
    TfToken const &opSuffix, bool isInverseOp) const 
{
    return GetXformOp(UsdGeomXformOp::TypeTranslateZ, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetTranslateOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeTranslate, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddScaleXOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeScaleX, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddScaleYOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeScaleY, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddScaleZOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeScaleZ, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddScaleOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeScale, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetScaleXOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeScaleX, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetScaleYOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeScaleY, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetScaleZOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeScaleZ, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetScaleOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeScale, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateXOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateX, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateXOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateX, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateYOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateY, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateYOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateY, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateZOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateZ, precision, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateZOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateZ, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateXYZOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateXYZ, precision, opSuffix, 
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateXYZOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateXYZ, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateXZYOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateXZY, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateXZYOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateXZY, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateYXZOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateYXZ, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateYXZOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateYXZ, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateYZXOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateYZX, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateYZXOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateYZX, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateZXYOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateZXY, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateZXYOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateZXY, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddRotateZYXOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeRotateZYX, precision, opSuffix,
                      isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetRotateZYXOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeRotateZYX, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::AddOrientOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeOrient, precision, opSuffix, isInverseOp);
}

UsdGeomXformOp 
UsdGeomXformable::GetOrientOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeOrient, opSuffix, isInverseOp);
}
    
UsdGeomXformOp 
UsdGeomXformable::AddTransformOp(UsdGeomXformOp::Precision const precision,
    TfToken const &opSuffix, bool isInverseOp) const
{
    return AddXformOp(UsdGeomXformOp::TypeTransform, precision, opSuffix, 
                      isInverseOp);
}
    
UsdGeomXformOp 
UsdGeomXformable::GetTransformOp(TfToken const &opSuffix, bool isInverseOp) const
{
    return GetXformOp(UsdGeomXformOp::TypeTransform, opSuffix, isInverseOp);
}

// Returns whether "!resetXformStack! exists in opOrderVec.
static 
bool 
_XformOpOrderHasResetXformStack(const VtTokenArray &opOrderVec)
{
    return std::find(opOrderVec.begin(), opOrderVec.end(), 
                     UsdGeomXformOpTypes->resetXformStack) != opOrderVec.end();
}

bool
UsdGeomXformable::SetResetXformStack(bool resetXformStack) const
{
    VtTokenArray opOrderVec;
    _GetXformOpOrderValue(&opOrderVec);

    bool result = true;
    if (resetXformStack) {
        // Nothing to do if resetXformStack already exists in xformOpOrder
        if (_XformOpOrderHasResetXformStack(opOrderVec))
            return true;

        VtTokenArray newOpOrderVec(opOrderVec.size() + 1);

        newOpOrderVec[0] = UsdGeomXformOpTypes->resetXformStack;
        for (size_t i = 0; i < opOrderVec.size(); i++)
            newOpOrderVec[i+1] = opOrderVec[i];

        result = CreateXformOpOrderAttr().Set(newOpOrderVec);
    } else {
        VtTokenArray newOpOrderVec;
        bool foundResetXformStack = false;
        for (size_t i = 0; i < opOrderVec.size(); i++) {
            if (opOrderVec[i] == UsdGeomXformOpTypes->resetXformStack) {
                foundResetXformStack = true;
                newOpOrderVec.clear();
            } else if (foundResetXformStack) {
                newOpOrderVec.push_back(opOrderVec[i]);
            }
        }

        if (foundResetXformStack) {
            result = CreateXformOpOrderAttr().Set(newOpOrderVec);
        } else {
            // This is a no-op if "!resetXformStack!" isn't present in 
            // xformOpOrder.
        }
    }

    return result;
}

bool
UsdGeomXformable::GetResetXformStack() const
{
    VtTokenArray opOrderVec;
    if (!_GetXformOpOrderValue(&opOrderVec))
        return false;

    return _XformOpOrderHasResetXformStack(opOrderVec);
}

bool 
UsdGeomXformable::SetXformOpOrder(
    vector<UsdGeomXformOp> const &orderedXformOps, 
    bool resetXformStack) const
{
    VtTokenArray ops;
    ops.reserve(orderedXformOps.size() + (resetXformStack ? 1: 0));

    if (resetXformStack)
        ops.push_back(UsdGeomXformOpTypes->resetXformStack);

    TF_FOR_ALL(it, orderedXformOps) {
        // Check to make sure that the xformOp being added to xformOpOrder 
        // belongs to this prim.
        if (it->GetAttr().GetPrim() == GetPrim()) {
            ops.push_back(it->GetOpName());
        } else {
            TF_CODING_ERROR("XformOp attribute <%s> does not belong to schema "
                            "prim <%s>.",  it->GetAttr().GetPath().GetText(),
                            GetPath().GetText()); 
            return false;
        }
    }

    return CreateXformOpOrderAttr().Set(ops);
}

bool
UsdGeomXformable::ClearXformOpOrder() const
{
    return SetXformOpOrder(vector<UsdGeomXformOp>());
}

UsdGeomXformOp
UsdGeomXformable::MakeMatrixXform() const
{
    ClearXformOpOrder();
    bool unused = false;
    if (!GetOrderedXformOps(&unused).empty()) {
        TF_WARN("Could not clear xformOpOrder for <%s>",
            GetPrim().GetPath().GetText());
        return UsdGeomXformOp();
    }
    return AddTransformOp();
}

vector<UsdGeomXformOp>
UsdGeomXformable::GetOrderedXformOps(bool *resetXformStack) const
{
    return _GetOrderedXformOps(
        resetXformStack, /*withAttributeQueries=*/false);
}

vector<UsdGeomXformOp>
UsdGeomXformable::_GetOrderedXformOps(bool *resetsXformStack,
                                      bool withAttributeQueries) const
{
    vector<UsdGeomXformOp> result;

    if (resetsXformStack) {
        *resetsXformStack = false;
    } else {
        TF_CODING_ERROR("resetsXformStack is NULL.");
    }

    VtTokenArray opOrderVec;
    if (!_GetXformOpOrderValue(&opOrderVec)) {
        return result;
    }

    if (opOrderVec.size() == 0) {
        return result;
    }

    // Reserve space for the xform ops.
    result.reserve(opOrderVec.size());

    UsdPrim thisPrim = GetPrim();
    for (VtTokenArray::const_iterator it = opOrderVec.cbegin() ; 
         it != opOrderVec.cend(); ++it) {

        const TfToken &opName = *it;

        // If this is the special resetXformStack op, then clear the currently
        // accreted xformOps and continue.
        if (opName == UsdGeomXformOpTypes->resetXformStack) { 
            if (resetsXformStack) {
                *resetsXformStack = true;
            }
            result.clear();
        } else {
            bool isInverseOp = false;
            UsdAttribute attr = UsdGeomXformOp::_GetXformOpAttr(
                thisPrim, opName, &isInverseOp);
            if (withAttributeQueries) {
                TfErrorMark m;
                UsdAttributeQuery query(attr);
                if (m.IsClean()) {
                    result.emplace_back(
                        std::move(query), isInverseOp,
                        UsdGeomXformOp::_ValidAttributeTagType {});
                }
                else {
                    // Skip invalid xform ops that appear in xformOpOrder, but
                    // issue a warning.
                    TF_WARN("Unable to get attribute associated with the "
                            "xformOp '%s', on the prim at path <%s>. Skipping "
                            "xformOp in the computation of the local "
                            "transformation at prim.",
                            opName.GetText(), GetPrim().GetPath().GetText());
                }                    
            }
            else {
                if (attr) {
                    // Only add valid xform ops.  We pass _ValidAttributeTag here
                    // since we've pre-checked the validity of attr above.
                    result.emplace_back(
                        attr, isInverseOp,
                        UsdGeomXformOp::_ValidAttributeTagType {});
                }
                else {
                    // Skip invalid xform ops that appear in xformOpOrder, but
                    // issue a warning.
                    TF_WARN("Unable to get attribute associated with the "
                            "xformOp '%s', on the prim at path <%s>. Skipping "
                            "xformOp in the computation of the local "
                            "transformation at prim.",
                            opName.GetText(), GetPrim().GetPath().GetText());
                }
            }
        }
    }

    return result;
}

UsdGeomXformable::XformQuery::XformQuery(const UsdGeomXformable &xformable):
    _resetsXformStack(false)
{
    _xformOps = xformable._GetOrderedXformOps(
        &_resetsXformStack, /*withAttributeQueries=*/true);
}

bool 
UsdGeomXformable::XformQuery::GetLocalTransformation(
    GfMatrix4d *transform,
    const UsdTimeCode time) const 
{
    return UsdGeomXformable::GetLocalTransformation(transform, _xformOps, time);
}

bool
UsdGeomXformable::XformQuery::HasNonEmptyXformOpOrder() const
{
    return !_xformOps.empty();
}

static
bool 
_TransformMightBeTimeVarying(vector<UsdGeomXformOp> const &xformOps)
{
    // If any of the xform ops may vary, then the cumulative transform may vary.
    TF_FOR_ALL(it, xformOps) {
        if (it->MightBeTimeVarying())
            return true;
    }

    return false;
}

bool
UsdGeomXformable::XformQuery::TransformMightBeTimeVarying() const
{
    return _TransformMightBeTimeVarying(_xformOps);
}

bool
UsdGeomXformable::TransformMightBeTimeVarying() const
{
    VtTokenArray opOrderVec;
    if (!_GetXformOpOrderValue(&opOrderVec))
        return false;

    if (opOrderVec.size() == 0) {
        return false;
    }
    for (VtTokenArray::const_reverse_iterator it = opOrderVec.crbegin() ; 
         it != opOrderVec.crend(); ++it) {

        const TfToken &opName = *it;

        // If this is the special resetXformStack op, return false to indicate 
        // that none of the xformOps that affect the local transformation are 
        // time-varying (since none of the (valid) xformOps after the last 
        // occurrence of !resetXformStack! are time-varying).
        if (opName == UsdGeomXformOpTypes->resetXformStack) { 
            return false;
        } else {
            bool isInverseOp = false;
            if (UsdAttribute attr = UsdGeomXformOp::_GetXformOpAttr(
                    GetPrim(), opName, &isInverseOp)) {
                // Only check valid xform ops for time-varyingness.
                UsdGeomXformOp op(attr, isInverseOp);
                if (op && op.MightBeTimeVarying()) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool
UsdGeomXformable::TransformMightBeTimeVarying(
    const vector<UsdGeomXformOp> &ops) const
{
    if (!ops.empty())
        return _TransformMightBeTimeVarying(ops);

    // Assume unvarying if neither orderedXformOps nor transform attribute is 
    // authored.
    return false;
}

/* static */
bool
UsdGeomXformable::GetTimeSamples(
    vector<UsdGeomXformOp> const &orderedXformOps,
    vector<double> *times)
{
    return GetTimeSamplesInInterval(orderedXformOps, 
            GfInterval::GetFullInterval(), times);
}

/* static */
bool 
UsdGeomXformable::GetTimeSamplesInInterval(
    std::vector<UsdGeomXformOp> const &orderedXformOps,
    const GfInterval &interval,
    std::vector<double> *times)
{
    // Optimize for the case where there's a single xformOp (typically a 4x4 
    // matrix op). 
    if (orderedXformOps.size() == 1) {
        return orderedXformOps.front().GetTimeSamplesInInterval(interval, 
            times);
    }

    vector<UsdAttribute> xformOpAttrs;
    xformOpAttrs.reserve(orderedXformOps.size());
    for (auto &xformOp : orderedXformOps) {
        xformOpAttrs.push_back(xformOp.GetAttr());
    }

    return UsdAttribute::GetUnionedTimeSamplesInInterval(xformOpAttrs, 
            interval, times);
}

bool 
UsdGeomXformable::GetTimeSamplesInInterval(
    const GfInterval &interval,
    std::vector<double> *times) const
{
    bool resetsXformStack=false;
    const vector<UsdGeomXformOp> &orderedXformOps= GetOrderedXformOps(
        &resetsXformStack);

    return UsdGeomXformable::GetTimeSamplesInInterval(orderedXformOps, interval, 
            times);
}

bool 
UsdGeomXformable::XformQuery::GetTimeSamples(vector<double> *times) const
{
    return GetTimeSamplesInInterval(GfInterval::GetFullInterval(), times);
}

bool 
UsdGeomXformable::XformQuery::GetTimeSamplesInInterval(
    const GfInterval &interval, 
    vector<double> *times) const
{
    if (_xformOps.size() == 1) {
        _xformOps.front().GetTimeSamplesInInterval(interval, times);
    }

    vector<UsdAttributeQuery> xformOpAttrQueries;
    xformOpAttrQueries.reserve(_xformOps.size());
    for (auto &xformOp : _xformOps) {
        // This should never throw and exception because XformQuery's constructor
        // initializes an attribute query for all its xformOps.
        const UsdAttributeQuery &attrQuery = 
            std::get<UsdAttributeQuery>(xformOp._attr);
        xformOpAttrQueries.push_back(attrQuery);
    }
    
    return UsdAttributeQuery::GetUnionedTimeSamplesInInterval(
            xformOpAttrQueries, interval, times);
}

bool 
UsdGeomXformable::GetTimeSamples(vector<double> *times) const
{
    bool resetsXformStack=false;
    const vector<UsdGeomXformOp> &orderedXformOps= GetOrderedXformOps(
        &resetsXformStack);

    return GetTimeSamples(orderedXformOps, times);
}

bool 
UsdGeomXformable::XformQuery::IsAttributeIncludedInLocalTransform(
    const TfToken &attrName) const
{
    TF_FOR_ALL(it, _xformOps) {
        if (it->GetName() == attrName)
            return true;
    }

    return false;
}

// Given two UsdGeomXformOps, returns true if they are inverses of each other.
static bool
_AreInverseXformOps(const UsdGeomXformOp &a, const UsdGeomXformOp &b)
{
    // The two given ops are inverses of each other if they have the same 
    // underlying attribute and only if one of them is an inverseOp.
    return a.GetAttr() == b.GetAttr() &&
           a.IsInverseOp() != b.IsInverseOp();
}

// Given two xformOp names, returns true if they are inverses of each other.
static bool
_AreInverseXformOps(const TfToken &a, const TfToken &b)
{
    return _tokens->invertPrefix.GetString() + a.GetString() == b.GetString() 
        || _tokens->invertPrefix.GetString() + b.GetString() == a.GetString();
}

bool 
UsdGeomXformable::GetLocalTransformation(
    GfMatrix4d *transform,
    bool *resetsXformStack,
    const UsdTimeCode time) const
{
    TRACE_FUNCTION();

    if (transform) {
        *transform = GfMatrix4d(1.);
    }
    else {
        TF_CODING_ERROR("transform is NULL.");
        return false;
    }

    if (resetsXformStack) {
        *resetsXformStack = false;
    } else {
        TF_CODING_ERROR("resetsXformStack is NULL.");
        return false;
    }
 
    VtTokenArray opOrderVec;
    if (!_GetXformOpOrderValue(&opOrderVec))
        return false;

    if (opOrderVec.size() == 0) {
        return true;
    }
    for (VtTokenArray::const_reverse_iterator it = opOrderVec.crbegin() ; 
         it != opOrderVec.crend(); ++it) {
            
        const TfToken &opName = *it;

        // Skip the current xformOp and the next one if they're inverses of 
        // each other.
        if ((it+1) != opOrderVec.crend()) {
            const TfToken &nextOpName = *(it+1);
            if (_AreInverseXformOps(opName, nextOpName)) {
                ++it;
                continue;
            }
        }

        // If this is the special resetXformStack op, then the currently
        // accreted localXform is the local transformation of the prim.
        if (opName == UsdGeomXformOpTypes->resetXformStack) { 
            *resetsXformStack = true;
            break;
        } else {
            bool isInverseOp = false;
            if (UsdAttribute attr = UsdGeomXformOp::_GetXformOpAttr(
                    GetPrim(), opName, &isInverseOp)) {
                // Only add valid xform ops.                
                UsdGeomXformOp op(attr, isInverseOp);
                if (op) {
                    GfMatrix4d opTransform = op.GetOpTransform(time);
                    // Avoid multiplying by the identity matrix when possible.
                    if (opTransform != *_IDENTITY) {
                        (*transform) *= opTransform;
                    }
                }
            } else {
                // Skip invalid xform ops that appear in xformOpOrder, but issue
                // a warning.
                TF_WARN("Unable to get attribute associated with the xformOp "
                    "'%s', on the prim at path <%s>. Skipping xformOp in the "
                    "computation of the local transformation at prim.", 
                    opName.GetText(), GetPrim().GetPath().GetText());
            }
        }
    }
    
    return true;
}

bool 
UsdGeomXformable::GetLocalTransformation(
    GfMatrix4d *transform,
    bool *resetsXformStack,
    const vector<UsdGeomXformOp> &ops,
    const UsdTimeCode time) const
{
    TRACE_FUNCTION();
    
    if (resetsXformStack) {
        *resetsXformStack = GetResetXformStack();
    } else {
        TF_CODING_ERROR("resetsXformStack is NULL.");
    }
    return GetLocalTransformation(transform, ops, time);
}

/* static */
bool 
UsdGeomXformable::GetLocalTransformation(
    GfMatrix4d *transform,
    const vector<UsdGeomXformOp> &orderedXformOps, 
    const UsdTimeCode time)
{
    GfMatrix4d xform(1.);

    for (vector<UsdGeomXformOp>::const_reverse_iterator 
            it = orderedXformOps.rbegin();
         it != orderedXformOps.rend(); ++it) {

        const UsdGeomXformOp &xformOp = *it;

        // Skip the current xformOp and the next one if they're inverses of 
        // each other.
        if ((it+1) != orderedXformOps.rend()) {
            const UsdGeomXformOp &nextXformOp = *(it+1);
            if (_AreInverseXformOps(xformOp, nextXformOp)) {
                ++it;
                continue;
            }
        }

        GfMatrix4d opTransform = xformOp.GetOpTransform(time);
        // Avoid multiplying by the identity matrix when possible.
        if (opTransform != *_IDENTITY) {
            xform *= opTransform;
        }
    }
    
    if (transform) {
        *transform = xform;
        return true;
    } else {
        TF_CODING_ERROR("'transform' pointer is NULL.");
    } 

    return false;
}

/* static */
bool 
UsdGeomXformable::IsTransformationAffectedByAttrNamed(const TfToken &attrName)
{
    return attrName == UsdGeomTokens->xformOpOrder ||
           UsdGeomXformOp::IsXformOp(attrName);
}

PXR_NAMESPACE_CLOSE_SCOPE
