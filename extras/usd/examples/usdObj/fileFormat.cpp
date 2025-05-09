//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "fileFormat.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/usdaFileFormat.h"

#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/registryManager.h"

#include "stream.h"
#include "streamIO.h"
#include "translator.h"

#include <fstream>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE


using std::string;

TF_DEFINE_PUBLIC_TOKENS(
    UsdObjFileFormatTokens, 
    USDOBJ_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdObjFileFormat, SdfFileFormat);
}

UsdObjFileFormat::UsdObjFileFormat()
    : SdfFileFormat(
        UsdObjFileFormatTokens->Id,
        UsdObjFileFormatTokens->Version,
        UsdObjFileFormatTokens->Target,
        UsdObjFileFormatTokens->Id)
{
}

UsdObjFileFormat::~UsdObjFileFormat()
{
}

bool
UsdObjFileFormat::CanRead(const string& filePath) const
{
    // Could check to see if it looks like valid obj data...
    return true;
}

bool
UsdObjFileFormat::_ReadFromStream(
    SdfLayer* layer,
    std::istream &input,
    bool metadataOnly,
    string *outErr) const
{
    // Read Obj data stream.
    UsdObjStream objStream;
    if (!UsdObjReadDataFromStream(input, &objStream, outErr))
        return false;

    // Translate obj to usd schema.
    SdfLayerRefPtr objAsUsd = UsdObjTranslateObjToUsd(objStream);
    if (!objAsUsd)
        return false;
    
    // Move generated content into final layer.
    layer->TransferContent(objAsUsd);
    return true;
}

bool
UsdObjFileFormat::Read(
    SdfLayer* layer,
    const string& resolvedPath,
    bool metadataOnly) const
{
    // try and open the file
    std::ifstream fin(resolvedPath.c_str());
    if (!fin.is_open()) {
        TF_RUNTIME_ERROR("Failed to open file \"%s\"", resolvedPath.c_str());
        return false;
    }

    string error;
    if (!_ReadFromStream(layer, fin, metadataOnly, &error)) {
        TF_RUNTIME_ERROR("Failed to read OBJ from file \"%s\": %s",
                         resolvedPath.c_str(), error.c_str());
        return false;
    }
    return true;
}

bool 
UsdObjFileFormat::ReadFromString(
    SdfLayer* layer,
    const std::string& str) const
{
    string error;
    std::stringstream ss(str);
    if (!_ReadFromStream(layer, ss, /*metadataOnly=*/false, &error)) {
        TF_RUNTIME_ERROR("Failed to read OBJ data from string: %s",
                         error.c_str());
        return false;
    }
    return true;
}


bool 
UsdObjFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as a OBJ.
    return SdfFileFormat::FindById(
        SdfUsdaFileFormatTokens->Id)->WriteToString(layer, str, comment);
}

bool
UsdObjFileFormat::WriteToStream(
    const SdfSpecHandle &spec,
    std::ostream& out,
    size_t indent) const
{
    // XXX: For now, defer to the usda file format for this.  We don't support
    // writing Usd content as a OBJ.
    return SdfFileFormat::FindById(
        SdfUsdaFileFormatTokens->Id)->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE

