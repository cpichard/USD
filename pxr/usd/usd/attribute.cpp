//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/attributeQuery.h"
#include "pxr/usd/usd/common.h"
#include "pxr/usd/usd/instanceCache.h"

#include "pxr/usd/usd/interpolators.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/valueUtils.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/schema.h"

// NOTE: this is not actually used, but AttributeSpec requires it
#include "pxr/usd/sdf/relationshipSpec.h"

#include "pxr/base/ts/spline.h"
#include "pxr/base/tf/preprocessorUtilsLite.h"

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


// ------------------------------------------------------------------------- //
// UsdAttribute 
// ------------------------------------------------------------------------- //

SdfVariability
UsdAttribute::GetVariability() const
{
    return _GetStage()->_GetVariability(*this);
}

bool 
UsdAttribute::SetVariability(SdfVariability variability) const
{
    return SetMetadata(SdfFieldKeys->Variability, variability);
}

SdfValueTypeName
UsdAttribute::GetTypeName() const
{
    TfToken typeName;
    GetMetadata(SdfFieldKeys->TypeName, &typeName);
    return SdfSchema::GetInstance().FindType(typeName);
}

TfToken
UsdAttribute::GetRoleName() const
{
    return GetTypeName().GetRole();
}

bool 
UsdAttribute::SetTypeName(const SdfValueTypeName& typeName) const
{
    return SetMetadata(SdfFieldKeys->TypeName, typeName.GetAsToken());
}

void 
UsdAttribute::Block() const
{
    Clear();
    Set(VtValue(SdfValueBlock()), UsdTimeCode::Default()); 
}

bool
UsdAttribute::GetTimeSamples(std::vector<double>* times) const 
{
    return _GetStage()->_GetTimeSamplesInInterval(*this, 
        GfInterval::GetFullInterval(), times);
}

size_t
UsdAttribute::GetNumTimeSamples() const 
{
    return _GetStage()->_GetNumTimeSamples(*this);
}

bool 
UsdAttribute::GetBracketingTimeSamples(double desiredTime, 
                                       double* lower, 
                                       double* upper, 
                                       bool* hasTimeSamples) const
{
    return _GetStage()->_GetBracketingTimeSamples(
        *this, desiredTime, /*requireAuthored*/ false,
        lower, upper, hasTimeSamples);
}

bool
UsdAttribute::GetTimeSamplesInInterval(const GfInterval& interval,
                                       std::vector<double>* times) const
{
    return _GetStage()->_GetTimeSamplesInInterval(*this, interval, times);  
}

/* static */
bool 
UsdAttribute::GetUnionedTimeSamples(
    const std::vector<UsdAttribute> &attrs, 
    std::vector<double> *times)
{
    return GetUnionedTimeSamplesInInterval(attrs, 
        GfInterval::GetFullInterval(), times);
}

/* static */
bool 
UsdAttribute::GetUnionedTimeSamplesInInterval(
    const std::vector<UsdAttribute> &attrs, 
    const GfInterval &interval,
    std::vector<double> *times)
{
    // Clear the vector first before proceeding to accumulate sample times.
    times->clear();

    if (attrs.empty()) {
        return true;
    }

    bool success = true;

    // Vector that holds the per-attribute sample times.
    std::vector<double> attrSampleTimes;

    // Temporary vector used to hold the union of two time-sample vectors.
    std::vector<double> tempUnionSampleTimes;

    for (const auto &attr : attrs) {
        if (!attr) {
            success = false;
            continue;
        }

        // This will work even if the attributes belong to different 
        // USD stages.
        success = attr.GetStage()->_GetTimeSamplesInInterval(attr, interval,
                &attrSampleTimes) && success;

        // Merge attrSamplesTimes into the times vector.
        Usd_MergeTimeSamples(times, attrSampleTimes, &tempUnionSampleTimes);
    }
    
    return success;
}

bool 
UsdAttribute::HasAuthoredValueOpinion() const
{
    UsdResolveInfo resolveInfo;
    _GetStage()->_GetResolveInfo(*this, &resolveInfo);
    return resolveInfo.HasAuthoredValueOpinion();
}

bool 
UsdAttribute::HasAuthoredValue() const
{
    UsdResolveInfo resolveInfo;
    _GetStage()->_GetResolveInfo(*this, &resolveInfo);
    return resolveInfo.HasAuthoredValue();
}

