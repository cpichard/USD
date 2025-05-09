//
// Copyright 2019 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "data.h"
#include "fileFormat.h"

#include "pxr/usd/pcp/dynamicFileFormatContext.h"
#include "pxr/usd/sdf/usdaFileFormat.h"

#include <fstream>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(
    UsdDancingCubesExampleFileFormatTokens, 
    USD_DANCING_CUBES_EXAMPLE_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdDancingCubesExampleFileFormat, SdfFileFormat);
}

UsdDancingCubesExampleFileFormat::UsdDancingCubesExampleFileFormat()
    : SdfFileFormat(
        UsdDancingCubesExampleFileFormatTokens->Id,
        UsdDancingCubesExampleFileFormatTokens->Version,
        UsdDancingCubesExampleFileFormatTokens->Target,
        UsdDancingCubesExampleFileFormatTokens->Extension)
{
}

UsdDancingCubesExampleFileFormat::~UsdDancingCubesExampleFileFormat()
{
}

bool
UsdDancingCubesExampleFileFormat::CanRead(const std::string& filePath) const
{
    return true;
}

SdfAbstractDataRefPtr
UsdDancingCubesExampleFileFormat::InitData(
    const FileFormatArguments &args) const
{
    // While we have the file format arguments used to generate the layer
    // data here, this function is meant to return the data in its new layer
    // form. The Read is responsible for generating the parameter driven
    // data. This is important for muting the layer.
    return UsdDancingCubesExample_Data::New();
}

bool
UsdDancingCubesExampleFileFormat::Read(
    SdfLayer *layer,
    const std::string &resolvedPath,
    bool metadataOnly) const
{
    if (!TF_VERIFY(layer)) {
        return false;
    }

    // Recreate the procedural abstract data with its parameters extracted
    // from the file format arguments.
    const FileFormatArguments &args = layer->GetFileFormatArguments();
    SdfAbstractDataRefPtr data = InitData(args);
    UsdDancingCubesExample_DataRefPtr cubesData = 
        TfStatic_cast<UsdDancingCubesExample_DataRefPtr>(data);
    cubesData->SetParams(UsdDancingCubesExample_DataParams::FromArgs(args));
    _SetLayerData(layer, data);

    // Enforce that the layer is read only.
    layer->SetPermissionToSave(false);
    layer->SetPermissionToEdit(false);

    // We don't do anything else when we read the file as the contents aren't
    // used at all in this example. There layer's data has already been 
    // initialized from file format arguments.
    return true;
}

bool 
UsdDancingCubesExampleFileFormat::WriteToString(
    const SdfLayer &layer,
    std::string *str,
    const std::string &comment) const
{
    // Write the generated contents in usda text format.
    return SdfFileFormat::FindById(
        SdfUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdDancingCubesExampleFileFormat::WriteToStream(
    const SdfSpecHandle &spec,
    std::ostream &out,
    size_t indent) const
{
    // Write the generated contents in usda text format.
    return SdfFileFormat::FindById(
        SdfUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

void 
UsdDancingCubesExampleFileFormat::ComposeFieldsForFileFormatArguments(
    const std::string &assetPath, 
    const PcpDynamicFileFormatContext &context,
    FileFormatArguments *args,
    VtValue *contextDependencyData) const
{
    UsdDancingCubesExample_DataParams params;

    // There is one relevant metadata field that should be dictionary valued.
    // Compose this field's value and extract any param values from the 
    // resulting dictionary.
    VtValue val;
    if (context.ComposeValue(UsdDancingCubesExampleFileFormatTokens->Params, &val) && 
            val.IsHolding<VtDictionary>()) {
        params = UsdDancingCubesExample_DataParams::FromDict(
            val.UncheckedGet<VtDictionary>());
    }

    // In addition, each parameter can optionally be specified via an attribute
    // on the prim with the same name and type as the parameter. If present, the
    // attributes default value will be used as the parameter value.
    #define xx(TYPE, NAME, DEFAULT) \
    if (context.ComposeAttributeDefaultValue( \
            UsdDancingCubesExample_DataParamsTokens->NAME, &val) && \
            val.IsHolding<TYPE>()) { \
        params.NAME = val.UncheckedGet<TYPE>(); \
    }
    USD_DANCING_CUBES_EXAMPLE_DATA_PARAMS_X_FIELDS
    #undef xx       

    // Convert the entire params object to file format arguments. We always 
    // convert all parameters even if they're default as the args are part of
    // the identity of the layer.
    *args = params.ToArgs();
}

bool 
UsdDancingCubesExampleFileFormat::CanFieldChangeAffectFileFormatArguments(
    const TfToken &field,
    const VtValue &oldValue,
    const VtValue &newValue,
    const VtValue &contextDependencyData) const
{
    // Theres only one relevant field and its values should hold a dictionary.
    const VtDictionary &oldDict = oldValue.IsHolding<VtDictionary>() ?
        oldValue.UncheckedGet<VtDictionary>() : VtGetEmptyDictionary();
    const VtDictionary &newDict = newValue.IsHolding<VtDictionary>() ?
        newValue.UncheckedGet<VtDictionary>() : VtGetEmptyDictionary();

    // The dictionary values for our metadata key are not restricted as to what
    // they may contain so it's possible they may have keys that are completely
    // irrelevant to generating the this file format's parameters. Here we're 
    // demonstrating how we can do a more fine grained analysis based on this
    // fact. In some cases this can provide a better experience for users if 
    // the extra processing in this function can prevent expensive prim 
    // recompositions for changes that don't require it. But keep in mind that
    // there can easily be cases where making this function more expensive can
    // outweigh the benefits of avoiding unnecessary recompositions.

    // Compare relevant values in the old and new dictionaries.
    // If both the old and new dictionaries are empty, there's no change.
    if (oldDict.empty() && newDict.empty()) {
        return false;
    }

    // Otherwise we iterate through each possible parameter value looking for
    // any one that has a value change between the two dictionaries.
    for (const TfToken &token : UsdDancingCubesExample_DataParamsTokens->allTokens) {
        auto oldIt = oldDict.find(token);
        auto newIt = newDict.find(token);
        const bool oldValExists = oldIt != oldDict.end();
        const bool newValExists = newIt != newDict.end();

        // If param value exists in one or not the other, we have change.
        if (oldValExists != newValExists) {
            return true;
        }
        // Otherwise if it's both and the value differs, we also have a change.
        if (newValExists && oldIt->second != newIt->second) {
            return true;
        }
    }

    // None of the relevant data params changed between the two dictionaries.
    return false;
}

bool 
UsdDancingCubesExampleFileFormat::_ShouldSkipAnonymousReload() const
{
    // Anonymous layers need to be reloadable for mute/unmute of the
    // layer to work.
    return false;
}

bool UsdDancingCubesExampleFileFormat::_ShouldReadAnonymousLayers() const
{
    // Anonymous layers of this format are allowed to call Read because
    // Read doesn't read from an actual asset path. This allows payloads
    // to target anonymous layers of this format.
    return true;
}


PXR_NAMESPACE_CLOSE_SCOPE



