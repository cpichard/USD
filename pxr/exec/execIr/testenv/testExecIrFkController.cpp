//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/exec/execIr/tokens.h"
#include "pxr/exec/execIr/types.h"

#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"

#include <iostream>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

#define ASSERT_CLOSE(expr, expected)                                           \
    [&] {                                                                      \
        auto&& expr_ = expr;                                                   \
        if (!GfIsClose(expr_, expected, 1e-6)) {                               \
            std::cout << std::flush;                                           \
            std::cerr << std::flush;                                           \
            TF_FATAL_ERROR(                                                    \
                "Expected " TF_PP_STRINGIZE(expr) " == '%s'; got '%s'",        \
                TfStringify(expected).c_str(),                                 \
                TfStringify(expr_).c_str());                                   \
        }                                                                      \
    }()

struct _EnsureNoErrors {
    ~_EnsureNoErrors() {
        TF_VERIFY(mark.IsClean());
    }

    TfErrorMark mark;
};

static void
Test_IrForwardCompute()
{
    _EnsureNoErrors mark;

    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(
        #usda 1.0

        def Scope "Root" {
            def IrFkController "FkController" {
                double In:Rx =    90.0
                double In:Ry =   -90.0
                double In:Rz =    90.0
                double In:Rspin =  0.0
                double In:Tx =     1.0
                double In:Ty =     2.0
                double In:Tz =     3.0
            }
        }
        )usda");
    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    const UsdPrim prim = usdStage->GetPrimAtPath(SdfPath("/Root/FkController"));
    TF_AXIOM(prim);
    const UsdAttribute outSpace = prim.GetAttribute(ExecIrTokens->outSpaceToken);
    TF_AXIOM(outSpace);

    ExecUsdSystem execSystem(usdStage);
    const ExecUsdRequest request = execSystem.BuildRequest({
        ExecUsdValueKey{outSpace, ExecBuiltinComputations->computeValue}
    });
    TF_AXIOM(request.IsValid());

    // Compute forward to get the output value produced by the authored scene.
    {
        ExecUsdCacheView cache = execSystem.Compute(request);
        const VtValue value = cache.Get(0);
        TF_AXIOM(!value.IsEmpty());

        ASSERT_CLOSE(
            value.Get<GfMatrix4d>(),
            GfMatrix4d(0,  0, 1, 0,
                       0, -1, 0, 0,
                       1,  0, 0, 0,
                       1,  2, 3, 1));
    }

    // Now set the parent space and compute again.
    {
        const UsdAttribute parentSpace =
            prim.GetAttribute(ExecIrTokens->parentSpaceToken);
        TF_AXIOM(parentSpace);
        parentSpace.Set(GfMatrix4d(1, 0, 0, 0,
                                   0, 1, 0, 0,
                                   0, 0, 1, 0,
                                   1, 1, 1, 1));

        ExecUsdCacheView cache = execSystem.Compute(request);
        const VtValue value = cache.Get(0);
        TF_AXIOM(!value.IsEmpty());
        ASSERT_CLOSE(
            value.Get<GfMatrix4d>(),
            GfMatrix4d(0,  0, 1, 0,
                       0, -1, 0, 0,
                       1,  0, 0, 0,
                       2,  3, 4, 1));
    }
}

