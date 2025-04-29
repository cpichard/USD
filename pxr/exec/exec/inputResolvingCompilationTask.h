//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_INPUT_RESOLVING_COMPILATION_TASK_H
#define PXR_EXEC_EXEC_INPUT_RESOLVING_COMPILATION_TASK_H

#include "pxr/pxr.h"

#include "pxr/exec/exec/api.h"

#include "pxr/exec/exec/compilationTask.h"
#include "pxr/exec/exec/inputKey.h"
#include "pxr/exec/exec/outputKey.h"

#include "pxr/base/tf/smallVector.h"
#include "pxr/exec/vdf/maskedOutput.h"

PXR_NAMESPACE_OPEN_SCOPE

class EsfJournal;
class EsfObject;
class Exec_CompilationState;

/// Compilation task that resolves an input key to the source VdfMaskedOutput
/// where the input data is ultimately sourced from.
/// 
/// This tasks traverses the scene to find the ultimate provider object of the
/// computation specified in the input key. The type of the scene traversal that
/// leads to the provider object is specified with the provider resolution mode
/// in the input key. The provider object and computation name are used to
/// construct output keys for Exec_OutputProvidingCompilationTasks, which are
/// then kicked off to populate the source masked outputs.
class Exec_InputResolvingCompilationTask : public Exec_CompilationTask
{
public:
    Exec_InputResolvingCompilationTask(
        Exec_CompilationState &compilationState,
        const Exec_InputKey &inputKey,
        const EsfObject &originObject,
        TfSmallVector<VdfMaskedOutput, 1> *resultOutputs,
        EsfJournal *journal) :
        Exec_CompilationTask(compilationState),
        _inputKey(inputKey),
        _originObject(originObject),
        _journal(journal),
        _resultOutputs(resultOutputs)
    {}

private:
    void _Compile(
        Exec_CompilationState &compilationState,
        TaskStages &taskStages) override;

    // The input key to resolve to output keys providing said input value.
    const Exec_InputKey _inputKey;

    // The scene object at which the scene traversal is started for the
    // specified provider resolution mode.
    const EsfObject &_originObject;

    // The journal that records the traversal performed by the resolution.
    EsfJournal *_journal;

    // The output keys populated as a result of the scene traversal.
    Exec_OutputKeyVector _outputKeys;

    // Pointer to the resulting masked outputs to be populated by this task.
    TfSmallVector<VdfMaskedOutput, 1> *const _resultOutputs;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif