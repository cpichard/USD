//
// Copyright 2023 Pixar
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
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#include "hdPrman/rileyShadingNodeSchema.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/trace/trace.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdPrmanRileyShadingNodeSchemaTokens,
    HD_PRMAN_RILEY_SHADING_NODE_SCHEMA_TOKENS);

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

HdTokenDataSourceHandle
HdPrmanRileyShadingNodeSchema::GetType()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return _GetTypedDataSource<HdTokenDataSource>(
        HdPrmanRileyShadingNodeSchemaTokens->type);
}

HdTokenDataSourceHandle
HdPrmanRileyShadingNodeSchema::GetName()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return _GetTypedDataSource<HdTokenDataSource>(
        HdPrmanRileyShadingNodeSchemaTokens->name);
}

HdTokenDataSourceHandle
HdPrmanRileyShadingNodeSchema::GetHandle()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return _GetTypedDataSource<HdTokenDataSource>(
        HdPrmanRileyShadingNodeSchemaTokens->handle);
}

HdPrmanRileyParamListSchema
HdPrmanRileyShadingNodeSchema::GetParams()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return HdPrmanRileyParamListSchema(_GetTypedDataSource<HdContainerDataSource>(
        HdPrmanRileyShadingNodeSchemaTokens->params));
}

/*static*/
HdContainerDataSourceHandle
HdPrmanRileyShadingNodeSchema::BuildRetained(
        const HdTokenDataSourceHandle &type,
        const HdTokenDataSourceHandle &name,
        const HdTokenDataSourceHandle &handle,
        const HdContainerDataSourceHandle &params
)
{
    TfToken _names[4];
    HdDataSourceBaseHandle _values[4];

    size_t _count = 0;

    if (type) {
        _names[_count] = HdPrmanRileyShadingNodeSchemaTokens->type;
        _values[_count++] = type;
    }

    if (name) {
        _names[_count] = HdPrmanRileyShadingNodeSchemaTokens->name;
        _values[_count++] = name;
    }

    if (handle) {
        _names[_count] = HdPrmanRileyShadingNodeSchemaTokens->handle;
        _values[_count++] = handle;
    }

    if (params) {
        _names[_count] = HdPrmanRileyShadingNodeSchemaTokens->params;
        _values[_count++] = params;
    }
    return HdRetainedContainerDataSource::New(_count, _names, _values);
}

HdPrmanRileyShadingNodeSchema::Builder &
HdPrmanRileyShadingNodeSchema::Builder::SetType(
    const HdTokenDataSourceHandle &type)
{
    _type = type;
    return *this;
}

HdPrmanRileyShadingNodeSchema::Builder &
HdPrmanRileyShadingNodeSchema::Builder::SetName(
    const HdTokenDataSourceHandle &name)
{
    _name = name;
    return *this;
}

HdPrmanRileyShadingNodeSchema::Builder &
HdPrmanRileyShadingNodeSchema::Builder::SetHandle(
    const HdTokenDataSourceHandle &handle)
{
    _handle = handle;
    return *this;
}

HdPrmanRileyShadingNodeSchema::Builder &
HdPrmanRileyShadingNodeSchema::Builder::SetParams(
    const HdContainerDataSourceHandle &params)
{
    _params = params;
    return *this;
}

HdContainerDataSourceHandle
HdPrmanRileyShadingNodeSchema::Builder::Build()
{
    return HdPrmanRileyShadingNodeSchema::BuildRetained(
        _type,
        _name,
        _handle,
        _params
    );
}

/*static*/
HdTokenDataSourceHandle
HdPrmanRileyShadingNodeSchema::BuildTypeDataSource(
    const TfToken &type)
{

    if (type == HdPrmanRileyShadingNodeSchemaTokens->pattern) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->bxdf) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->integrator) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->light) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->lightFilter) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->projection) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->displacement) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->sampleFilter) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    if (type == HdPrmanRileyShadingNodeSchemaTokens->displayFilter) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New(type);
        return ds;
    }
    // fallback for unknown token
    return HdRetainedTypedSampledDataSource<TfToken>::New(type);
} 

PXR_NAMESPACE_CLOSE_SCOPE