//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_EXEC_EXEC_USD_SYSTEM_H
#define PXR_EXEC_EXEC_USD_SYSTEM_H

/// \file

#include "pxr/pxr.h"

#include "pxr/exec/execUsd/api.h"

#include "pxr/base/tf/declarePtrs.h"
#include "pxr/exec/exec/request.h"
#include "pxr/exec/exec/system.h"

#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(UsdStage);

class ExecUsdCacheView;
class ExecUsdRequest;
class ExecUsdValueKey;

/// The implementation of a system to procedurally compute values based on USD
/// scene description and computation definitions.
/// 
/// ExecUsdSystem specializes the base ExecSystem class and owns USD-specific
/// structures and logic necessary to compile, schedule and evaluate requested
/// computation values.
/// 
class ExecUsdSystem : public ExecSystem
{
public:
    EXECUSD_API
    explicit ExecUsdSystem(const UsdStageConstRefPtr &stage);

    // Systems are non-copyable and non-movable to simplify management of
    // back-pointers.
    // 
    ExecUsdSystem(const ExecUsdSystem &) = delete;
    ExecUsdSystem& operator=(const ExecUsdSystem &) = delete;

    EXECUSD_API
    ~ExecUsdSystem();

    /// Builds a request for the given \p valueKeys.
    ///
    /// The optionally provided \p invalidationCallback will be invoked when
    /// previously computed value keys become invalid as a result of authored
    /// value changes or structural invalidation of the scene. If multiple
    /// value keys become invalid at the same time, they may be batched into a
    /// single invocation of the callback.
    /// 
    /// \note
    /// The \p invalidationCallback is only guaranteed to be invoked at
    /// least once per invalid value key and invalid time interval combination,
    /// and only after CacheValues() has been called. If clients want to be
    /// notified of future invalidation, they must call CacheValues() again to
    /// renew their interest in the computed value keys.
    /// 
    /// \note
    /// The client must not call into execution (including, but not
    /// limited to CacheValues() or value extraction) from within the
    /// \p invalidationCallback.
    /// 
    EXECUSD_API
    ExecUsdRequest BuildRequest(
        std::vector<ExecUsdValueKey> &&valueKeys,
        const ExecRequestIndexedInvalidationCallback &invalidationCallback =
            ExecRequestIndexedInvalidationCallback());

    /// Prepares a given \p request for execution.
    /// 
    /// This ensures the exec network is compiled and scheduled for the value
    /// keys in the request. CacheValues() will implicitly prepare the request
    /// if needed, but calling PrepareRequest() separately enables clients to
    /// front-load compilation and scheduling cost.
    ///
    EXECUSD_API
    void PrepareRequest(const ExecUsdRequest &request);

    /// Executes the given \p request and returns a cache view for extracting
    /// the computed values.
    /// 
    /// This implicitly calls PrepareRequest(), though clients may choose to
    /// call PrepareRequest() ahead of time and front-load the associated
    /// compilation and scheduling cost.
    ///
    EXECUSD_API
    ExecUsdCacheView CacheValues(const ExecUsdRequest &request);

private:
    // This object to subscribes to scene changes on the UsdStage and delivers
    // those changes to the base ExecSystem.
    class _NoticeListener;
    std::unique_ptr<_NoticeListener> _noticeListener;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
