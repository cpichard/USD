//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/exec/execIr/controllerBuilder.h"

PXR_NAMESPACE_OPEN_SCOPE

ExecIrControllerBuilder::ExecIrControllerBuilder(
    ExecComputationBuilder &self,
    Callback forwardCallback,
    Callback inverseCallback)
    : _self(self)
    , _forwardComputeReg(
        self.PrimComputation(ExecIrComputationTokens->forwardCompute))
    , _inverseComputeReg(
        self.PrimComputation(ExecIrComputationTokens->inverseCompute))
    , _inverseCallback(inverseCallback)
{
    _forwardComputeReg.Callback<ExecIrResult>(forwardCallback);
}

ExecIrControllerBuilder::~ExecIrControllerBuilder()
{
    // Wrap the inverse callback in a lambda that checks if we have input values
    // for all invertible output attributes and returns an empty ExecIrResult if
    // any are missing.
    _inverseComputeReg.Callback<ExecIrResult>(
        [inverseCallback = _inverseCallback,
         invertibleOutputAttributeNames =
             std::move(_invertibleOutputAttributeNames)]
        (const VdfContext &ctx) -> void {
            for (const TfToken &name : invertibleOutputAttributeNames) {
                if (!ctx.HasInputValue(name)) {
                    ctx.SetOutput(ExecIrResult{});
                    return;
                }
            }

            ctx.SetOutput(inverseCallback(ctx));
        });
}

PXR_NAMESPACE_CLOSE_SCOPE
