//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/exec/outputProvidingCompilationTask.h"

#include "pxr/exec/exec/compilationState.h"
#include "pxr/exec/exec/computationDefinition.h"
#include "pxr/exec/exec/inputResolvingCompilationTask.h"
#include "pxr/exec/exec/program.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/trace/trace.h"
#include "pxr/exec/esf/editReason.h"
#include "pxr/exec/esf/journal.h"

PXR_NAMESPACE_OPEN_SCOPE

void
Exec_OutputProvidingCompilationTask::_Compile(
    Exec_CompilationState &compilationState,
    TaskStages &taskStages)
{
    TRACE_FUNCTION();

    const Exec_ComputationDefinition *const computationDefinition =
        _outputKey.GetComputationDefinition();

    taskStages.Invoke(
    // Make sure input dependencies are fulfilled
    [this, &compilationState, computationDefinition](TaskDependencies &deps) {
        TRACE_FUNCTION_SCOPE("input tasks");

        // TODO: The node to be compiled by this task should be uncompiled when
        // the provider object is resynced. Ideally, this dependency would be
        // automatically added by looking up the computation definition, but
        // that already happened in the input resolving task. Therefore, we need
        // to explicitly add the resync entry to the node's journal.
        _nodeJournal.Add(
            _outputKey.GetProviderObject()->GetPath(nullptr),
            EsfEditReason::ResyncedObject);

        _inputKeys =
            computationDefinition->GetInputKeys(
                *_outputKey.GetProviderObject(),
                &_nodeJournal);

        _inputSources.resize(_inputKeys.size());
        _inputJournals.resize(_inputKeys.size());
        const size_t numInputKeys = _inputKeys.size();
        for (size_t i = 0; i < numInputKeys; ++i) {
            deps.NewSubtask<Exec_InputResolvingCompilationTask>(
                compilationState,
                _inputKeys[i],
                _outputKey.GetProviderObject(),
                &_inputSources[i],
                &_inputJournals[i]);
        }
    },

    // Compile and connect the node
    [this, &compilationState, computationDefinition](
        TaskDependencies &deps) {
        TRACE_FUNCTION_SCOPE("node creation");

        VdfNode *const node = computationDefinition->CompileNode(
            *_outputKey.GetProviderObject(),
            &_nodeJournal,
            compilationState.GetProgram());

        if (!TF_VERIFY(node)) {
            return;
        }

        const Exec_OutputKey::Identity keyIdentity = _outputKey.MakeIdentity();
        node->SetDebugNameCallback([keyIdentity]{
            return keyIdentity.GetDebugName();
        });

        for (size_t i = 0; i < _inputSources.size(); ++i) {
            compilationState.GetProgram()->Connect(
                _inputJournals[i],
                _inputSources[i],
                node,
                _inputKeys[i].inputName);
        }

        // Return the compiled output to the calling task.
        VdfMaskedOutput compiledOutput(node->GetOutput(), VdfMask::AllOnes(1));
        *_resultOutput = compiledOutput;

        // Then publish it to the compiled outputs cache.
        TF_VERIFY(compilationState.GetProgram()->SetCompiledOutput(
            _outputKey.MakeIdentity(), compiledOutput));

        // Then indicate that the task identified by _outputKey is done. This
        // notifies all other tasks with a dependency on this _outputKey.
        _MarkDone(_outputKey.MakeIdentity());
    }
    );
}

PXR_NAMESPACE_CLOSE_SCOPE
