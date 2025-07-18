//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/usdImaging/usdImaging/dataSourcePrimvars.h"
#include "pxr/usdImaging/usdImaging/dataSourceRelationship.h"

#include "pxr/usdImaging/usdImaging/primvarUtils.h"

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/primvarsSchema.h"
#include "pxr/imaging/hd/primvarSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/tf/denseHashMap.h"

PXR_NAMESPACE_OPEN_SCOPE

static inline bool
_IsIndexed(const UsdAttributeQuery& indicesQuery)
{
    return indicesQuery.IsValid() && indicesQuery.HasValue();
}

static
TfToken
_GetInterpolation(const UsdAttribute &attr)
{
    // A reimplementation of UsdGeomPrimvar::GetInterpolation(),
    // but with "vertex" as the default instead of "constant"...
    TfToken interpolation;
    if (attr.GetMetadata(UsdGeomTokens->interpolation, &interpolation)) {
        return UsdImagingUsdToHdInterpolationToken(interpolation);
    }

    return HdPrimvarSchemaTokens->vertex;
}

// Reject primvars:points since we always want to get the value from
// the points attribute.
// Similar for velocities and accelerations.
static
bool
_RejectPrimvar(const TfToken &name)
{
    if (name == UsdGeomTokens->points) {
        return true;
    }
    if (name == UsdGeomTokens->velocities) {
        return true;
    }
    if (name == UsdGeomTokens->accelerations) {
        return true;
    }

    return false;
}

UsdImagingDataSourcePrimvars::UsdImagingDataSourcePrimvars(
    const SdfPath &sceneIndexPath,
    UsdPrim const &usdPrim,
    const UsdImagingDataSourceStageGlobals & stageGlobals)
: _sceneIndexPath(sceneIndexPath)
, _usdPrim(usdPrim)
, _stageGlobals(stageGlobals)
{
}


/*static*/
TfToken
UsdImagingDataSourcePrimvars::_GetPrefixedName(const TfToken &name)
{
    return TfToken(("primvars:" + name.GetString()).c_str());
}

TfTokenVector 
UsdImagingDataSourcePrimvars::GetNames()
{
    TRACE_FUNCTION();

    TfTokenVector result;

    // Enumerate primvar names using UsdGeomPrimvarsAPI.
    // This API filters out supporting attributes such as
    // "primvars:indexedPrimvar:indices".
    const std::vector<UsdGeomPrimvar> primvars =
         UsdGeomPrimvarsAPI(_usdPrim).GetAuthoredPrimvars();
    result.reserve(primvars.size());
    for (const UsdGeomPrimvar& primvar : primvars) {
        const TfToken name = primvar.GetPrimvarName();
         if (!_RejectPrimvar(name)) {
            result.push_back(name);
        }
    }

    return result;
}

HdIntDataSourceHandle
_ElementSizeToDataSource(const int n)
{
    if (n == 1) {
        // elementSize = 1 is default.
        // Don't occur the cost of instantiating the data source and
        // passing it down when most clients ignore elementSize anyway.
        return nullptr;
    }
    return HdRetainedTypedSampledDataSource<int>::New(n);
}

HdDataSourceBaseHandle
UsdImagingDataSourcePrimvars::Get(const TfToken & name)
{
    TRACE_FUNCTION();

    if (_RejectPrimvar(name)) {
        return nullptr;
    }

    const TfToken prefixedName = _GetPrefixedName(name);

    if (UsdAttribute attr = _usdPrim.GetAttribute(prefixedName)) {
        UsdGeomPrimvar usdPrimvar(attr);

        UsdAttributeQuery valueQuery(attr);
        if (!valueQuery.HasAuthoredValue()) {
            return nullptr;
        }

        return UsdImagingDataSourcePrimvar::New(
                _sceneIndexPath, name, _stageGlobals,
                /* value = */ std::move(valueQuery),
                /* indices = */ UsdAttributeQuery(usdPrimvar.GetIndicesAttr()),
                HdPrimvarSchema::BuildInterpolationDataSource(
                    UsdImagingUsdToHdInterpolationToken(
                        usdPrimvar.GetInterpolation())),
                HdPrimvarSchema::BuildRoleDataSource(
                    UsdImagingUsdToHdRole(attr.GetRoleName())),
                _ElementSizeToDataSource(usdPrimvar.GetElementSize()));
                
    }

    if (UsdRelationship rel = _usdPrim.GetRelationship(prefixedName)) {
        return HdPrimvarSchema::Builder()
            .SetPrimvarValue(UsdImagingDataSourceRelationship::New(
                rel, _stageGlobals))
            .SetInterpolation(HdPrimvarSchema::BuildInterpolationDataSource(
                HdPrimvarSchemaTokens->constant))
            .Build();
    }

    return nullptr;
}

// ----------------------------------------------------------------------------

