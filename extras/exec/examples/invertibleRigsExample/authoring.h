//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXTRAS_EXEC_EXAMPLES_INVERTIBLE_RIGS_EXAMPLE_AUTHORING_H
#define PXR_EXTRAS_EXEC_EXAMPLES_INVERTIBLE_RIGS_EXAMPLE_AUTHORING_H

#include "pxr/pxr.h"

#include "api.h"

#include "pxr/exec/execUsd/system.h"
#include "pxr/usd/usd/attribute.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdTimeCode;
class VtValue;

/// Class that implements authoring functionality needed to demonstrate an
/// invertible rig with compensation.
///
class InvertibleRigsExample_Authoring {
public:
    EXEC_INVERTIBLERIGSEXAMPLE_API
    InvertibleRigsExample_Authoring(const UsdStageRefPtr &stage);

    /// Adds a knot to the spline on \p attr with the given \p value at the
    /// given \p time, with curve interpolation.
    ///
    EXEC_INVERTIBLERIGSEXAMPLE_API
    static void
    SetSplineKnot(
        const UsdAttribute &attr,
        const UsdTimeCode &time,
        const VtValue &value);

    /// Authors a value to a switch attribute with compensation that authors
    /// input avar values such that the overall pose is the same before and
    /// after the change to the switch attribute value.
    ///
    EXEC_INVERTIBLERIGSEXAMPLE_API
    void
    CompensateSwitch(
        const UsdAttribute &switchAttribute,
        const UsdTimeCode &time,
        const TfToken &switchValue);

private:
    ExecUsdSystem _execSystem;
    UsdAttributeVector _inputAvars;
    UsdAttributeVector _outputSpaces;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
