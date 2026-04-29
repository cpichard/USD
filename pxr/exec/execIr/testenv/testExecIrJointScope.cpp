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
Test_JointScopeBasic()
{
    _EnsureNoErrors mark;

    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(
        #usda 1.0

        def Scope "Root" {
            def IrJointScope "Joint" {
                double avars:tx =     1.0
                double avars:ty =     2.0
                double avars:tz =     3.0
                double avars:rx =    90.0
                double avars:ry =   -90.0
                double avars:rz =    90.0
                double avars:rspin =  0.0
                matrix4d posed:space.connect = </Root/FkController.Out:Space>
            }

            def IrFkController "FkController" {
                double In:Tx.connect = </Root/Joint.avars:tx>
                double In:Ty.connect = </Root/Joint.avars:ty>
                double In:Tz.connect = </Root/Joint.avars:tz>
                double In:Rx.connect = </Root/Joint.avars:rx>
                double In:Ry.connect = </Root/Joint.avars:ry>
                double In:Rz.connect = </Root/Joint.avars:rz>
                double In:Rspin.connect = </Root/Joint.avars:rspin>
            }
        }
        )usda");
    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    const UsdPrim joint = usdStage->GetPrimAtPath(SdfPath("/Root/Joint"));
    TF_AXIOM(joint);
    const UsdAttribute posedSpace =
        joint.GetAttribute(ExecIrTransformableTokens->posedSpace);
    TF_AXIOM(posedSpace);

    const std::vector<UsdAttribute> inputAttributes = {
        joint.GetAttribute(ExecIrTransformableTokens->avarsRx),
        joint.GetAttribute(ExecIrTransformableTokens->avarsRy),
        joint.GetAttribute(ExecIrTransformableTokens->avarsRz),
        joint.GetAttribute(ExecIrTransformableTokens->avarsRspin),

        joint.GetAttribute(ExecIrTransformableTokens->avarsTx),
        joint.GetAttribute(ExecIrTransformableTokens->avarsTy),
        joint.GetAttribute(ExecIrTransformableTokens->avarsTz),
    };
    for (const UsdAttribute &attr : inputAttributes) {
        TF_AXIOM(attr);
    }

    ExecUsdSystem execSystem(usdStage);

    {
        const ExecUsdRequest request = execSystem.BuildRequest({
            ExecUsdValueKey{posedSpace, ExecBuiltinComputations->computeValue}
        });
        TF_AXIOM(request.IsValid());

        // Compute forward to get the output value produced by the authored
        // scene.
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
    }

    {
        std::vector<ExecUsdValueKey> valueKeys;
        for (const UsdAttribute &attr : inputAttributes) {
            valueKeys.emplace_back(
                attr, ExecIrComputationTokens->computeDesiredValue);
        }
        const ExecUsdRequest request =
            execSystem.BuildRequest(std::move(valueKeys));
        TF_AXIOM(request.IsValid());

        // Perform an inverse compute, to get the values of the avars that
        // produce the desired value for the posed space.
        {
            const GfMatrix4d desiredPosedSpaceValue(0,  0, 1, 0,
                                                    0, -1, 0, 0,
                                                    1,  0, 0, 0,
                                                    1,  2, 3, 1);
            ExecUsdValueOverrideVector overrides {
                {{posedSpace, ExecIrComputationTokens->explicitDesiredValue},
                 VtValue(desiredPosedSpaceValue)}
            };
            ExecUsdCacheView cache =
                execSystem.ComputeWithOverrides(request, std::move(overrides));

            // Expected input values in the same order as the value keys in the
            // request.
            const std::vector<double> expectedInputValues{
                90.0, -90.0, 90.0, 0.0,    1.0, 2.0, 3.0,
            };
            for (unsigned int index=0;
                 index<expectedInputValues.size(); ++index) {
                ASSERT_CLOSE(
                    cache.Get(index).Get<double>(),
                    expectedInputValues[index]);
            }
        }

        // Compute with a different desired value.
        {
            const GfMatrix4d desiredPosedSpaceValue( 0,  0, -1, 0,
                                                     0,  1,  0, 0,
                                                     1,  0,  0, 0,
                                                    10, 20, 30, 1);
            ExecUsdValueOverrideVector overrides {
                {{posedSpace, ExecIrComputationTokens->explicitDesiredValue},
                 VtValue(desiredPosedSpaceValue)}
            };
            ExecUsdCacheView cache =
                execSystem.ComputeWithOverrides(request, std::move(overrides));

            // Expected input values in the same order as the value keys in the
            // request.
            const std::vector<double> expectedInputValues{
                0.0, 90.0, 0.0, 0.0,    10.0, 20.0, 30.0,
            };
            for (unsigned int index=0;
                 index<expectedInputValues.size(); ++index) {
                ASSERT_CLOSE(
                    cache.Get(index).Get<double>(),
                    expectedInputValues[index]);
            }
        }
    }
}

static void
Test_JointScopeParentChild()
{
    _EnsureNoErrors mark;

    // Create a scene with two fkControllers, where the child controller's
    // parent space is connected to the output of the parent controller.
    const SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    layer->ImportFromString(
        R"usda(
        #usda 1.0

        def Scope "Root" {
            def IrJointScope "ParentJoint" {
                double avars:tx =     0.0
                double avars:ty =     0.0
                double avars:tz =     0.0
                double avars:rx =    90.0
                double avars:ry =   -90.0
                double avars:rz =    90.0
                double avars:rspin =  0.0
                matrix4d posed:space.connect = </Root/ParentController.Out:Space>

                def IrJointScope "ChildJoint" {
                    double avars:tx =    1.0
                    double avars:ty =    2.0
                    double avars:tz =    3.0
                    double avars:rx =    0.0
                    double avars:ry =    0.0
                    double avars:rz =    0.0
                    double avars:rspin = 0.0
                    matrix4d posed:space.connect = </Root/ChildController.Out:Space>
                }
            }

            def IrFkController "ParentController" {
                double In:Tx.connect = </Root/ParentJoint.avars:tx>
                double In:Ty.connect = </Root/ParentJoint.avars:ty>
                double In:Tz.connect = </Root/ParentJoint.avars:tz>
                double In:Rx.connect = </Root/ParentJoint.avars:rx>
                double In:Ry.connect = </Root/ParentJoint.avars:ry>
                double In:Rz.connect = </Root/ParentJoint.avars:rz>
                double In:Rspin.connect = </Root/ParentJoint.avars:rspin>
            }

            def IrFkController "ChildController" {
                double In:Tx.connect = </Root/ParentJoint/ChildJoint.avars:tx>
                double In:Ty.connect = </Root/ParentJoint/ChildJoint.avars:ty>
                double In:Tz.connect = </Root/ParentJoint/ChildJoint.avars:tz>
                double In:Rx.connect = </Root/ParentJoint/ChildJoint.avars:rx>
                double In:Ry.connect = </Root/ParentJoint/ChildJoint.avars:ry>
                double In:Rz.connect = </Root/ParentJoint/ChildJoint.avars:rz>
                double In:Rspin.connect = </Root/ParentJoint/ChildJoint.avars:rspin>

                matrix4d ParentIn:Space.connect = </Root/ParentController.Out:Space>
            }
        }
        )usda");

    const UsdStageConstRefPtr usdStage = UsdStage::Open(layer);
    TF_AXIOM(usdStage);

    const UsdPrim parent =
        usdStage->GetPrimAtPath(SdfPath("/Root/ParentJoint"));
    const UsdPrim child =
        usdStage->GetPrimAtPath(SdfPath("/Root/ParentJoint/ChildJoint"));
    TF_AXIOM(parent && child);

    const UsdAttribute parentPosedSpace =
        parent.GetAttribute(ExecIrTransformableTokens->posedSpace);
    const UsdAttribute childPosedSpace =
        child.GetAttribute(ExecIrTransformableTokens->posedSpace);
    TF_AXIOM(parentPosedSpace && childPosedSpace);

    const std::vector<UsdAttribute> inputAttributes = {
        parent.GetAttribute(ExecIrTransformableTokens->avarsRx),
        parent.GetAttribute(ExecIrTransformableTokens->avarsRy),
        parent.GetAttribute(ExecIrTransformableTokens->avarsRz),
        parent.GetAttribute(ExecIrTransformableTokens->avarsRspin),

        parent.GetAttribute(ExecIrTransformableTokens->avarsTx),
        parent.GetAttribute(ExecIrTransformableTokens->avarsTy),
        parent.GetAttribute(ExecIrTransformableTokens->avarsTz),

        child.GetAttribute(ExecIrTransformableTokens->avarsRx),
        child.GetAttribute(ExecIrTransformableTokens->avarsRy),
        child.GetAttribute(ExecIrTransformableTokens->avarsRz),
        child.GetAttribute(ExecIrTransformableTokens->avarsRspin),

        child.GetAttribute(ExecIrTransformableTokens->avarsTx),
        child.GetAttribute(ExecIrTransformableTokens->avarsTy),
        child.GetAttribute(ExecIrTransformableTokens->avarsTz),
    };
    for (const UsdAttribute &attr : inputAttributes) {
        TF_AXIOM(attr);
    }

    ExecUsdSystem execSystem(usdStage);

    const ExecUsdRequest outputRequest = execSystem.BuildRequest({
            ExecUsdValueKey{
                parentPosedSpace, ExecBuiltinComputations->computeValue},
            ExecUsdValueKey{
                childPosedSpace, ExecBuiltinComputations->computeValue},
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

    const GfMatrix4d desiredParentPosedSpaceValue(0,  0, 1, 0,
                                                  0, -1, 0, 0,
                                                  1,  0, 0, 0,
                                                  0,  0, 0, 1);
    GfMatrix4d desiredChildPosedSpaceValue = desiredParentPosedSpaceValue;
    desiredChildPosedSpaceValue.SetTranslateOnly({1, 2, 3});

    // Expected input values in the same order as the value keys in the request.
    const std::vector<double> expectedInputValues{
        90.0, -90.0, 90.0, 0.0,    0.0,  0.0, 0.0,
        0.0,   0.0,   0.0, 0.0,    3.0, -2.0, 1.0,
    };

    {
        ExecUsdValueOverrideVector overrides {
            {{parentPosedSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredParentPosedSpaceValue)},
            {{childPosedSpace, ExecIrComputationTokens->explicitDesiredValue},
             VtValue(desiredChildPosedSpaceValue)},
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

        ASSERT_CLOSE(cache.Get(0).Get<GfMatrix4d>(),
                     desiredParentPosedSpaceValue);
        ASSERT_CLOSE(cache.Get(1).Get<GfMatrix4d>(),
                     desiredChildPosedSpaceValue);
    }
}

int main(int argc, char **argv)
{
    Test_JointScopeBasic();
    Test_JointScopeParentChild();
}
