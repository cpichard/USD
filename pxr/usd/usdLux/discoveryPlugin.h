//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_USDLUX_DISCOVERY_PLUGIN_H
#define PXR_USD_USDLUX_DISCOVERY_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/usd/usdLux/api.h"

#include "pxr/usd/sdr/declare.h"
#include "pxr/usd/sdr/discoveryPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdLux_DiscoveryPlugin
///
/// Discovers nodes for corresponding concrete light types that are defined in 
/// the UsdLux library.
///
class UsdLux_DiscoveryPlugin : public SdrDiscoveryPlugin {
public:
    USDLUX_API
    UsdLux_DiscoveryPlugin() = default;

    USDLUX_API
    ~UsdLux_DiscoveryPlugin() override = default;
    
    USDLUX_API
    virtual SdrShaderNodeDiscoveryResultVec DiscoverShaderNodes(
        const Context &context) override;

    USDLUX_API
    virtual const SdrStringVec& GetSearchURIs() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_USDLUX_DISCOVERY_PLUGIN_H
