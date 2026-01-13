//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdSkelImaging/dataSourceResolvedExtComputationPrim.h"

#include "pxr/usdImaging/usdSkelImaging/bindingSchema.h"
#include "pxr/usdImaging/usdSkelImaging/blendShapeData.h"
#include "pxr/usdImaging/usdSkelImaging/extComputations.h"
#include "pxr/usdImaging/usdSkelImaging/dataSourceResolvedPointsBasedPrim.h"
#include "pxr/usdImaging/usdSkelImaging/jointInfluencesData.h"
#include "pxr/usdImaging/usdSkelImaging/tokens.h"

#include "pxr/usd/usdSkel/tokens.h"

#include "pxr/imaging/hd/extComputationInputComputationSchema.h"
#include "pxr/imaging/hd/extComputationPrimvarsSchema.h"
#include "pxr/imaging/hd/extComputationOutputSchema.h"
#include "pxr/imaging/hd/extComputationSchema.h"
#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/trace/trace.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Data source for locator extComputation:inputValues on
// skinningInputAggregatorComputation prim.
class _ExtAggregatorComputationInputValuesDataSource
      : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(
        _ExtAggregatorComputationInputValuesDataSource);

    TfTokenVector GetNames() override
    {
        // TODO
        // We might wanna consider reusing HdSkinningInputTokens here.
        return UsdSkelImagingExtAggregatorComputationInputNameTokens->allTokens;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override {
        TRACE_FUNCTION();

        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->restPoints) {
            return _resolvedPrimSource->GetPoints();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->geomBindXform) {
            return _resolvedPrimSource->GetGeomBindTransform();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->hasConstantInfluences) {
            return _resolvedPrimSource->GetHasConstantInfluences();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->numInfluencesPerComponent) {
            return _resolvedPrimSource->GetNumInfluencesPerComponent();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->influences) {
            return _resolvedPrimSource->GetInfluences();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->blendShapeOffsets) {
            return _resolvedPrimSource->GetBlendShapeOffsets();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->blendShapeOffsetRanges) {
            return _resolvedPrimSource->GetBlendShapeOffsetRanges();
        }
        if (name == UsdSkelImagingExtAggregatorComputationInputNameTokens
                                ->numBlendShapeOffsetRanges) {
            return _resolvedPrimSource->GetNumBlendShapeOffsetRanges();
        }

        return nullptr;
    }

private:
    _ExtAggregatorComputationInputValuesDataSource(
        UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle resolvedPrimSource)
     : _resolvedPrimSource(std::move(resolvedPrimSource))
    {
    }

    UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle const _resolvedPrimSource;
};

TfTokenVector
_ExtComputationInputNamesForClassicLinear()
{
    TfTokenVector result;
    for (const TfToken &name :
             UsdSkelImagingExtComputationInputNameTokens->allTokens) {
        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningScaleXforms) {
            continue;
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                        ->skinningDualQuats) {
            continue;
        }
        result.push_back(name);
    }
    return result;
}

class _ExtComputationInputValuesDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_ExtComputationInputValuesDataSource);

    TfTokenVector GetNames() override {
        // TODO
        // We might wanna consider reusing HdSkinningInputTokens here.
        if (_IsDualQuatSkinning()) {
            return UsdSkelImagingExtComputationInputNameTokens->allTokens;
        } else {
            static const TfTokenVector result =
                _ExtComputationInputNamesForClassicLinear();
            return result;
        }
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        TRACE_FUNCTION();

        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->commonSpaceToPrimLocal) {
            // Typed sampled data source holding inverse of xform:matrix from
            // prim from input scene.
            return _resolvedPrimSource->GetCommonSpaceToPrimLocal();
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->blendShapeWeights) {
            return _resolvedPrimSource->GetBlendShapeWeights();
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->skinningXforms) {
            return _resolvedPrimSource->GetSkinningTransforms();
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->skinningScaleXforms) {
            return _IsDualQuatSkinning() ? 
                _resolvedPrimSource->GetSkinningScaleTransforms() : nullptr;
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->skinningDualQuats) {
            return _IsDualQuatSkinning() ? 
                _resolvedPrimSource->GetSkinningDualQuats() : nullptr;
        }
        if (name == UsdSkelImagingExtComputationInputNameTokens
                                ->skelLocalToCommonSpace) {
            return _GetResolvedSkeletonSchema().GetSkelLocalToCommonSpace();
        }

        return nullptr;
    }

private:
    _ExtComputationInputValuesDataSource(
        UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle resolvedPrimSource)
     : _resolvedPrimSource(std::move(resolvedPrimSource))
    {
    }

    const UsdSkelImagingResolvedSkeletonSchema &_GetResolvedSkeletonSchema()
    {
        return _resolvedPrimSource->GetResolvedSkeletonSchema();
    }

    bool _IsDualQuatSkinning() const
    {
        return _resolvedPrimSource->GetSkinningMethod() == 
            UsdSkelTokens->dualQuaternion;
    }

    UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle const _resolvedPrimSource;
};

// Data source for locator extComputations:dispatchCount and
// extComputations:elementCount on skinningComputation prim.

class _NumPointsDataSource : public HdSizetDataSource
{
public:
    HD_DECLARE_DATASOURCE(_NumPointsDataSource);

    VtValue GetValue(const HdSampledDataSource::Time shutterOffset) override {
        return VtValue(GetTypedValue(shutterOffset));
    }

