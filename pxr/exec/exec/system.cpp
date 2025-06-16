//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/exec/system.h"

#include "pxr/exec/exec/authoredValueInvalidationResult.h"
#include "pxr/exec/exec/compiler.h"
#include "pxr/exec/exec/disconnectedInputsInvalidationResult.h"
#include "pxr/exec/exec/program.h"
#include "pxr/exec/exec/requestImpl.h"
#include "pxr/exec/exec/runtime.h"
#include "pxr/exec/exec/timeChangeInvalidationResult.h"

#include "pxr/base/tf/span.h"
#include "pxr/base/trace/trace.h"
#include "pxr/base/work/withScopedParallelism.h"
#include "pxr/exec/ef/time.h"

#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

ExecSystem::ExecSystem(EsfStage &&stage)
    : _stage(std::move(stage))
    , _program(std::make_unique<Exec_Program>())
    , _runtime(std::make_unique<Exec_Runtime>(
        _program->GetTimeInputNode(),
        _program->GetLeafNodeCache()))
{
    _ChangeTime(EfTime());
}

ExecSystem::~ExecSystem() = default;

void
ExecSystem::_ChangeTime(const EfTime &newTime)
{
    const auto [timeChanged, oldTime] =
        _runtime->SetTime(_program->GetTimeInputNode(), newTime);
    if (!timeChanged) {
        return;
    }

    TRACE_FUNCTION();

    // Invalidate time on the program.
    const Exec_TimeChangeInvalidationResult invalidationResult =
        _program->InvalidateTime(oldTime, newTime);

    // Invalidate the executor and send request invalidation notification.
    WorkWithScopedDispatcher(
        [&runtime = _runtime, &invalidationResult, &requests = _requests]
        (WorkDispatcher &dispatcher){
        // Invalidate values on the executor.
        dispatcher.Run([&](){
            runtime->InvalidateExecutor(invalidationResult.invalidationRequest);
        });

        // Notify all the requests of the time change. Not all the requests will
        // contain all the leaf nodes affected by the time change, and the
        // request impls are responsible for filtering the provided information.
        // 
        // TODO: Once we expect the system to contain more than a handful of
        // requests, we should do this in parallel. We might still want to
        // invoke the invalidation callbacks serially, though.
        if (!invalidationResult.invalidLeafNodes.empty()) {
            dispatcher.Run([&](){
                for (const auto &requestImpl : requests) {
                    requestImpl->DidChangeTime(invalidationResult);
                }
            });
        }
    });
}

void
ExecSystem::_InsertRequest(std::shared_ptr<Exec_RequestImpl> &&impl)
{
    _requests.push_back(std::move(impl));
}

void
ExecSystem::_CacheValues(
    const VdfSchedule &schedule,
    const VdfRequest &computeRequest)
{
    TRACE_FUNCTION();

    // Reset the accumulated uninitialized input nodes on the program, and
    // retain the invalidation request for executor invalidation below.
    VdfMaskedOutputVector invalidationRequest =
        _program->ResetUninitializedInputNodes();

    // Make sure that the executor data manager is properly invalidated for any
    // input nodes that were just initialized.
    _runtime->InvalidateExecutor(invalidationRequest);

    // Run the executor to compute the values.
    _runtime->ComputeValues(schedule, computeRequest);
}

std::vector<VdfMaskedOutput>
ExecSystem::_Compile(TfSpan<const ExecValueKey> valueKeys)
{
    Exec_Compiler compiler(_stage, _program.get(), _runtime.get());
    return compiler.Compile(valueKeys);
}

bool
ExecSystem::_HasPendingRecompilation() const
{
    return !_program->GetInputsRequiringRecompilation().empty();
}

void
ExecSystem::_InvalidateAll()
{
    TRACE_FUNCTION();

    // Reset data structures in reverse order of construction.
    _requests.clear();
    _runtime.reset();
    _program.reset();

    // Reconstruct the relevant data structures.
    _program = std::make_unique<Exec_Program>();
    _runtime = std::make_unique<Exec_Runtime>(
        _program->GetTimeInputNode(),
        _program->GetLeafNodeCache());

    // Initialize time with the default time.
    _ChangeTime(EfTime());
}

void
ExecSystem::_InvalidateDisconnectedInputs()
{
    TRACE_FUNCTION();

    Exec_DisconnectedInputsInvalidationResult invalidationResult =
        _program->InvalidateDisconnectedInputs();

    // Invalidate the executor and send request invalidation.
    WorkWithScopedDispatcher(
        [&runtime = _runtime, &invalidationResult, &requests = _requests]
        (WorkDispatcher &dispatcher){
        // Invalidate the executor data manager.
        dispatcher.Run([&](){
            runtime->InvalidateExecutor(invalidationResult.invalidationRequest);
        });

        // Invalidate values in the page cache.
        dispatcher.Run([&](){
            runtime->InvalidatePageCache(
                invalidationResult.invalidationRequest,
                EfTimeInterval::GetFullInterval());
        });

        // Notify all the requests of computed value invalidation. Not all the
        // requests will contain all the invalid leaf nodes, and the request
        // impls are responsible for filtering the provided information.
        // 
        // TODO: Once we expect the system to contain more than a handful of
        // requests, we should do this in parallel. We might still want to
        // invoke the invalidation callbacks serially, though.
        dispatcher.Run([&](){
            for (const auto &requestImpl : requests) {
                requestImpl->DidInvalidateComputedValues(invalidationResult);
            }
        });

    });
}

void
ExecSystem::_InvalidateAuthoredValues(TfSpan<const SdfPath> invalidProperties)
{
    TRACE_FUNCTION();

    const Exec_AuthoredValueInvalidationResult invalidationResult =
        _program->InvalidateAuthoredValues(invalidProperties);

    // Invalidate the executor and send request invalidation.
    WorkWithScopedDispatcher(
        [&runtime = _runtime, &invalidationResult, &requests = _requests]
        (WorkDispatcher &dispatcher){
        // If any of the inputs to exec changed to be time dependent when
        // previously they were not (or vice versa), we need to invalidate the
        // main executor's topological state, such that invalidation traversals
        // pick up the new time dependency.
        if (invalidationResult.isTimeDependencyChange) {
            dispatcher.Run([&](){
                runtime->InvalidateTopologicalState();
            });
        }

        // Invalidate values in the page cache.
        dispatcher.Run([&](){
            runtime->InvalidatePageCache(
                invalidationResult.invalidationRequest,
                invalidationResult.invalidInterval);
        });

        // Notify all the requests of computed value invalidation. Not all the
        // requests will contain all the invalid leaf nodes or invalid
        // properties, and the request impls are responsible for filtering the
        // provided information.
        // 
        // TODO: Once we expect the system to contain more than a handful of
        // requests, we should do this in parallel. We might still want to
        // invoke the invalidation callbacks serially, though.
        dispatcher.Run([&](){
            for (const auto &requestImpl : requests) {
                requestImpl->DidInvalidateComputedValues(invalidationResult);
            }
        });
    });
}

PXR_NAMESPACE_CLOSE_SCOPE
