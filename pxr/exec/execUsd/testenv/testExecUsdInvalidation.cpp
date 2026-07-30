//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/ts/knot.h"
#include "pxr/base/ts/spline.h"
#include "pxr/base/vt/value.h"
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/systemDiagnostics.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/stage.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

#define ASSERT_EQ(expr, expected)                                              \
    [&] {                                                                      \
        std::cout << std::flush;                                               \
        std::cerr << std::flush;                                               \
        auto&& expr_ = expr;                                                   \
        if (expr_ != expected) {                                               \
            TF_FATAL_ERROR(                                                    \
                "Expected " TF_PP_STRINGIZE(expr) " == '%s'; got '%s'",        \
                TfStringify(expected).c_str(),                                 \
                TfStringify(expr_).c_str());                                   \
        }                                                                      \
    }()

// Verfies that the computed value of the given attribute matches the given
// expected value.
template <typename ValueType>
static void
_VerifyAttrValue(
    const UsdAttribute &attr,
    const ValueType expectedValue,
    ExecUsdSystem *execSystem)
{
    const ExecUsdRequest request =
        execSystem->BuildRequest({ExecUsdValueKey{
            attr, ExecBuiltinComputations->computeResolvedValue}});

    ExecUsdCacheView cache = execSystem->Compute(request);
    const VtValue value = cache.Get(0);
    TF_AXIOM(value.IsHolding<ValueType>());
    ASSERT_EQ(value.Get<ValueType>(), expectedValue);
}

// Sets a spline on attr with a knot with the given value at the given time.
static void
_SetSplineKnot(
    const UsdTimeCode &time,
    const double value,
    UsdAttribute *const attr)
{
    static const TfType doubleType = TfType::Find<double>();
    TsSpline spline(doubleType);

    TsKnot knot = TsKnot(doubleType);
    knot.SetTime(time.GetValue());
    knot.SetValue(VtValue(value));
    spline.SetKnot(knot);

    attr->SetSpline(spline);
}

static void
Test_ValueInvalidation()
{
    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(#usda 1.0
        def Scope "Prim" {
            custom double attr = 0
        }
        )usda");
    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    UsdAttribute attr = usdStage->GetAttributeAtPath(SdfPath("/Prim.attr"));
    TF_AXIOM(attr);

    ExecUsdSystem execSystem(usdStage);
    UsdTimeCode time(0.0);
    execSystem.ChangeTime(time);

    _VerifyAttrValue(attr, 0.0, &execSystem);

    // Author the default value.
    attr.Set(1.0);
    _VerifyAttrValue(attr, 1.0, &execSystem);

    // Change the default value.
    attr.Set(2.0);
    _VerifyAttrValue(attr, 2.0, &execSystem);

    // Add a spline.
    _SetSplineKnot(time, 3.0, &attr);
    _VerifyAttrValue(attr, 3.0, &execSystem);

    // Change the value produced by the spline.
    _SetSplineKnot(time, 4.0, &attr);
    _VerifyAttrValue(attr, 4.0, &execSystem);

    // Author a time sample.
    attr.Set(5.0, time);
    _VerifyAttrValue(attr, 5.0, &execSystem);

    // Change the time sample value.
    attr.Set(6.0, time);
    _VerifyAttrValue(attr, 6.0, &execSystem);

    // InvalidateAll: The computed value should remain the same. (And we
    // shouldn't crash.)
    ExecSystem::Diagnostics(&execSystem).InvalidateAll();
    execSystem.ChangeTime(time);
    _VerifyAttrValue(attr, 6.0, &execSystem);

    // Set the time back to default.
    execSystem.ChangeTime(UsdTimeCode::Default());
    _VerifyAttrValue(attr, 2.0, &execSystem);
}

template <typename ValueType>
static void
doTimeInvalidationTest(
    const std::string &layerContents,
    std::vector<std::pair<UsdTimeCode, ValueType>> timeAndValues)
{
    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(layerContents);
    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    UsdAttribute attr = usdStage->GetAttributeAtPath(SdfPath("/Prim.attr"));
    TF_AXIOM(attr);

    ExecUsdSystem execSystem(usdStage);

    for (const auto &[time, expectedValue] : timeAndValues) {
        execSystem.ChangeTime(time);
        _VerifyAttrValue(attr, expectedValue, &execSystem);
    }
}

static void
Test_TimeInvalidationSpline()
{
    doTimeInvalidationTest<double>(
        R"usda(#usda 1.0
            def Scope "Prim" {
                custom double attr = -11.0
                double attr.spline = {
                    0: 0; post linear,
                    10: 10; post held,
                }
            }
        )usda",
        {
            {UsdTimeCode(-1.0), 0.0},
            {UsdTimeCode(0.0), 0.0},
            {UsdTimeCode(5.0), 5.0},
            {UsdTimeCode(10.0), 10.0},
            {UsdTimeCode::Default(), -11.0},
            {UsdTimeCode(15.0), 10.0},
        });
}

static void
Test_TimeInvalidationTimeSamples()
{
    doTimeInvalidationTest<std::string>(
        R"usda(#usda 1.0
            def Scope "Prim" {
                custom string attr = "default"
                string attr.timeSamples = {
                    0: "time sample 0",
                    10: "time sample 10",
                }
            }
        )usda",
        {
            {UsdTimeCode(-1.0), {"time sample 0"}},
            {UsdTimeCode(0.0), {"time sample 0"}},
            {UsdTimeCode(5.0), {"time sample 0"}},
            {UsdTimeCode(10.0), {"time sample 10"}},
            {UsdTimeCode::Default(), {"default"}},
            {UsdTimeCode(15.0), {"time sample 10"}},
        });
}

static void
Test_TimeInvalidationSingleTimeSample()
{
    doTimeInvalidationTest<std::string>(
        R"usda(#usda 1.0
            def Scope "Prim" {
                custom string attr = "default"
                string attr.timeSamples = {
                    0: "time sample",
                }
            }
        )usda",
        {
            {UsdTimeCode(-1.0), {"time sample"}},
            {UsdTimeCode(0.0), {"time sample"}},
            {UsdTimeCode(1.0), {"time sample"}},
            {UsdTimeCode::Default(), {"default"}},
            {UsdTimeCode(10.0), {"time sample"}},
        });
}

int main()
{
    Test_ValueInvalidation();
    Test_TimeInvalidationSpline();
    Test_TimeInvalidationTimeSamples();
    Test_TimeInvalidationSingleTimeSample();

    return 0;
}
