//
// Copyright 2016 Pixar
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

#include "pxr/pxr.h"
#include "pxr/usd/pcp/propertyIndex.h"
#include "pxr/usd/pcp/primIndex.h"
#include "pxr/usd/pcp/cache.h"

#include "pxr/base/tf/pyResultConversions.h"

#include <boost/python/class.hpp>
#include <boost/python/def.hpp>

using namespace boost::python;

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

static SdfPropertySpecHandleVector
_WrapPropertyStack(const PcpPropertyIndex& propIndex)
{
    const PcpPropertyRange range = propIndex.GetPropertyRange();
    return SdfPropertySpecHandleVector(range.first, range.second);
}

static SdfPropertySpecHandleVector
_WrapLocalPropertyStack(const PcpPropertyIndex& propIndex)
{
    const PcpPropertyRange range = 
        propIndex.GetPropertyRange(/* localOnly= */ true);
    return SdfPropertySpecHandleVector(range.first, range.second);
}

static boost::python::tuple
_WrapBuildPrimPropertyIndex(
    const SdfPath &path, const PcpCache &cache, const PcpPrimIndex &primIndex)
{
    PcpErrorVector errors;
    PcpPropertyIndex propIndex;
    PcpBuildPrimPropertyIndex(path, cache, primIndex, &propIndex, &errors);

    return boost::python::make_tuple(
        boost::python::object(propIndex), errors);
}

} // anonymous namespace 

void
wrapPropertyIndex()
{
    typedef PcpPropertyIndex This;

    def("BuildPrimPropertyIndex", _WrapBuildPrimPropertyIndex);

    class_<This>
        ("PropertyIndex", "", no_init)
        .add_property("propertyStack", _WrapPropertyStack)
        .add_property("localPropertyStack", _WrapLocalPropertyStack)
        .add_property("localErrors", 
                      make_function(&This::GetLocalErrors,
                                    return_value_policy<TfPySequenceToList>()))
        ;
}
