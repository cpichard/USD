//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/sdf/usdFileFormat.h"
#include "pxr/usd/sdf/usdaFileFormat.h"
#include "pxr/usd/sdf/usdcFileFormat.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"

#include "pxr/base/trace/trace.h"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"

#include "crateData.h"

PXR_NAMESPACE_OPEN_SCOPE


using std::string;

TF_DECLARE_WEAK_AND_REF_PTRS(Sdf_CrateData);

TF_DEFINE_PUBLIC_TOKENS(SdfUsdFileFormatTokens, SDF_USD_FILE_FORMAT_TOKENS);

TF_DEFINE_ENV_SETTING(
    USD_DEFAULT_FILE_FORMAT, "usdc",
    "Default file format for new .usd files; either 'usda' or 'usdc'.");

// ------------------------------------------------------------

static
SdfFileFormatConstPtr
_GetFileFormat(const TfToken& formatId)
{
    const SdfFileFormatConstPtr fileFormat = SdfFileFormat::FindById(formatId);
    TF_VERIFY(fileFormat);
    return fileFormat;
}

static
const SdfUsdcFileFormatConstPtr&
_GetUsdcFileFormat()
{
    static const auto usdcFormat = TfDynamic_cast<SdfUsdcFileFormatConstPtr>(
        _GetFileFormat(SdfUsdcFileFormatTokens->Id));
    return usdcFormat;
}

static
const SdfUsdaFileFormatConstPtr&
_GetUsdaFileFormat()
{
    static const auto usdaFormat = TfDynamic_cast<SdfUsdaFileFormatConstPtr>(
        _GetFileFormat(SdfUsdaFileFormatTokens->Id));
    return usdaFormat;
}

// A .usd file may actually be either a text .usda file or a binary crate 
// .usdc file. This function returns the appropriate file format for a
// given data object.
static
SdfFileFormatConstPtr
_GetUnderlyingFileFormat(const SdfAbstractDataConstPtr& data)
{
    // A .usd file can only be backed by one of these formats,
    // so check each one individually.
    if (TfDynamic_cast<const Sdf_CrateDataConstPtr>(data)) {
        return _GetFileFormat(SdfUsdcFileFormatTokens->Id);
    }

    if (TfDynamic_cast<const SdfDataConstPtr>(data)) {
        return _GetFileFormat(SdfUsdaFileFormatTokens->Id);
    }
    
    return SdfFileFormatConstPtr();
}

// Returns the default underlying file format for a .usd file.
static
SdfFileFormatConstPtr
_GetDefaultFileFormat()
{
    TfToken defaultFormatId(TfGetEnvSetting(USD_DEFAULT_FILE_FORMAT));
    if (defaultFormatId != SdfUsdaFileFormatTokens->Id
        && defaultFormatId != SdfUsdcFileFormatTokens->Id) {
        TF_WARN("Default file format '%s' set in USD_DEFAULT_FILE_FORMAT "
                "must be either 'usda' or 'usdc'. Falling back to 'usdc'", 
                defaultFormatId.GetText());
        defaultFormatId = SdfUsdcFileFormatTokens->Id;
    }

    SdfFileFormatConstPtr defaultFormat = _GetFileFormat(defaultFormatId);
    TF_VERIFY(defaultFormat);
    return defaultFormat;
}

// Returns the 'format' argument token corresponding to the given
// file format.
static
TfToken
_GetFormatArgumentForFileFormat(const SdfFileFormatConstPtr& fileFormat)
{
    TfToken formatArg = fileFormat ? fileFormat->GetFormatId() : TfToken();
    TF_VERIFY(formatArg == SdfUsdaFileFormatTokens->Id ||
              formatArg == SdfUsdcFileFormatTokens->Id,
              "Unhandled file format '%s'",
              fileFormat ? formatArg.GetText() : "<null>");
    return formatArg;
}

// Returns the file format associated with the given arguments, or nullptr.
static
SdfFileFormatConstPtr
_GetFileFormatForArguments(const SdfFileFormat::FileFormatArguments& args)
{
    auto it = args.find(SdfUsdFileFormatTokens->FormatArg.GetString());
    if (it != args.end()) {
        const std::string& format = it->second;
        if (format == SdfUsdaFileFormatTokens->Id) {
            return _GetFileFormat(SdfUsdaFileFormatTokens->Id);
        }
        else if (format == SdfUsdcFileFormatTokens->Id) {
            return _GetFileFormat(SdfUsdcFileFormatTokens->Id);
        }
        TF_CODING_ERROR("'%s' argument was '%s', must be '%s' or '%s'. "
                        "Defaulting to '%s'.", 
                        SdfUsdFileFormatTokens->FormatArg.GetText(),
                        format.c_str(),
                        SdfUsdaFileFormatTokens->Id.GetText(),
                        SdfUsdcFileFormatTokens->Id.GetText(),
                        _GetFormatArgumentForFileFormat(
                            _GetDefaultFileFormat()).GetText());
    }
    return TfNullPtr;
}

// ------------------------------------------------------------

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(SdfUsdFileFormat, SdfFileFormat);
}

SdfUsdFileFormat::SdfUsdFileFormat()
    : SdfFileFormat(SdfUsdFileFormatTokens->Id,
                    SdfUsdFileFormatTokens->Version,
                    SdfUsdFileFormatTokens->Target,
                    SdfUsdFileFormatTokens->Id)
{
}

SdfUsdFileFormat::~SdfUsdFileFormat()
{
}

SdfAbstractDataRefPtr
SdfUsdFileFormat::InitData(const FileFormatArguments& args) const
{
    SdfFileFormatConstPtr fileFormat = _GetFileFormatForArguments(args);
    if (!fileFormat) {
        fileFormat = _GetDefaultFileFormat();
    }
    
    return fileFormat->InitData(args);
}