    size_t GetTypedValue(const HdSampledDataSource::Time shutterOffset) override {
        TRACE_FUNCTION();

        HdSampledDataSourceHandle const ds = _GetPoints();
        if (!ds) {
            return 0;
        }
        return ds->GetValue(shutterOffset).GetArraySize();
    }

    bool GetContributingSampleTimesForInterval(
        const HdSampledDataSource::Time startTime,
        const HdSampledDataSource::Time endTime,
        std::vector<float> * const outSampleTimes) override
    {
        TRACE_FUNCTION();

        HdSampledDataSourceHandle const ds = _GetPoints();
        if (!ds) {
            return false;
        }
        return ds->GetContributingSampleTimesForInterval(
            startTime, endTime, outSampleTimes);
    }

private:
    _NumPointsDataSource(const HdPrimvarsSchema &primvars)
     : _primvars(primvars)
    {
    }

    HdSampledDataSourceHandle _GetPoints() {
        return _primvars.GetPrimvar(HdPrimvarsSchemaTokens->points)
                                .GetPrimvarValue();
    }

    const HdPrimvarsSchema _primvars;
};

// Prim data source skinningInputAggregatorComputation prim.
HdContainerDataSourceHandle
_ExtAggregatorComputationPrimDataSource(
    UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle resolvedPrimSource)
{
    TRACE_FUNCTION();

    return
        HdRetainedContainerDataSource::New(
            HdExtComputationSchema::GetSchemaToken(),
            HdExtComputationSchema::Builder()
                .SetInputValues(
                    _ExtAggregatorComputationInputValuesDataSource::New(
                        std::move(resolvedPrimSource)))
                .Build());
}

// Data source for locator extComputation:inputComputations on
// skinningComputation prim.
HdContainerDataSourceHandle
_ExtComputationInputComputations(const SdfPath &primPath)
{
    TRACE_FUNCTION();

    static const TfTokenVector names =
        UsdSkelImagingExtAggregatorComputationInputNameTokens->allTokens;

    HdPathDataSourceHandle const pathSrc =
        HdRetainedTypedSampledDataSource<SdfPath>::New(
            primPath.AppendChild(
                UsdSkelImagingExtComputationNameTokens->aggregatorComputation));

    std::vector<HdDataSourceBaseHandle> values;
    values.reserve(names.size());

    for (const TfToken &name : names) {
        values.push_back(
            HdExtComputationInputComputationSchema::Builder()
                .SetSourceComputation(pathSrc)
                .SetSourceComputationOutputName(
                    HdRetainedTypedSampledDataSource<TfToken>::New(
                        name))
                .Build());
    }

    return
        HdExtComputationInputComputationContainerSchema::BuildRetained(
            names.size(), names.data(), values.data());
}

// Data source for locator extComputation:outputs on
// skinningComputation prim.
HdContainerDataSourceHandle
_ExtComputationOutputs()
{
    static const TfToken names[] = {
        UsdSkelImagingExtComputationOutputNameTokens->skinnedPoints
    };
    HdDataSourceBaseHandle const values[] = {
        HdExtComputationOutputSchema::Builder()
            .SetValueType(
                HdRetainedTypedSampledDataSource<HdTupleType>::New(
                    HdTupleType{HdTypeFloatVec3, 1}))
            .Build()
    };

    static_assert(std::size(names) == std::size(values));
    return
        HdExtComputationOutputContainerSchema::BuildRetained(
            std::size(names), names, values);
}

// Prim data source skinningComputation prim.
HdContainerDataSourceHandle
_ExtComputationPrimDataSource(
    UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle resolvedPrimSource)
{
    TRACE_FUNCTION();

    static HdContainerDataSourceHandle const outputs =
        _ExtComputationOutputs();
    const SdfPath &primPath = resolvedPrimSource->GetPrimPath();
    HdSizetDataSourceHandle const numPoints =
        _NumPointsDataSource::New(resolvedPrimSource->GetPrimvars());

    return
        HdRetainedContainerDataSource::New(
            HdExtComputationSchema::GetSchemaToken(),
            HdExtComputationSchema::Builder()
                .SetInputValues(
                    _ExtComputationInputValuesDataSource::New(
                        resolvedPrimSource))
                .SetInputComputations(
                    _ExtComputationInputComputations(
                        primPath))
                .SetOutputs(
                    outputs)
                .SetGlslKernel(
                    UsdSkelImagingExtComputationGlslKernel(
                        resolvedPrimSource->GetSkinningMethod()))
                .SetCpuCallback(
                    UsdSkelImagingExtComputationCpuCallback(
                        resolvedPrimSource->GetSkinningMethod()))
                .SetDispatchCount(numPoints)
                .SetElementCount(numPoints)
                .Build());
}

}

HdContainerDataSourceHandle
UsdSkelImagingDataSourceResolvedExtComputationPrim(
    UsdSkelImagingDataSourceResolvedPointsBasedPrimHandle resolvedPrimSource,
    const TfToken &computationName)
{
    TRACE_FUNCTION();

    if (computationName == UsdSkelImagingExtComputationNameTokens
                                ->computation) {
        return
            _ExtComputationPrimDataSource(
                std::move(resolvedPrimSource));
    }
    if (computationName == UsdSkelImagingExtComputationNameTokens
                                ->aggregatorComputation) {
        return
            _ExtAggregatorComputationPrimDataSource(
                std::move(resolvedPrimSource));
    }

    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
