//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/exec/systemDiagnostics.h"

#include "pxr/exec/exec/program.h"

#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

ExecSystem::Diagnostics::Diagnostics(ExecSystem *const system) :
    _system(system)
{
    TF_VERIFY(system);
}

void
ExecSystem::Diagnostics::InvalidateAll()
{
    _system->_InvalidateAll();
}

void
ExecSystem::Diagnostics::GraphNetwork(const char *filename) const
{
    _system->_program->GraphNetwork(filename);
}

PXR_NAMESPACE_CLOSE_SCOPE
