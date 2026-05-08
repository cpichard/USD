//
// Copyright 2026 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usdImaging/usdExecImaging/adapterRegistry.h"

#include "pxr/usdImaging/usdExecImaging/primAdapter.h"
#include "pxr/usdImaging/usdExecImaging/xformablePrimAdapter.h"

#include "pxr/usd/usdGeom/xformable.h"

PXR_NAMESPACE_OPEN_SCOPE

UsdExecImagingPrimAdapter *
UsdExecImaging_AdapterRegistry::GetPrimAdapter(const UsdPrim &prim)
{
    // TODO: The adapter currently has a single hard-coded adapter type. This
    // will change in the future to generically handle adapters registered in
    // plugins.
    if (prim.IsA<UsdGeomXformable>()) {
        static UsdExecImaging_XformablePrimAdapter xformablePrimAdapter;
        return &xformablePrimAdapter;
    }

    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE
