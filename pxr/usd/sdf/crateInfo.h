//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_SDF_CRATE_INFO_H
#define PXR_USD_SDF_CRATE_INFO_H

#include "pxr/pxr.h"
#include "pxr/usd/sdf/api.h"
#include "pxr/base/tf/token.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class SdfCrateInfo
///
/// A class for introspecting the underlying qualities of .usdc 'crate' files,
/// for diagnostic purposes.
///
class SdfCrateInfo
{
public:
    struct Section {
        Section() = default;
        Section(std::string const &name, int64_t start, int64_t size)
            : name(name), start(start), size(size) {}
        std::string name;
        int64_t start = -1, size = -1;
    };

    struct SummaryStats {
        size_t numSpecs = 0;
        size_t numUniquePaths = 0;
        size_t numUniqueTokens = 0;
        size_t numUniqueStrings = 0;
        size_t numUniqueFields = 0;
        size_t numUniqueFieldSets = 0;
    };

    /// Attempt to open and read \p fileName.
    SDF_API
    static SdfCrateInfo Open(std::string const &fileName);

    /// Return summary statistics structure for this file.
    SDF_API
    SummaryStats GetSummaryStats() const;

    /// Return the named file sections, their location and sizes in the file.
    SDF_API
    std::vector<Section> GetSections() const;

    /// Return the file version.
    SDF_API
    TfToken GetFileVersion() const;
    
    /// Return the software version.
    SDF_API
    TfToken GetSoftwareVersion() const;

    /// Return true if this object refers to a valid file.
    explicit operator bool() const { return (bool)_impl; }

private:

    struct _Impl;
    std::shared_ptr<_Impl> _impl;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_SDF_CRATE_INFO_H