static void
Test_IrInverseCompute()
{
    _EnsureNoErrors mark;

    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(
        #usda 1.0

        def Scope "Root" {
            def IrFkController "FkController" {
            }
        }
        )usda");

    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    const UsdPrim prim = usdStage->GetPrimAtPath(SdfPath("/Root/FkController"));
    TF_AXIOM(prim);

    const UsdAttribute outSpace = prim.GetAttribute(ExecIrTokens->outSpaceToken);
    TF_AXIOM(outSpace);

    const std::vector<UsdAttribute> inputAttributes = {
        prim.GetAttribute(ExecIrTokens->rxToken),
        prim.GetAttribute(ExecIrTokens->ryToken),
        prim.GetAttribute(ExecIrTokens->rzToken),
        prim.GetAttribute(ExecIrTokens->rspinToken),

        prim.GetAttribute(ExecIrTokens->txToken),
        prim.GetAttribute(ExecIrTokens->tyToken),
        prim.GetAttribute(ExecIrTokens->tzToken),
    };
    for (const UsdAttribute &attr : inputAttributes) {
        TF_AXIOM(attr);
    }

    ExecUsdSystem execSystem(usdStage);

    std::vector<ExecUsdValueKey> valueKeys;
    for (const UsdAttribute &attr : inputAttributes) {
        valueKeys.emplace_back(
            attr, ExecIrComputationTokens->computeDesiredValue);
    }
    const ExecUsdRequest request = execSystem.BuildRequest(std::move(valueKeys));
    TF_AXIOM(request.IsValid());

    // Perform an inverse compute, to get the values of the invertible inputs
    // that produce the desired value for the output space.
    {
        const GfMatrix4d desiredOutSpaceValue(0,  0, 1, 0,
                                              0, -1, 0, 0,
                                              1,  0, 0, 0,
                                              1,  2, 3, 1);
        ExecUsdValueOverrideVector overrides {
            {{outSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredOutSpaceValue)}
        };
        ExecUsdCacheView cache =
            execSystem.ComputeWithOverrides(request, std::move(overrides));

        // Expected input values in the same order as the value keys in the
        // request.
        const std::vector<double> expectedInputValues{
            90.0, -90.0, 90.0, 0.0,    1.0, 2.0, 3.0,
        };
        for (unsigned int index=0; index<expectedInputValues.size(); ++index) {
            ASSERT_CLOSE(
                cache.Get(index).Get<double>(),
                expectedInputValues[index]);
        }
    }

    // Compute with a different desired value.
    {
        const GfMatrix4d desiredOutSpaceValue( 0,  0, -1, 0,
                                               0,  1,  0, 0,
                                               1,  0,  0, 0,
                                              10, 20, 30, 1);
        ExecUsdValueOverrideVector overrides {
            {{outSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredOutSpaceValue)}
        };
        ExecUsdCacheView cache =
            execSystem.ComputeWithOverrides(request, std::move(overrides));

        // Expected input values in the same order as the value keys in the
        // request.
        const std::vector<double> expectedInputValues{
            0.0, 90.0, 0.0, 0.0,    10.0, 20.0, 30.0,
        };
        for (unsigned int index=0; index<expectedInputValues.size(); ++index) {
            ASSERT_CLOSE(
                cache.Get(index).Get<double>(),
                expectedInputValues[index]);
        }
    }

    // Now set the parent space and compute the inverse again.
    {
        const UsdAttribute parentSpace =
            prim.GetAttribute(ExecIrTokens->parentSpaceToken);
        TF_AXIOM(parentSpace);
        parentSpace.Set(GfMatrix4d(1, 0, 0, 0,
                                   0, 1, 0, 0,
                                   0, 0, 1, 0,
                                   1, 1, 1, 1));

        const GfMatrix4d desiredOutSpaceValue( 0,  0, -1, 0,
                                               0,  1,  0, 0,
                                               1,  0,  0, 0,
                                              10, 20, 30, 1);
        ExecUsdValueOverrideVector overrides {
            {{outSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredOutSpaceValue)}
        };
        ExecUsdCacheView cache =
            execSystem.ComputeWithOverrides(request, std::move(overrides));

        // Expected input values in the same order as the value keys in the
        // request.
        const std::vector<double> expectedInputValues{
            0.0, 90.0, 0.0, 0.0,    9.0, 19.0, 29.0,
        };
        for (unsigned int index=0; index<expectedInputValues.size(); ++index) {
            ASSERT_CLOSE(
                cache.Get(index).Get<double>(),
                expectedInputValues[index]);
        }
    }
}

static void
Test_DependentFkControllers()
{
    _EnsureNoErrors mark;

    // Create a scene with two fkControllers, where the child controller's
    // parent space is connected to the output of the parent controller.
    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(
        #usda 1.0

        def Scope "Root" {
            def IrFkController "Parent" {
            }

            def IrFkController "Child" {
                matrix4d ParentIn:Space.connect = </Root/Parent.Out:Space>
            }
        }
        )usda");

    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    const UsdPrim parent = usdStage->GetPrimAtPath(SdfPath("/Root/Parent"));
    const UsdPrim child = usdStage->GetPrimAtPath(SdfPath("/Root/Child"));
    TF_AXIOM(parent && child);

    const UsdAttribute parentOutSpace =
        parent.GetAttribute(ExecIrTokens->outSpaceToken);
    const UsdAttribute childOutSpace =
        child.GetAttribute(ExecIrTokens->outSpaceToken);
    TF_AXIOM(parentOutSpace && childOutSpace);

    const std::vector<UsdAttribute> inputAttributes = {
        parent.GetAttribute(ExecIrTokens->rxToken),
        parent.GetAttribute(ExecIrTokens->ryToken),
        parent.GetAttribute(ExecIrTokens->rzToken),
        parent.GetAttribute(ExecIrTokens->rspinToken),

        parent.GetAttribute(ExecIrTokens->txToken),
        parent.GetAttribute(ExecIrTokens->tyToken),
        parent.GetAttribute(ExecIrTokens->tzToken),

        child.GetAttribute(ExecIrTokens->rxToken),
        child.GetAttribute(ExecIrTokens->ryToken),
        child.GetAttribute(ExecIrTokens->rzToken),
        child.GetAttribute(ExecIrTokens->rspinToken),

        child.GetAttribute(ExecIrTokens->txToken),
        child.GetAttribute(ExecIrTokens->tyToken),
        child.GetAttribute(ExecIrTokens->tzToken),
    };
    for (const UsdAttribute &attr : inputAttributes) {
        TF_AXIOM(attr);
    }

    ExecUsdSystem execSystem(usdStage);

    const ExecUsdRequest outputRequest = execSystem.BuildRequest({
            ExecUsdValueKey{
                parentOutSpace, ExecBuiltinComputations->computeValue},
            ExecUsdValueKey{
                childOutSpace, ExecBuiltinComputations->computeValue},
        });
    TF_AXIOM(outputRequest.IsValid());

    std::vector<ExecUsdValueKey> valueKeys;
    for (const UsdAttribute &attr : inputAttributes) {
        valueKeys.emplace_back(
            attr, ExecIrComputationTokens->computeDesiredValue);
    }
    const ExecUsdRequest inputRequest =
        execSystem.BuildRequest(std::move(valueKeys));
    TF_AXIOM(inputRequest.IsValid());

    const GfMatrix4d desiredParentOutSpaceValue(0,  0, 1, 0,
                                                0, -1, 0, 0,
                                                1,  0, 0, 0,
                                                0,  0, 0, 1);
    GfMatrix4d desiredChildOutSpaceValue = desiredParentOutSpaceValue;
    desiredChildOutSpaceValue.SetTranslateOnly({1, 2, 3});

    // Expected input values in the same order as the value keys in the request.
    const std::vector<double> expectedInputValues{
        90.0, -90.0, 90.0, 0.0,    0.0,  0.0, 0.0,
        0.0,   0.0,   0.0, 0.0,    3.0, -2.0, 1.0,
    };

    {
        ExecUsdValueOverrideVector overrides {
            {{parentOutSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredParentOutSpaceValue)},
            {{childOutSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredChildOutSpaceValue)},
        };
        ExecUsdCacheView cache =
            execSystem.ComputeWithOverrides(inputRequest, std::move(overrides));

        for (unsigned int index=0; index<expectedInputValues.size(); ++index) {
            ASSERT_CLOSE(
                cache.Get(index).Get<double>(),
                expectedInputValues[index]);
        }
    }

    // Author the expected input values and confirm we get the desired output
    // values.
    {
        int index = 0;
        for (const UsdAttribute &attr : inputAttributes) {
            attr.Set(expectedInputValues[index++]);
        }

        ExecUsdCacheView cache = execSystem.Compute(outputRequest);

        ASSERT_CLOSE(cache.Get(0).Get<GfMatrix4d>(), desiredParentOutSpaceValue);
        ASSERT_CLOSE(cache.Get(1).Get<GfMatrix4d>(), desiredChildOutSpaceValue);
    }
}

int main(int argc, char **argv)
{
    Test_IrForwardCompute();
    Test_IrInverseCompute();
    Test_DependentFkControllers();
}
