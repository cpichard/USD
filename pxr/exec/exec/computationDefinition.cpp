//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/exec/computationDefinition.h"

#include "pxr/exec/exec/callbackNode.h"
#include "pxr/exec/exec/program.h"

#include "pxr/exec/vdf/connectorSpecs.h"

#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

//
// Exec_ComputationDefinition
//

Exec_ComputationDefinition::Exec_ComputationDefinition(
    TfType resultType,
    const TfToken &computationName)
    : _resultType(resultType)
    , _computationName(computationName)
{        
}

Exec_ComputationDefinition::~Exec_ComputationDefinition() = default;

TfType
Exec_ComputationDefinition::GetResultType(
    const EsfObjectInterface &,
    EsfJournal *) const
{
    return _resultType;
}

//
// Exec_PluginComputationDefinition
//

Exec_PluginComputationDefinition::Exec_PluginComputationDefinition(
    TfType resultType,
    const TfToken &computationName,
    ExecCallbackFn &&callback,
    Exec_InputKeyVector &&inputKeys)
    : Exec_ComputationDefinition(
        resultType,
        computationName)
    , _callback(std::move(callback))
    , _inputKeys(std::move(inputKeys))
{
}

Exec_PluginComputationDefinition::~Exec_PluginComputationDefinition() = default;

Exec_InputKeyVector
Exec_PluginComputationDefinition::GetInputKeys(
    const EsfObjectInterface &,
    EsfJournal *) const
{
    return _inputKeys;
}

VdfNode *
Exec_PluginComputationDefinition::CompileNode(
    const EsfObjectInterface &providerObject,
    EsfJournal *const nodeJournal,
    Exec_Program *const program) const
{
    if (!nodeJournal) {
        TF_CODING_ERROR("Null nodeJournal");
        return nullptr;
    }
    if (!program) {
        TF_CODING_ERROR("Null program");
        return nullptr;
    }

    VdfInputSpecs inputSpecs;
    inputSpecs.Reserve(_inputKeys.size());
    for (const Exec_InputKey &inputKey : _inputKeys) {
        inputSpecs.ReadConnector(inputKey.resultType, inputKey.inputName);
    }

    VdfOutputSpecs outputSpecs;
    outputSpecs.Connector(
        GetResultType(providerObject, nodeJournal),
        VdfTokens->out);

    return program->CreateNode<Exec_CallbackNode>(
        *nodeJournal,
        inputSpecs,
        outputSpecs,
        _callback);
}

PXR_NAMESPACE_CLOSE_SCOPE
