//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_EXEC_IMAGING_REQUEST_H
#define PXR_USD_IMAGING_USD_EXEC_IMAGING_REQUEST_H

/// \file

#include "pxr/pxr.h"

#include "pxr/usdImaging/usdExecImaging/requestAccessor.h"
#include "pxr/usdImaging/usdExecImaging/valueKeyMap.h"

#include "pxr/base/tf/declarePtrs.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/imaging/hd/dataSource.h"

#include <memory>
#include <optional>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_REF_PTRS(UsdStage);

/// Requests are always referred to by a shared pointer.
///
/// This allows data sources to share ownership of the underlying request.
///
using UsdExecImaging_RequestSharedPtr =
    std::shared_ptr<class UsdExecImaging_Request>;

/// The UsdExecImaging_Request manages an ExecUsdRequest used to compute values
/// for imaging.
///
/// UsdExecImaging_Request is the mediator between the scene index and the
/// exec request. It manages building, rebuilding, and computing the
/// contained ExecUsdRequest, as well as tracking the invalidation state of
/// computed values.
///
class UsdExecImaging_Request
    : public std::enable_shared_from_this<UsdExecImaging_Request> 
    , public UsdExecImagingRequestAccessor
{
public:
    static UsdExecImaging_RequestSharedPtr New(UsdStageRefPtr stage);

    /// Ensures the request is up-to-date and ready to extract computed values.
    ///
    /// This rebuilds and recomputes the request if necessary.
    ///
    void Refresh();

    // Implements ExecUsdImagingRequestAccessor.
    VtValue GetComputedValue(const UsdExecImagingValueKey &valueKey) override;

    /// Returns a container data source for \p primPath that can access computed
    /// values from this request.
    ///
    /// This function delegates to the GetPrimData method of the corresponding
    /// UsdExecImagingPrimAdapter for \p primPath. That method uses a shared
    /// pointer to a UsdExecImagingRequestAccessor to construct data sources
    /// that lazily extract values from this request. Since
    /// UsdExecImaging_Request IS_A UsdExecImagingRequestAccessor, data sources
    /// returned from this method hold shared pointers to this request.
    ///
    /// If there is no adapter for the prim at \p primPath, this returns a
    /// null data source handle.
    ///
    HdContainerDataSourceHandle GetPrimData(const SdfPath &primPath);

private:
    UsdExecImaging_Request(UsdStageRefPtr stage);

    // Traverses the stage and builds a new exec request for all adapted prims.
    void _Rebuild();

    // Recomputes the request. This potentially recompiles the network.
    void _Recompute();

private:
    UsdStageRefPtr _stage;
    std::optional<ExecUsdSystem> _system;
    std::optional<ExecUsdRequest> _request;
    std::optional<ExecUsdCacheView> _cacheView;
    UsdExecImaging_ValueKeyMap _valueKeyMap;
    bool _requiresRebuild;
    bool _requiresRecompute;
    unsigned _graphFileIndex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif