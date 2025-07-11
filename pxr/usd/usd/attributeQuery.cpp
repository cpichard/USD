//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usd/attributeQuery.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/valueUtils.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/tf/preprocessorUtilsLite.h"
#include "pxr/base/ts/spline.h"

PXR_NAMESPACE_OPEN_SCOPE


UsdAttributeQuery::UsdAttributeQuery(
    const UsdAttribute& attr) : 
    _attr(attr)
{
    _Initialize();
}

UsdAttributeQuery::UsdAttributeQuery(
    const UsdPrim& prim, const TfToken& attrName)
    : UsdAttributeQuery(prim.GetAttribute(attrName))
{
}

UsdAttributeQuery::UsdAttributeQuery(
    const UsdAttribute &attr, 
    const UsdResolveTarget &resolveTarget) : 
    _attr(attr)
{
    _Initialize(resolveTarget);
}

std::vector<UsdAttributeQuery>
UsdAttributeQuery::CreateQueries(
    const UsdPrim& prim, const TfTokenVector& attrNames)
{
    std::vector<UsdAttributeQuery> rval;
    rval.reserve(attrNames.size());
    for (const auto& attrName : attrNames) {
        rval.push_back(UsdAttributeQuery(prim, attrName));
    }

    return rval;
}

UsdAttributeQuery::UsdAttributeQuery()
{
}

UsdAttributeQuery::UsdAttributeQuery(const UsdAttributeQuery &other) :
    _attr(other._attr),
    _resolveInfo(other._resolveInfo)
{
    if (other._resolveTarget) {
        _resolveTarget = std::make_unique<UsdResolveTarget>(
            *(other._resolveTarget));
    }
}

UsdAttributeQuery &
UsdAttributeQuery::operator=(const UsdAttributeQuery &other)
{
    _attr = other._attr;
    _resolveInfo = other._resolveInfo;
    if (other._resolveTarget) {
        _resolveTarget = std::make_unique<UsdResolveTarget>(
            *(other._resolveTarget));
    }
    return *this;
}

void
UsdAttributeQuery::_Initialize()
{
    TRACE_FUNCTION();

    if (_attr) {
        const UsdStage* stage = _attr._GetStage();
        stage->_GetResolveInfo(_attr, &_resolveInfo);
    }
}

void 
UsdAttributeQuery::_Initialize(
    const UsdResolveTarget &resolveTarget)
{
    TRACE_FUNCTION();

    if (resolveTarget.IsNull()) {
        _Initialize();
        return;
    }

    if (!_attr) {
        return;
    }

    // Validate that the resolve target is for this attribute's prim path.
    if (_attr.GetPrimPath() != resolveTarget.GetPrimIndex()->GetPath()) {
        TF_CODING_ERROR("Invalid resolve target for attribute '%s'. The "
            "given resolve target is only valid for attributes on the prim "
            "'%s'.",
            _attr.GetPrimPath().GetText(), 
            resolveTarget.GetPrimIndex()->GetPath().GetText());
        return;
    }

    const UsdStage* stage = _attr._GetStage();
    stage->_GetResolveInfoWithResolveTarget(_attr, resolveTarget, &_resolveInfo);

    _resolveTarget = std::make_unique<UsdResolveTarget>(resolveTarget);
}

const UsdAttribute& 
UsdAttributeQuery::GetAttribute() const
{
    return _attr;
}

template <typename T>
USD_API
bool 
UsdAttributeQuery::_Get(T* value, UsdTimeCode time) const
{
    // If the requested time is default but the resolved value source is time
    // varying, then the stored resolve info won't give us the correct value 
    // for default time. In this case we have to get the resolve info at default
    // time and query the value from that.
    if (time.IsDefault() && 
            _resolveInfo.ValueSourceMightBeTimeVarying()) {

        static const UsdTimeCode defaultTime = UsdTimeCode::Default();
        UsdResolveInfo defaultResolveInfo;
        if (_resolveTarget && TF_VERIFY(!_resolveTarget->IsNull())) {
            _attr._GetStage()->_GetResolveInfoWithResolveTarget(
                _attr, *_resolveTarget, &defaultResolveInfo, &defaultTime);
        } else {
            _attr._GetStage()->_GetResolveInfo(
                _attr, &defaultResolveInfo, &defaultTime);
        }
        return _attr._GetStage()->_GetValueFromResolveInfo(
            defaultResolveInfo, defaultTime, _attr, value);
    }

    return _attr._GetStage()->_GetValueFromResolveInfo(
        _resolveInfo, time, _attr, value);
}

bool 
UsdAttributeQuery::Get(VtValue* value, UsdTimeCode time) const
{
    return _Get(value, time);
}

bool 
UsdAttributeQuery::GetTimeSamples(std::vector<double>* times) const
{
    return _attr._GetStage()->_GetTimeSamplesInIntervalFromResolveInfo(
        _resolveInfo, _attr, GfInterval::GetFullInterval(), times);
}

