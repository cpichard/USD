//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
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

#include "hdPrman/rileySampleFilterSchema.h"

#include "pxr/imaging/hd/retainedDataSource.h"

#include "pxr/base/trace/trace.h"

// --(BEGIN CUSTOM CODE: Includes)--
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdPrmanRileySampleFilterSchemaTokens,
    HD_PRMAN_RILEY_SAMPLE_FILTER_SCHEMA_TOKENS);

// --(BEGIN CUSTOM CODE: Schema Methods)--
// --(END CUSTOM CODE: Schema Methods)--

HdPrmanRileyShadingNodeVectorSchema
HdPrmanRileySampleFilterSchema::GetSampleFilter()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return HdPrmanRileyShadingNodeVectorSchema(_GetTypedDataSource<HdVectorDataSource>(
        HdPrmanRileySampleFilterSchemaTokens->sampleFilter));
}

HdPrmanRileyParamListSchema
HdPrmanRileySampleFilterSchema::GetAttributes()
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{
    return HdPrmanRileyParamListSchema(_GetTypedDataSource<HdContainerDataSource>(
        HdPrmanRileySampleFilterSchemaTokens->attributes));
}

/*static*/
HdContainerDataSourceHandle
HdPrmanRileySampleFilterSchema::BuildRetained(
        const HdVectorDataSourceHandle &sampleFilter,
        const HdContainerDataSourceHandle &attributes
)
{
    TfToken _names[2];
    HdDataSourceBaseHandle _values[2];

    size_t _count = 0;

    if (sampleFilter) {
        _names[_count] = HdPrmanRileySampleFilterSchemaTokens->sampleFilter;
        _values[_count++] = sampleFilter;
    }

    if (attributes) {
        _names[_count] = HdPrmanRileySampleFilterSchemaTokens->attributes;
        _values[_count++] = attributes;
    }
    return HdRetainedContainerDataSource::New(_count, _names, _values);
}

HdPrmanRileySampleFilterSchema::Builder &
HdPrmanRileySampleFilterSchema::Builder::SetSampleFilter(
    const HdVectorDataSourceHandle &sampleFilter)
{
    _sampleFilter = sampleFilter;
    return *this;
}

HdPrmanRileySampleFilterSchema::Builder &
HdPrmanRileySampleFilterSchema::Builder::SetAttributes(
    const HdContainerDataSourceHandle &attributes)
{
    _attributes = attributes;
    return *this;
}

HdContainerDataSourceHandle
HdPrmanRileySampleFilterSchema::Builder::Build()
{
    return HdPrmanRileySampleFilterSchema::BuildRetained(
        _sampleFilter,
        _attributes
    );
}

/*static*/
HdPrmanRileySampleFilterSchema
HdPrmanRileySampleFilterSchema::GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer)
{
    return HdPrmanRileySampleFilterSchema(
        fromParentContainer
        ? HdContainerDataSource::Cast(fromParentContainer->Get(
                HdPrmanRileySampleFilterSchemaTokens->rileySampleFilter))
        : nullptr);
}

/*static*/
const TfToken &
HdPrmanRileySampleFilterSchema::GetSchemaToken()
{
    return HdPrmanRileySampleFilterSchemaTokens->rileySampleFilter;
}

/*static*/
const HdDataSourceLocator &
HdPrmanRileySampleFilterSchema::GetDefaultLocator()
{
    static const HdDataSourceLocator locator(GetSchemaToken());
    return locator;
}

/* static */
const HdDataSourceLocator &
HdPrmanRileySampleFilterSchema::GetSampleFilterLocator()
{
    static const HdDataSourceLocator locator =
        GetDefaultLocator().Append(
            HdPrmanRileySampleFilterSchemaTokens->sampleFilter);
    return locator;
}

/* static */
const HdDataSourceLocator &
HdPrmanRileySampleFilterSchema::GetAttributesLocator()
{
    static const HdDataSourceLocator locator =
        GetDefaultLocator().Append(
            HdPrmanRileySampleFilterSchemaTokens->attributes);
    return locator;
} 

PXR_NAMESPACE_CLOSE_SCOPE