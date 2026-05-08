//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdExecImaging/request.h"

#include "pxr/usdImaging/usdExecImaging/adapterRegistry.h"
#include "pxr/usdImaging/usdExecImaging/debugCodes.h"
#include "pxr/usdImaging/usdExecImaging/primAdapter.h"
#include "pxr/usdImaging/usdExecImaging/requestBuilderImpl.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/scopeDescription.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/trace/trace.h"
#include "pxr/exec/exec/systemDiagnostics.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"

#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

UsdExecImaging_RequestSharedPtr
UsdExecImaging_Request::New(UsdStageRefPtr stage)
{
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST, "[%s]\n", TF_FUNC_NAME().c_str());

    if (!TF_VERIFY(stage)) {
        return nullptr;
    }

    return std::shared_ptr<UsdExecImaging_Request>(
        new UsdExecImaging_Request(std::move(stage)));
}

UsdExecImaging_Request::UsdExecImaging_Request(UsdStageRefPtr stage)
    : _stage(std::move(stage))
    , _requiresRebuild(true)
    , _requiresRecompute(true)
    , _graphFileIndex(0)
{
    _system.emplace(_stage);
}

void
UsdExecImaging_Request::Refresh()
{
    TRACE_FUNCTION();
    TF_DESCRIBE_SCOPE("Refreshing UsdExecImaging request");
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST, "[%s]\n", TF_FUNC_NAME().c_str());

    if (_requiresRebuild || !_request || !_request->IsValid()) {
        _Rebuild();
    }
    if (_requiresRecompute) {
        _Recompute();
    }
}

VtValue
UsdExecImaging_Request::GetComputedValue(const UsdExecImagingValueKey &valueKey)
{
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST,
        "[%s] %s %s\n",
        TF_FUNC_NAME().c_str(),
        valueKey.providerPath.GetText(),
        valueKey.computationName.GetText());

    // The request must be ready for extraction.
    if (!TF_VERIFY(!_requiresRebuild) ||
        !TF_VERIFY(!_requiresRecompute) ||
        !TF_VERIFY(_request) ||
        !TF_VERIFY(_request->IsValid())) {
        return {};
    }

    // Extract the value key from the cache view, using the value key map to
    // determine the appropriate index. We expect the value key to be found in
    // the map.
    const auto it = _valueKeyMap.valueKeyToIndexMap.find(valueKey);
    if (!TF_VERIFY(it != _valueKeyMap.valueKeyToIndexMap.end())) {
        return {};
    }
    return _cacheView->Get(it->second);
}

HdContainerDataSourceHandle
UsdExecImaging_Request::GetPrimData(const SdfPath &primPath)
{
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST,
        "[%s] %s\n",
        TF_FUNC_NAME().c_str(),
        primPath.GetText());

    // Check if there is a prim adapter for the prim at primPath. If not, there
    // is no data source for computed values. This is not an error.
    const auto it = _valueKeyMap.primToAdapterMap.find(primPath);
    if (it == _valueKeyMap.primToAdapterMap.end()) {
        return nullptr;
    }

    // Construct a shared pointer to a UsdExecImagingRequestAccessor which holds
    // a shared reference to this request.
    UsdExecImagingRequestAccessorSharedPtr requestAccessor(
        shared_from_this(),
        static_cast<UsdExecImagingRequestAccessor *>(this));

    // Get data sources by delegating to the prim adapter. The adapter may make
    // copies of the requestAccessor, and each copy holds a shared reference to
    // this request.
    return it->second->GetPrimData(primPath, requestAccessor);
}

void
UsdExecImaging_Request::_Rebuild()
{
    TRACE_FUNCTION();
    TF_DESCRIBE_SCOPE("Rebuilding UsdExecImaging request");
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST, "[%s]\n", TF_FUNC_NAME().c_str());

    // Prim adapters use this object to add value keys to the request.
    UsdExecImaging_RequestBuilderImpl requestBuilder;

    for (const UsdPrim &prim : _stage->Traverse()) {
        // Get the adpater for this prim. If there is no adapter, then skip
        // this prim.
        const UsdExecImagingPrimAdapter *const primAdapter =
            UsdExecImaging_AdapterRegistry::GetPrimAdapter(prim);
        if (!primAdapter) {
            continue;
        }

        // Add value keys for this prim.
        TF_DEBUG_MSG(USDEXECIMAGING_REQUEST,
            "[%s] Adapting prim %s\n",
            TF_FUNC_NAME().c_str(),
            prim.GetPath().GetText());
        requestBuilder.SetAdaptedPrim(prim, *primAdapter);
        primAdapter->BuildRequest(prim, requestBuilder);
    }

    // Build the exec request.
    _request = _system->BuildRequest(requestBuilder.TakeValueKeys());

    // Save the value key map gathered by the request builder.
    _valueKeyMap = requestBuilder.TakeValueKeyMap();

    // The request no longer requires rebuilding, but must be recomputed.
    _requiresRebuild = false;
    _requiresRecompute = true;

    // If enabled, write the exec network to a file.
    if (TfDebug::IsEnabled(USDEXECIMAGING_GRAPH_AFTER_REBUILD)) {
        TF_DESCRIBE_SCOPE("Writing exec network to file");
        TRACE_FUNCTION_SCOPE("Write exec network to file");

        const std::string filename =
            TfStringPrintf("usdExecImaging_Request_%d.dot", _graphFileIndex++);

        TF_DEBUG_MSG(USDEXECIMAGING_GRAPH_AFTER_REBUILD,
            "[%s] Writing %s\n",
            TF_FUNC_NAME().c_str(),
            filename.c_str());
        
        // Ensure the request has been compiled or else the graph may be empty.
        _system->PrepareRequest(*_request);
        
        ExecSystem::Diagnostics diagnostics(&_system.value());
        diagnostics.GraphNetwork(filename.c_str());
    }
}

void
UsdExecImaging_Request::_Recompute()
{
    TRACE_FUNCTION();
    TF_DESCRIBE_SCOPE("Recomputing UsdExecImaging request");
    TF_DEBUG_MSG(USDEXECIMAGING_REQUEST, "[%s]\n", TF_FUNC_NAME().c_str());

    if (!TF_VERIFY(_request->IsValid())) {
        return;
    }

    _cacheView.emplace(_system->Compute(*_request));
    _requiresRecompute = false;
}

PXR_NAMESPACE_CLOSE_SCOPE
