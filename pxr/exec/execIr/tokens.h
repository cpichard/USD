//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_IR_TOKENS_H
#define PXR_EXEC_EXEC_IR_TOKENS_H

/// \file

#include "pxr/pxr.h"

#include "pxr/exec/execIr/api.h"

#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

#define EXEC_IR_COMPUTATION_TOKENS              \
    (computeDesiredValue)                       \
    (explicitDesiredValue)                      \
                                                \
    (computeInvertedForwardValue)               \
    (forwardCompute)                            \
    (inverseCompute)

TF_DECLARE_PUBLIC_TOKENS(
    ExecIrComputationTokens, EXECIR_API, EXEC_IR_COMPUTATION_TOKENS);

#define EXEC_IR_TOKENS                          \
    ((parentSpaceToken, "ParentIn:Space"))      \
                                                \
    ((defaultSpaceToken, "In:DefaultSpace"))    \
                                                \
    ((txToken, "In:Tx"))                        \
    ((tyToken, "In:Ty"))                        \
    ((tzToken, "In:Tz"))                        \
    ((rxToken, "In:Rx"))                        \
    ((ryToken, "In:Ry"))                        \
    ((rzToken, "In:Rz"))                        \
    ((rspinToken, "In:Rspin"))                  \
    ((rotationOrderToken, "In:RotationOrder"))  \
                                                \
    ((outSpaceToken, "Out:Space"))

TF_DECLARE_PUBLIC_TOKENS(ExecIrTokens, EXECIR_API, EXEC_IR_TOKENS);

#define EXEC_IR_TRANSFORMABLE_TOKENS                                            \
    ((avarsTx, "avars:tx"))                                                     \
    ((avarsTy, "avars:ty"))                                                     \
    ((avarsTz, "avars:tz"))                                                     \
    ((avarsRx, "avars:rx"))                                                     \
    ((avarsRy, "avars:ry"))                                                     \
    ((avarsRz, "avars:rz"))                                                     \
    ((avarsRspin, "avars:rspin"))                                               \
    ((avarsTotalSize, "avars:totalSize"))                                       \
    ((avarsRotationOrder, "avars:rotationOrder"))                               \
    ((avarsDefaultTotalSize, "avars:defaultTotalSize"))                         \
    ((avarsDefaultSpace, "avars:defaultSpace"))                                 \
    ((avarsUnitScaleFactor, "avars:unitScaleFactor"))                           \
                                                                                \
    ((restTx, "rest:tx"))                                                       \
    ((restTy, "rest:ty"))                                                       \
    ((restTz, "rest:tz"))                                                       \
    ((restRx, "rest:rx"))                                                       \
    ((restRy, "rest:ry"))                                                       \
    ((restRz, "rest:rz"))                                                       \
    ((restSpace, "rest:space"))                                                 \
                                                                                \
    ((defaultTx, "default:tx"))                                                 \
    ((defaultTy, "default:ty"))                                                 \
    ((defaultTz, "default:tz"))                                                 \
    ((defaultRx, "default:rx"))                                                 \
    ((defaultRy, "default:ry"))                                                 \
    ((defaultRz, "default:rz"))                                                 \
    ((defaultTotalSize, "default:totalSize"))                                   \
    ((defaultSpace, "default:space"))                                           \
                                                                                \
    ((posedSpace, "posed:space"))                                               \
    ((posedDefaultSpace, "posed:defaultSpace"))                                 \
                                                                                \
    ((parentSpace, "parent:Space"))                                             \
    ((parentDefaultTotalSize, "parent:defaultTotalSize"))                       \
    ((parentDefaultSpace, "parent:defaultSpace"))                               \
    ((parentTotalSize, "parent:totalSize"))

TF_DECLARE_PUBLIC_TOKENS(
    ExecIrTransformableTokens, EXECIR_API, EXEC_IR_TRANSFORMABLE_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
