//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/usd/sdf/usdFileFormat.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/base/tf/pyStaticTokens.h"

#include "pxr/external/boost/python/bases.hpp"
#include "pxr/external/boost/python/class.hpp"
#include "pxr/external/boost/python/scope.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

void
wrapUsdFileFormat()
{
    using This = SdfUsdFileFormat;

    scope s = class_<This, bases<SdfFileFormat>, noncopyable>
        ("UsdFileFormat", no_init)

        .def("GetUnderlyingFormatForLayer", 
            &This::GetUnderlyingFormatForLayer)
        .staticmethod("GetUnderlyingFormatForLayer")
        ;

    TF_PY_WRAP_PUBLIC_TOKENS(
        "Tokens",
        SdfUsdFileFormatTokens,
        SDF_USD_FILE_FORMAT_TOKENS);
}