bool 
UsdAttribute::HasValue() const
{
    UsdResolveInfo resolveInfo;
    _GetStage()->_GetResolveInfo(*this, &resolveInfo);
    return resolveInfo._source != UsdResolveInfoSourceNone;
}

bool
UsdAttribute::HasFallbackValue() const
{
    UsdPrimDefinition::Attribute attrDef =
        _GetStage()->_GetSchemaAttribute(*this);
    return attrDef && attrDef.GetFallbackValue<VtValue>(nullptr);
}

bool 
UsdAttribute::ValueMightBeTimeVarying() const
{
    return _GetStage()->_ValueMightBeTimeVarying(*this);
}

template <typename T>
bool 
UsdAttribute::_Get(T* value, UsdTimeCode time) const 
{
    return _GetStage()->_GetValue(time, *this, value);
}

bool 
UsdAttribute::Get(VtValue* value, UsdTimeCode time) const 
{
    return _GetStage()->_GetValue(time, *this, value);
}

UsdResolveInfo
UsdAttribute::GetResolveInfo(UsdTimeCode time) const
{
    UsdResolveInfo resolveInfo;
    _GetStage()->_GetResolveInfo(*this, &resolveInfo, &time);
    return resolveInfo;
}

UsdResolveInfo
UsdAttribute::GetResolveInfo() const
{
    UsdResolveInfo resolveInfo;
    _GetStage()->_GetResolveInfo(*this, &resolveInfo, nullptr);
    return resolveInfo;
}

template <typename T>
bool 
UsdAttribute::_Set(const T& value, UsdTimeCode time) const
{
    return _GetStage()->_SetValue(time, *this, value);
}

bool 
UsdAttribute::Set(const char* value, UsdTimeCode time) const {
    std::string strVal(value);
    return _Set(strVal, time);
}

bool 
UsdAttribute::Set(const VtValue& value, UsdTimeCode time) const 
{ 
    return _GetStage()->_SetValue(time, *this, value);
}

bool
UsdAttribute::HasSpline() const
{
    return _GetStage()->_HasMetadata(
        *this,                 // find a field in our attribute spec
        SdfFieldKeys->Spline,  // find the Spline field
        TfToken(),             // not a dict field, so no dict key
        false);                // want authored opinions only
}

TsSpline
UsdAttribute::GetSpline() const
{
    TsSpline spline;
    _GetStage()->_GetMetadata(
        *this,                 // read a field in our attribute spec
        SdfFieldKeys->Spline,  // read the Spline field
        TfToken(),             // not a dict field, so no dict key
        false,                 // want authored opinions only
        &spline);              // read into this variable
    return spline;
}

bool
UsdAttribute::SetSpline(const TsSpline &spline)
{
    return _GetStage()->_SetMetadata(
        *this,                 // write a field in our attribute spec
        SdfFieldKeys->Spline,  // write the Spline field
        TfToken(),             // not a dict field, so no dict key
        spline);               // write this value
}

bool
UsdAttribute::Clear() const
{
    return ClearDefault() 
       && ClearMetadata(SdfFieldKeys->TimeSamples)
       && ClearMetadata(SdfFieldKeys->Spline);
}

bool
UsdAttribute::ClearAtTime(UsdTimeCode time) const
{
    return _GetStage()->_ClearValue(time, *this);
}

bool
UsdAttribute::ClearDefault() const
{
    return ClearAtTime(UsdTimeCode::Default());
}

TfToken 
UsdAttribute::GetColorSpace() const
{
    TfToken colorSpace;
    GetMetadata(SdfFieldKeys->ColorSpace, &colorSpace);
    return colorSpace;
}

void 
UsdAttribute::SetColorSpace(const TfToken &colorSpace) const
{
    SetMetadata(SdfFieldKeys->ColorSpace, colorSpace);
}

bool 
UsdAttribute::HasColorSpace() const
{
    return HasMetadata(SdfFieldKeys->ColorSpace);
}

bool 
UsdAttribute::ClearColorSpace() const
{
    return ClearMetadata(SdfFieldKeys->ColorSpace);
}