SdfAbstractDataRefPtr
SdfUsdFileFormat::_InitDetachedData(const FileFormatArguments& args) const
{
    SdfFileFormatConstPtr fileFormat = _GetFileFormatForArguments(args);
    if (!fileFormat) {
        fileFormat = _GetDefaultFileFormat();
    }
    
    return fileFormat->InitDetachedData(args);
}

bool
SdfUsdFileFormat::CanRead(const string& filePath) const
{
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(filePath));
    return asset &&
        (_GetUsdcFileFormat()->_CanReadFromAsset(filePath, asset) ||
         _GetUsdaFileFormat()->_CanReadFromAsset(filePath, asset));
}

bool
SdfUsdFileFormat::Read(
    SdfLayer* layer,
    const string& resolvedPath,
    bool metadataOnly) const
{
    TRACE_FUNCTION();

    return _ReadHelper</* Detached = */ false>(
        layer, resolvedPath, metadataOnly);
}

bool
SdfUsdFileFormat::_ReadDetached(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    TRACE_FUNCTION();

    return _ReadHelper</* Detached = */ true>(
        layer, resolvedPath, metadataOnly);
}

template <bool Detached>
bool 
SdfUsdFileFormat::_ReadHelper(
    SdfLayer* layer,
    const string& resolvedPath,
    bool metadataOnly) const
{
    // Fetch the asset from Ar.
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
        return false;
    }

    const auto& usdcFileFormat = _GetUsdcFileFormat();
    const auto& usdaFileFormat = _GetUsdaFileFormat();

    // Network-friendly path -- just try to read the file and if we get one that
    // works we're good.
    //
    // Try binary usdc format first, since that's most common, then usda text.
    {
        TfErrorMark m;
        if (usdcFileFormat->_ReadFromAsset(
                layer, resolvedPath, asset, metadataOnly, Detached)) {
            return true;
        }
        m.Clear();

        if (usdaFileFormat->_ReadFromAsset(
                layer, resolvedPath, asset, metadataOnly)) {
            return true;
        }
        m.Clear();
    }

    // Failed to load.  Do the slower (for the network) version where we attempt
    // to determine the underlying format first, and then load using it. This
    // gives us better diagnostic messages.
    if (usdcFileFormat->_CanReadFromAsset(resolvedPath, asset)) {
        return usdcFileFormat->_ReadFromAsset(
            layer, resolvedPath, asset, metadataOnly, Detached);
    }

    if (usdaFileFormat->_CanReadFromAsset(resolvedPath, asset)) {
        return usdaFileFormat->_ReadFromAsset(
            layer, resolvedPath, asset, metadataOnly);
    }

    return false;
}

SdfFileFormatConstPtr 
SdfUsdFileFormat::_GetUnderlyingFileFormatForLayer(
    const SdfLayer& layer)
{
    auto underlyingFileFormat = _GetUnderlyingFileFormat(_GetLayerData(layer));
    return underlyingFileFormat ?
        underlyingFileFormat : _GetDefaultFileFormat();
}

/* static */
TfToken
SdfUsdFileFormat::GetUnderlyingFormatForLayer(const SdfLayer& layer)
{
    if (layer.GetFileFormat()->GetFormatId() != SdfUsdFileFormatTokens->Id)
        return TfToken();

    auto fileFormat = _GetUnderlyingFileFormatForLayer(layer);
    return _GetFormatArgumentForFileFormat(fileFormat);
}

bool
SdfUsdFileFormat::WriteToFile(
    const SdfLayer& layer,
    const std::string& filePath,
    const std::string& comment,
    const FileFormatArguments& args) const
{
    // If a specific underlying file format is requested via the file format
    // arguments, just use that.
    SdfFileFormatConstPtr fileFormat = _GetFileFormatForArguments(args);

    // When exporting to a .usd layer (i.e., calling SdfLayer::Export), we use
    // the default underlying format for .usd. This ensures consistent behavior
    // -- creating a new .usd layer always uses the default format unless
    // otherwise specified.
    if (!fileFormat) {
        fileFormat = _GetDefaultFileFormat();
    }

    return fileFormat->WriteToFile(layer, filePath, comment, args);
}

bool
SdfUsdFileFormat::SaveToFile(
    const SdfLayer& layer,
    const std::string& filePath,
    const std::string& comment,
    const FileFormatArguments& args) const
{
    // If we are saving a .usd layer (i.e., calling SdfLayer::Save), we want to
    // maintain that layer's underlying format. For example, calling Save() on a
    // text .usd file should produce a text file and not convert it to binary.
    // 
    SdfFileFormatConstPtr fileFormat = _GetUnderlyingFileFormatForLayer(layer);

    if (!fileFormat) {
        fileFormat = _GetDefaultFileFormat();
    }

    return fileFormat->SaveToFile(layer, filePath, comment, args);
}

bool 
SdfUsdFileFormat::ReadFromString(
    SdfLayer* layer,
    const std::string& str) const
{
    return _GetUnderlyingFileFormatForLayer(*layer)
        ->ReadFromString(layer, str);
}

bool 
SdfUsdFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    return _GetUnderlyingFileFormatForLayer(layer)
        ->WriteToString(layer, str, comment);
}

bool
SdfUsdFileFormat::WriteToStream(
    const SdfSpecHandle &spec,
    std::ostream& out,
    size_t indent) const
{
    return _GetUnderlyingFileFormatForLayer(
        *get_pointer(spec->GetLayer()))->WriteToStream(
            spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
