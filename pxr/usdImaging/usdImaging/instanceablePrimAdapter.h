//
// Copyright 2024 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef PXR_USD_IMAGING_USD_IMAGING_INSTANCEABLE_PRIM_ADAPTER_H
#define PXR_USD_IMAGING_USD_IMAGING_INSTANCEABLE_PRIM_ADAPTER_H

#include "pxr/usdImaging/usdImaging/api.h"
#include "pxr/usdImaging/usdImaging/primAdapter.h"

#include "pxr/usd/sdf/path.h"

#include "pxr/pxr.h"

PXR_NAMESPACE_OPEN_SCOPE

/// An abstract adapter class for prims that are instanceable. Adapters for
/// instanceable prims should derive from this class instead of
/// UsdImaginggPrimAdapter.
class UsdImagingInstanceablePrimAdapter : public UsdImagingPrimAdapter
{
public:
    using BaseAdapter = UsdImagingPrimAdapter;

protected:
    friend class UsdImagingInstanceAdapter;
    friend class UsdImagingPointInstancerAdapter;
    // ---------------------------------------------------------------------- //
    /// \name Utility
    // ---------------------------------------------------------------------- //
    
    // Given the USD path for a prim of this adapter's type, returns
    // the prim's Hydra cache path.
    USDIMAGING_API
    SdfPath
    ResolveCachePath(
        const SdfPath& usdPath,
        const UsdImagingInstancerContext*
            instancerContext = nullptr) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_IMAGING_USD_IMAGING_INSTANCEABLE_PRIM_ADAPTER_H
