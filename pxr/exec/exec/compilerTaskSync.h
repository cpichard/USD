//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_COMPILER_TASK_SYNC_H
#define PXR_EXEC_EXEC_COMPILER_TASK_SYNC_H

#include "pxr/pxr.h"

#include "pxr/exec/exec/compilerTaskSyncBase.h"
#include "pxr/exec/exec/outputKey.h"

#include "pxr/base/tf/hash.h"

#include <tbb/concurrent_unordered_map.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Instances of this class are used to synchronize compilation task graphs.
/// 
/// Tasks can claim dependent tasks, where each dependent task is identified by
/// \p KeyType. The result of this operation instructs the caller to run the
/// dependent task (if the caller is the first to claim the task), or to wait on
/// the dependent task (if another caller claimed it first).
/// 
/// The lifetime of instances of this class is expected to be limited to one
/// round of compilation.
///
template <class KeyType>
class Exec_CompilerTaskSync final : public Exec_CompilerTaskSyncBase
{
public:
    explicit Exec_CompilerTaskSync(WorkDispatcher &dispatcher);

    ~Exec_CompilerTaskSync();

    /// Attempts to claim the task identified by \p key, and returns whether the
    /// attempt was succesful.
    /// 
    /// This method will increment the dependency count of the \p task, if the
    /// key has already been claimed and \p task needs to wait for the results.
    /// Once the dependency is fulfilled, the \p task will be notified by
    /// decrementing its dependency count, and if it reaches zero the \p task
    /// will automatically be spawned.
    ///
    ClaimResult Claim(const KeyType &key, Exec_CompilationTask *task);

    /// Establishes that \p task depends on the task identified by \p key.
    ///
    /// Unlike Claim, if the task for \p key has not been claimed, the caller is
    /// *not* responsible for creating that task. In that case, a new waitlist
    /// is created for \p key if necessary, and the \p task is added to it.
    ///
    WaitResult WaitOn(const KeyType &key, Exec_CompilationTask *task);

    /// Marks the task associated with \p key as done.
    /// 
    /// This method will notify any tasks depending on \p key by decrementing
    /// their dependency counts, and spawning them if their dependency count
    /// reaches 0.
    ///
    /// Any \p key value can be marked done, even if \p key has never been
    /// claimed or waited on, but the same \p key cannot be marked done more
    /// than once.
    ///
    void MarkDone(const KeyType &key);

private:
    // A unique entry (containing a waitlist and status) is maintained for each
    // key.
    using _Entries = tbb::concurrent_unordered_map<KeyType, _Entry, TfHash>;
    _Entries _entries;
};

/// Synchronizes Exec_OutputProvidingCompilationTasks.
///
/// Tasks are identified by the identity of the output key being compiled.
///
using Exec_OutputProvidingTaskSync =
    Exec_CompilerTaskSync<Exec_OutputKey::Identity>;

PXR_NAMESPACE_CLOSE_SCOPE

#endif