SdfAttributeSpecHandle
UsdAttribute::_CreateSpec(const SdfValueTypeName& typeName, bool custom,
                          const SdfVariability &variability) const
{
    UsdStage *stage = _GetStage();
    
    // Try to create a spec for editing either from the definition or from
    // copying existing spec info.
    TfErrorMark m;
    if (SdfAttributeSpecHandle attrSpec =
        stage->_CreateAttributeSpecForEditing(*this))
        return attrSpec;

    // If creating the spec on the stage failed without issuing an error, that
    // means there was no existing authored scene description to go on (i.e. no
    // builtin info from prim type, and no existing authored spec).  Stamp a
    // spec with the provided default values.
    if (m.IsClean()) {
        SdfChangeBlock block;
        return SdfAttributeSpec::New(
            stage->_CreatePrimSpecForEditing(GetPrim()),
            _PropName(), typeName, variability, custom);
    }
    return TfNullPtr;
}

SdfAttributeSpecHandle
UsdAttribute::_CreateSpec() const
{
    return _GetStage()->_CreateAttributeSpecForEditing(*this);
}

bool
UsdAttribute::_Create(const SdfValueTypeName& typeName, bool custom,
                      const SdfVariability &variability) const
{
    return _CreateSpec(typeName, custom, variability);
}


ARCH_PRAGMA_PUSH
ARCH_PRAGMA_INSTANTIATION_AFTER_SPECIALIZATION

// Explicitly instantiate templated getters and setters for all Sdf value
// types.
#define _INSTANTIATE_GET(unused, elem)                                  \
    template USD_API bool UsdAttribute::_Get(                           \
        SDF_VALUE_CPP_TYPE(elem)*, UsdTimeCode) const;                  \
    template USD_API bool UsdAttribute::_Get(                           \
        SDF_VALUE_CPP_ARRAY_TYPE(elem)*, UsdTimeCode) const;            \
    template USD_API bool UsdAttribute::_Set(                           \
        const SDF_VALUE_CPP_TYPE(elem)&, UsdTimeCode) const;            \
    template USD_API bool UsdAttribute::_Set(                           \
        const SDF_VALUE_CPP_ARRAY_TYPE(elem)&, UsdTimeCode) const;

TF_PP_SEQ_FOR_EACH(_INSTANTIATE_GET, ~, SDF_VALUE_TYPES)
#undef _INSTANTIATE_GET

// In addition to the Sdf value types, _Set can also be called with an 
// SdfValueBlock.
template USD_API bool UsdAttribute::_Set(
    const SdfValueBlock &, UsdTimeCode) const;

ARCH_PRAGMA_POP

SdfPath
UsdAttribute::_GetPathForAuthoring(const SdfPath &path,
                                   std::string* whyNot) const
{
    SdfPath result;
    if (!path.IsEmpty()) {
        SdfPath absPath =
            path.MakeAbsolutePath(GetPath().GetAbsoluteRootOrPrimPath());
        if (Usd_InstanceCache::IsPathInPrototype(absPath)) {
            if (whyNot) { 
                *whyNot = "Cannot refer to a prototype or an object within a "
                    "prototype.";
            }
            return result;
        }
    }

    // If this is a relative target path, we have to map both the anchor
    // and target path and then re-relativize them.
    const UsdEditTarget &editTarget = _GetStage()->GetEditTarget();
    if (path.IsAbsolutePath()) {
        result = editTarget.MapToSpecPath(path).StripAllVariantSelections();
    } else {
        const SdfPath anchorPrim = GetPath().GetPrimPath();
        const SdfPath translatedAnchorPrim =
            editTarget.MapToSpecPath(anchorPrim)
            .StripAllVariantSelections();
        const SdfPath translatedPath =
            editTarget.MapToSpecPath(path.MakeAbsolutePath(anchorPrim))
            .StripAllVariantSelections();
        result = translatedPath.MakeRelativePath(translatedAnchorPrim);
    }
    
    if (result.IsEmpty()) {
        if (whyNot) {
            *whyNot = TfStringPrintf(
                "Cannot map <%s> to layer @%s@ via stage's EditTarget",
                path.GetText(), _GetStage()->GetEditTarget().
                GetLayer()->GetIdentifier().c_str());
        }
    }

    return result;
}