TsSpline
UsdAttributeQuery::GetSpline() const
{
    if (_resolveInfo.GetSource() != UsdResolveInfoSourceSpline) {
        return TsSpline();
    }

    if (!TF_VERIFY(_resolveInfo._spline, 
        "Spline should be valid when source is Spline")) {
        return TsSpline();
    }

    if (_resolveInfo._layerToStageOffset.IsIdentity()) {
        return _resolveInfo._spline.value();
    }

    TsSpline mappedSpline = _resolveInfo._spline.value();
    Usd_ApplyLayerOffsetToValue(
        &mappedSpline, _resolveInfo._layerToStageOffset);
    return mappedSpline;
}

bool
UsdAttributeQuery::GetTimeSamplesInInterval(const GfInterval& interval,
                                            std::vector<double>* times) const
{
    return _attr._GetStage()->_GetTimeSamplesInIntervalFromResolveInfo(
        _resolveInfo, _attr, interval, times);
}

/* static */
bool 
UsdAttributeQuery::GetUnionedTimeSamples(
    const std::vector<UsdAttributeQuery> &attrQueries, 
    std::vector<double> *times)
{
    return GetUnionedTimeSamplesInInterval(attrQueries, 
            GfInterval::GetFullInterval(), times);
}

/* static */
bool 
UsdAttributeQuery::GetUnionedTimeSamplesInInterval(
    const std::vector<UsdAttributeQuery> &attrQueries, 
    const GfInterval &interval,
    std::vector<double> *times)
{
    // Clear the vector first before proceeding to accumulate sample times.
    times->clear();

    if (attrQueries.empty()) {
        return true;
    }

    bool success = true;

    // Vector that holds the per-attribute sample times.
    std::vector<double> attrSampleTimes;

    // Temporary vector used to hold the union of two time-sample vectors.
    std::vector<double> tempUnionSampleTimes;

    for (const auto &attrQuery : attrQueries) {
        const UsdAttribute &attr = attrQuery.GetAttribute();
        if (!attr)
            continue;

        // This will work even if the attributes belong to different 
        // USD stages.
        success = attr.GetStage()->_GetTimeSamplesInIntervalFromResolveInfo(
                attrQuery._resolveInfo, attr, interval, &attrSampleTimes) 
                && success;

        // Merge attrSamplesTimes into the times vector.
        Usd_MergeTimeSamples(times, attrSampleTimes, &tempUnionSampleTimes);
    }
    
    return success;
}

size_t
UsdAttributeQuery::GetNumTimeSamples() const
{
    return _attr._GetStage()->_GetNumTimeSamplesFromResolveInfo(
        _resolveInfo, _attr);
}

bool 
UsdAttributeQuery::GetBracketingTimeSamples(double desiredTime, 
                                            double* lower, 
                                            double* upper, 
                                            bool* hasTimeSamples) const
{
    return _attr._GetStage()->_GetBracketingTimeSamplesFromResolveInfo(
        _resolveInfo, _attr, desiredTime, /* authoredOnly */ false,
        lower, upper, hasTimeSamples);
}

bool 
UsdAttributeQuery::HasValue() const
{
    return _resolveInfo._source != UsdResolveInfoSourceNone;  
}

bool
UsdAttributeQuery::HasSpline() const
{
    return _resolveInfo._source == UsdResolveInfoSourceSpline;
}

bool 
UsdAttributeQuery::HasAuthoredValueOpinion() const
{
    return _resolveInfo.HasAuthoredValueOpinion();
}

bool 
UsdAttributeQuery::HasAuthoredValue() const
{
    return _resolveInfo.HasAuthoredValue();
}

bool
UsdAttributeQuery::HasFallbackValue() const
{
    return _attr.HasFallbackValue();
}

bool 
UsdAttributeQuery::ValueMightBeTimeVarying() const
{
    return _attr._GetStage()->_ValueMightBeTimeVaryingFromResolveInfo(
        _resolveInfo, _attr);
}

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_INSTANTIATION_AFTER_SPECIALIZATION

// Explicitly instantiate templated getters for all Sdf value
// types.
#define _INSTANTIATE_GET(unused, elem)                                  \
    template USD_API bool UsdAttributeQuery::_Get(                      \
        SDF_VALUE_CPP_TYPE(elem)*, UsdTimeCode) const;                  \
    template USD_API bool UsdAttributeQuery::_Get(                      \
        SDF_VALUE_CPP_ARRAY_TYPE(elem)*, UsdTimeCode) const;

TF_PP_SEQ_FOR_EACH(_INSTANTIATE_GET, ~, SDF_VALUE_TYPES)
#undef _INSTANTIATE_GET

ARCH_PRAGMA_POP

PXR_NAMESPACE_CLOSE_SCOPE