UsdImagingDataSourceCustomPrimvars::UsdImagingDataSourceCustomPrimvars(
        const SdfPath &sceneIndexPath,
        UsdPrim const &usdPrim,
        const Mappings &mappings,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
: _sceneIndexPath(sceneIndexPath)
, _usdPrim(usdPrim)
, _stageGlobals(stageGlobals)
, _mappings(mappings)
{
}

TfTokenVector
UsdImagingDataSourceCustomPrimvars::GetNames()
{
    TRACE_FUNCTION();
    
    TfTokenVector result;
    result.reserve(_mappings.size());

    for (const auto &mapping : _mappings) {
        result.push_back(mapping.primvarName);
    }

    return result;
}

HdDataSourceBaseHandle
UsdImagingDataSourceCustomPrimvars::Get(const TfToken &name)
{
    TRACE_FUNCTION();
    
    for (const Mapping &mapping : _mappings) {
        if (mapping.primvarName != name) {
            continue;
        }

        const UsdAttribute attr = _usdPrim.GetAttribute(mapping.usdAttrName);
        UsdAttributeQuery valueQuery(attr);

        if (!valueQuery.HasAuthoredValue()) {
            return nullptr;
        }

        return UsdImagingDataSourcePrimvar::New(
            _sceneIndexPath, name, _stageGlobals,
            /* value = */ std::move(valueQuery),
            /* indices = */ UsdAttributeQuery(),
            HdPrimvarSchema::BuildInterpolationDataSource(
                mapping.interpolation.IsEmpty()
                ? _GetInterpolation(attr)
                : mapping.interpolation),
            HdPrimvarSchema::BuildRoleDataSource(
                UsdImagingUsdToHdRole(attr.GetRoleName())),
            /* elementSize = */ nullptr);
    }

    return nullptr;
}



/*static*/
HdDataSourceLocatorSet
UsdImagingDataSourceCustomPrimvars::Invalidate(
        const TfTokenVector &properties,
        const Mappings &mappings)
{
    HdDataSourceLocatorSet result;

    // TODO, decide how to handle this based on the size?
    TfDenseHashMap<TfToken, TfToken, TfHash> nameMappings;
    for (const UsdImagingDataSourceCustomPrimvars::Mapping &m : mappings) {
        nameMappings[m.usdAttrName] = m.primvarName;
    }

    for (const TfToken &propertyName : properties) {
        const auto it = nameMappings.find(propertyName);
        if (it != nameMappings.end()) {
            result.insert(HdPrimvarsSchema::GetDefaultLocator().Append(
                it->second));
        }
    }

    return result;
}

// ----------------------------------------------------------------------------


UsdImagingDataSourcePrimvar::UsdImagingDataSourcePrimvar(
        const SdfPath &sceneIndexPath,
        const TfToken &name,
        const UsdImagingDataSourceStageGlobals &stageGlobals,
        UsdAttributeQuery valueQuery,
        UsdAttributeQuery indicesQuery,
        HdTokenDataSourceHandle interpolation,
        HdTokenDataSourceHandle role,
        HdIntDataSourceHandle elementSize)
: _stageGlobals(stageGlobals)
, _valueQuery(valueQuery)
, _indicesQuery(indicesQuery)
, _interpolation(std::move(interpolation))
, _role(std::move(role))
, _elementSize(std::move(elementSize))
{
    const bool indexed = _IsIndexed(_indicesQuery);
    if (indexed) {
        if (_valueQuery.ValueMightBeTimeVarying()) {
            _stageGlobals.FlagAsTimeVarying(sceneIndexPath,
                    HdDataSourceLocator(
                        HdPrimvarsSchemaTokens->primvars,
                        name,
                        HdPrimvarSchemaTokens->indexedPrimvarValue));
        }
        if (_indicesQuery.ValueMightBeTimeVarying()) {
            _stageGlobals.FlagAsTimeVarying(sceneIndexPath,
                    HdDataSourceLocator(
                        HdPrimvarsSchemaTokens->primvars,
                        name,
                        HdPrimvarSchemaTokens->indices));
        }
    } else {
        if (_valueQuery.ValueMightBeTimeVarying()) {
            _stageGlobals.FlagAsTimeVarying(sceneIndexPath,
                    HdDataSourceLocator(
                        HdPrimvarsSchemaTokens->primvars,
                        name,
                        HdPrimvarSchemaTokens->primvarValue));
        }
    }
}

TfTokenVector
UsdImagingDataSourcePrimvar::GetNames()
{
    const bool indexed = _IsIndexed(_indicesQuery);

    TfTokenVector result = {
        HdPrimvarSchemaTokens->interpolation,
        HdPrimvarSchemaTokens->role,
    };
    
    if (indexed) {
        result.push_back(HdPrimvarSchemaTokens->indexedPrimvarValue);
        result.push_back(HdPrimvarSchemaTokens->indices);
    } else {
        result.push_back(HdPrimvarSchemaTokens->primvarValue);
    }

    if (_elementSize) {
        result.push_back(HdPrimvarSchemaTokens->elementSize);
    }

    return result;
}

HdDataSourceBaseHandle
UsdImagingDataSourcePrimvar::Get(const TfToken & name)
{
    TRACE_FUNCTION();

    const bool indexed = _IsIndexed(_indicesQuery);

    if (indexed) {
        if (name == HdPrimvarSchemaTokens->indexedPrimvarValue) {
            return UsdImagingDataSourceAttributeNew(
                _valueQuery, _stageGlobals);
        } else if (name == HdPrimvarSchemaTokens->indices) {
            return UsdImagingDataSourceAttributeNew(
                    _indicesQuery, _stageGlobals);
        }
    } else {
        if (name == HdPrimvarSchemaTokens->primvarValue) {
            return UsdImagingDataSourceAttributeNew(
                _valueQuery, _stageGlobals);
        }
    }

    if (name == HdPrimvarSchemaTokens->interpolation) {
        return _interpolation;
    }
    if (name == HdPrimvarSchemaTokens->role) {
        return _role;
    }
    if (name == HdPrimvarSchemaTokens->elementSize) {
        return _elementSize;
    }
    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