bool
UsdAttribute::AddConnection(const SdfPath& source,
                            UsdListPosition position) const
{
    std::string errMsg;
    const SdfPath pathToAuthor = _GetPathForAuthoring(source, &errMsg);
    if (pathToAuthor.IsEmpty()) {
        TF_CODING_ERROR("Cannot append connection <%s> to attribute <%s>: %s",
                        source.GetText(), GetPath().GetText(), errMsg.c_str());
        return false;
    }

    // NOTE! Do not insert any code that modifies scene description between the
    // changeblock and the call to _CreateSpec!  Explanation: _CreateSpec calls
    // code that inspects the composition graph and then does some authoring.
    // We want that authoring to be inside the change block, but if any scene
    // description changes are made after the block is created but before we
    // call _CreateSpec, the composition structure may be invalidated.
    SdfChangeBlock block;
    SdfAttributeSpecHandle attrSpec = _CreateSpec();

    if (!attrSpec)
        return false;

    Usd_InsertListItem( attrSpec->GetConnectionPathList(), pathToAuthor,
                        position );
    return true;
}

bool
UsdAttribute::RemoveConnection(const SdfPath& source) const
{
    std::string errMsg;
    const SdfPath pathToAuthor = _GetPathForAuthoring(source, &errMsg);
    if (pathToAuthor.IsEmpty()) {
        TF_CODING_ERROR("Cannot remove connection <%s> from attribute <%s>: %s",
                        source.GetText(), GetPath().GetText(), errMsg.c_str());
        return false;
    }

    // NOTE! Do not insert any code that modifies scene description between the
    // changeblock and the call to _CreateSpec!  Explanation: _CreateSpec calls
    // code that inspects the composition graph and then does some authoring.
    // We want that authoring to be inside the change block, but if any scene
    // description changes are made after the block is created but before we
    // call _CreateSpec, the composition structure may be invalidated.
    SdfChangeBlock block;
    SdfAttributeSpecHandle attrSpec = _CreateSpec();

    if (!attrSpec)
        return false;

    attrSpec->GetConnectionPathList().Remove(pathToAuthor);
    return true;
}

bool
UsdAttribute::SetConnections(const SdfPathVector& sources) const
{
    SdfPathVector mappedPaths;
    mappedPaths.reserve(sources.size());
    for (const SdfPath &path: sources) {
        std::string errMsg;
        mappedPaths.push_back(_GetPathForAuthoring(path, &errMsg));
        if (mappedPaths.back().IsEmpty()) {
            TF_CODING_ERROR("Cannot set connection <%s> on attribute <%s>: %s",
                            path.GetText(), GetPath().GetText(), 
                            errMsg.c_str());
            return false;
        }
    }

    // NOTE! Do not insert any code that modifies scene description between the
    // changeblock and the call to _CreateSpec!  Explanation: _CreateSpec calls
    // code that inspects the composition graph and then does some authoring.
    // We want that authoring to be inside the change block, but if any scene
    // description changes are made after the block is created but before we
    // call _CreateSpec, the composition structure may be invalidated.
    SdfChangeBlock block;
    SdfAttributeSpecHandle attrSpec = _CreateSpec();

    if (!attrSpec)
        return false;

    attrSpec->GetConnectionPathList().ClearEditsAndMakeExplicit();
    attrSpec->GetConnectionPathList().GetExplicitItems() = mappedPaths;

    return true;
}

bool
UsdAttribute::ClearConnections() const
{
    // NOTE! Do not insert any code that modifies scene description between the
    // changeblock and the call to _CreateSpec!  Explanation: _CreateSpec calls
    // code that inspects the composition graph and then does some authoring.
    // We want that authoring to be inside the change block, but if any scene
    // description changes are made after the block is created but before we
    // call _CreateSpec, the composition structure may be invalidated.
    SdfChangeBlock block;
    SdfAttributeSpecHandle attrSpec = _CreateSpec();

    if (!attrSpec)
        return false;

    attrSpec->GetConnectionPathList().ClearEdits();
    return true;
}

bool
UsdAttribute::GetConnections(SdfPathVector *sources) const
{
    TRACE_FUNCTION();
    return _GetTargets(SdfSpecTypeAttribute, sources);
}

bool
UsdAttribute::HasAuthoredConnections() const
{
    return HasAuthoredMetadata(SdfFieldKeys->ConnectionPaths);
}

PXR_NAMESPACE_CLOSE_SCOPE

