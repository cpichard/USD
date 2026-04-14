//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/exec/execIr/controllerBuilder.h"
#include "pxr/exec/execIr/tokens.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/token.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/vdf/context.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

// Builder used to regiser computations for input and output attributes.
//
// TODO: When the OpenExec core provides builtin support for attribute conection
// data flow and for inversion, we won't need to define any of the plugin
// computations defined by this builder.
// 
class _Builder {
public:
    _Builder(ExecComputationBuilder &self)
        : _self(self)
    {}

    // Define the computations needed for an attribute that provides input
    // values for an invertible controller, via an attribute connection, when
    // performing a forward computation.
    //
    template <typename ValueType>
    void InputAttribute(const TfToken &attributeName);

    // Define the computations needed for an attribute that receives values
    // values from an invertible controller output, via an attribute connection,
    // when performing a forward computation.
    //
    template <typename ValueType>
    void OutputAttribute(const TfToken &attributeName);

private:
    // Returns a pointer to the desired value provided by the
    // 'explicitDesiredValue' and 'computedDesiredValue' inputs; otherwise,
    // returns null.
    //
    // If more than one input value is provided, an error is emitted.
    //
    template <typename ValueType>
    static const ValueType *
    _GetExactlyOneDesiredValue(
        const VdfContext &ctx);

private:
    ExecComputationBuilder &_self;
};

} // anonymous namespace

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(ExecIrJointScope)
{
    _Builder builder(self);

    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsTx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsTy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsTz);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsRx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsRy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsRz);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsRspin);
    builder.InputAttribute<double>(ExecIrTransformableTokens->avarsTotalSize);
    builder.InputAttribute<TfToken>(
        ExecIrTransformableTokens->avarsRotationOrder);
    builder.InputAttribute<double>(
        ExecIrTransformableTokens->avarsDefaultTotalSize);
    builder.InputAttribute<GfMatrix4d>(
        ExecIrTransformableTokens->avarsDefaultSpace);
    builder.InputAttribute<double>(
        ExecIrTransformableTokens->avarsUnitScaleFactor);

    builder.InputAttribute<double>(ExecIrTransformableTokens->restTx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->restTy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->restTz);
    builder.InputAttribute<double>(ExecIrTransformableTokens->restRx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->restRy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->restRz);
    builder.InputAttribute<GfMatrix4d>(ExecIrTransformableTokens->restSpace);

    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultTx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultTy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultTz);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultRx);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultRy);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultRz);
    builder.InputAttribute<double>(ExecIrTransformableTokens->defaultTotalSize);
    builder.InputAttribute<GfMatrix4d>(ExecIrTransformableTokens->defaultSpace);

    builder.OutputAttribute<GfMatrix4d>(ExecIrTransformableTokens->posedSpace);
    builder.OutputAttribute<GfMatrix4d>(
        ExecIrTransformableTokens->posedDefaultSpace);
    
    builder.InputAttribute<GfMatrix4d>(ExecIrTransformableTokens->parentSpace);
    builder.InputAttribute<double>(
        ExecIrTransformableTokens->parentDefaultTotalSize);
    builder.InputAttribute<GfMatrix4d>(
        ExecIrTransformableTokens->parentDefaultSpace);
    builder.InputAttribute<double>(ExecIrTransformableTokens->parentTotalSize);
}

template <typename ValueType>
void
_Builder::InputAttribute(const TfToken &attributeName)
{
    using namespace exec_registration;

    // The 'computeDesiredValue' computation gets its value from the
    // 'computeDesiredValue' computation via incoming connections--but only if
    // there is exactly one desired value present. Otherwise, no value is
    // returned. An error is emitted if more than one desired value is present.
    //
    // TODO: This plugin computation won't be necessary when OpenExec provides
    // core inversion support.
    _self.AttributeComputation(
        attributeName, ExecIrComputationTokens->computeDesiredValue)
        .Callback<ValueType>(+[](const VdfContext &ctx) {
            if (const ValueType *const valuePtr =
                _GetExactlyOneDesiredValue<ValueType>(ctx)) {
                ctx.SetOutput(*valuePtr);
            } else {
                ctx.SetEmptyOutput();
            }
        })
        .Inputs(
            IncomingConnections<ValueType>(
                ExecIrComputationTokens->computeDesiredValue));
}

template <typename ValueType>
void
_Builder::OutputAttribute(const TfToken &attributeName)
{
    using namespace exec_registration;

    // Output attributes support dataflow across connections.
    //
    // TODO: This expression won't be needed when the OpenExec core defines
    // default attribute connection dataflow behavior for all attributes.
    _self.AttributeExpression(attributeName)
        .Callback(+[](const VdfContext &ctx) -> ValueType {
            // A value that flows across a connection takes precedence.
            if (const ValueType *const connectedValuePtr =
                ctx.GetInputValuePtr<ValueType>(
                    ExecBuiltinComputations->computeValue)) {
                return *connectedValuePtr;
            }

            // Otherwise, the attribute's resolved value is its computed
            // value.
            return ctx.GetInputValue<ValueType>(
                ExecBuiltinComputations->computeResolvedValue);
        })
        .Inputs(
            Connections<ValueType>(
                ExecBuiltinComputations->computeValue),
            Computation<ValueType>(
                ExecBuiltinComputations->computeResolvedValue));

    // The 'explicitDesiredValue' computation only exists to provide an
    // output where desired values can be specified as overrides passed to
    // ComputeWithOverrides.
    //
    // TODO: This plugin computation won't be necessary when OpenExec
    // provides core inversion support.
    _self.AttributeComputation(
        attributeName, ExecIrComputationTokens->explicitDesiredValue)
        .Callback<ValueType>(+[](const VdfContext &ctx) {
            ctx.SetEmptyOutput();
        });

    // The 'computeDesiredValue' computation gets its value from the
    // 'explicitDesiredValue' computation if it provides one, or the
    // `computeDesiredValue` via incoming connections if it provides
    // one. Otherwise, no value is returned.
    //
    // TODO: This plugin computation won't be necessary when OpenExec
    // provides core inversion support.
    _self.AttributeComputation(
        attributeName, ExecIrComputationTokens->computeDesiredValue)
        .Callback<ValueType>(+[](const VdfContext &ctx) {
            if (const ValueType *const valuePtr =
                ctx.GetInputValuePtr<ValueType>(
                    ExecIrComputationTokens->explicitDesiredValue)) {
                ctx.SetOutput(*valuePtr);
            } else if (const ValueType *const valuePtr =
                       ctx.GetInputValuePtr<ValueType>(
                           ExecIrComputationTokens->computeDesiredValue)) {
                ctx.SetOutput(*valuePtr);
            } else {
                ctx.SetEmptyOutput();
            }
        })
        .Inputs(
            Computation<ValueType>(
                ExecIrComputationTokens->explicitDesiredValue),
            IncomingConnections<ValueType>(
                ExecIrComputationTokens->computeDesiredValue));
}

template <typename ValueType>
const ValueType *
_Builder::_GetExactlyOneDesiredValue(const VdfContext &ctx)
{
    const ValueType *foundInputValue = 
        ctx.GetInputValuePtr<ValueType>(
            ExecIrComputationTokens->explicitDesiredValue);

    for (const ValueType &value :
             VdfReadIteratorRange<ValueType>(
                 ctx, ExecIrComputationTokens->computeDesiredValue)) {
        if (!foundInputValue) {
            foundInputValue = &value;
        } else {
            // TODO: We will introduce an ExecValidationErrorType enum to
            // specifically identifiy this error condition.
            TF_RUNTIME_ERROR(
                "Found more than one desired value for node %s",
                ctx.GetNodeDebugName().c_str());
            return nullptr;
        }
    }

    return foundInputValue;
}
