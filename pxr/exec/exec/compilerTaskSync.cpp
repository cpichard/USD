//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/exec/compilerTaskSync.h"

#include "pxr/exec/exec/compilerTaskSyncBase.h"
#include "pxr/exec/exec/outputKey.h"

#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

template <class KeyType>
Exec_CompilerTaskSync<KeyType>::Exec_CompilerTaskSync(
    WorkDispatcher &dispatcher)
    : Exec_CompilerTaskSyncBase(dispatcher)
{}

template <class KeyType>
Exec_CompilerTaskSync<KeyType>::~Exec_CompilerTaskSync() = default;

template <class KeyType>
Exec_CompilerTaskSyncBase::ClaimResult
Exec_CompilerTaskSync<KeyType>::Claim(
    const KeyType &key,
    Exec_CompilationTask *task)
{
    // Add the key to the map. If another task got to claiming it first, it's
    // expected and safe for the key to already have an entry.
    const auto &[iterator, inserted] = _entries.emplace(
        std::piecewise_construct, 
            std::forward_as_tuple(key),
            std::forward_as_tuple());
    _Entry *const entry = &iterator->second;

    return _Claim(entry, task);
}

template <class KeyType>
Exec_CompilerTaskSyncBase::WaitResult
Exec_CompilerTaskSync<KeyType>::WaitOn(
    const KeyType &key,
    Exec_CompilationTask *task)
{
    // Add the key to the map. If another task got to claiming it first, it's
    // expected and safe for the key to already have an entry.
    const auto &[iterator, inserted] = _entries.emplace(
        std::piecewise_construct, 
            std::forward_as_tuple(key),
            std::forward_as_tuple());
    _Entry *const entry = &iterator->second;

    return _WaitOn(entry, task);
}

template <class KeyType>
void
Exec_CompilerTaskSync<KeyType>::MarkDone(const KeyType &key)
{
    // Get the entry for the key. If previously claimed or waited on, the entry
    // will exist. If not, we create a new entry in case a future caller
    // attempts to wait on this key.
    const auto &[iterator, inserted] = _entries.emplace(
        std::piecewise_construct, 
            std::forward_as_tuple(key),
            std::forward_as_tuple());
    _Entry *const entry = &iterator->second;
 
    _MarkDone(entry);
}

// Explicit template instantiations.
template class Exec_CompilerTaskSync<Exec_OutputKey::Identity>;

PXR_NAMESPACE_CLOSE_SCOPE
