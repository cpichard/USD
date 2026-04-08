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
{
    _forwardComputeReg.Callback<ExecIrResult>(forwardCallback);

    // TODO:
    // Wrap the inverse callback in a function that checks if we have input
    // values for all invertible output attributes and returns an empty
    // ExecIrResult if any are missing, so that the client code doesn't have to
    // do so.
    //
    // To do that, we'd need to allow for general functors as callbacks, and
    // we'd also need some way to check for a non-empty input value without
    // knowing the value type.
    _inverseComputeReg.Callback(inverseCallback);
}

const TfToken &
ExecIrControllerBuilder::_GetConstantInputName()
{
    static TfToken inputNameToken("execIrControllerBuilder_inputName");
    return inputNameToken;
}

PXR_NAMESPACE_CLOSE_SCOPE
