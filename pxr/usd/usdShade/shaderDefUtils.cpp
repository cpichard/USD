//
// Copyright 2018 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdShade/shaderDefUtils.h"

#include "pxr/usd/usdShade/shader.h"

#include "pxr/base/tf/stringUtils.h"

#include "pxr/usd/ar/resolver.h"

#include "pxr/usd/sdr/filesystemDiscoveryHelpers.h"

#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"

#include "pxr/usd/sdr/shaderMetadataHelpers.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/usd/sdr/shaderProperty.h"

#include <cctype>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    /* property metadata */
    (primvarProperty)
    (defaultInput)
    (implementationName)
);

/* static */
SdrShaderNodeDiscoveryResultVec 
UsdShadeShaderDefUtils::GetDiscoveryResults(
    const UsdShadeShader &shaderDef,
    const std::string &sourceUri)
{
    SdrShaderNodeDiscoveryResultVec result;

    // Implementation source must be sourceAsset for the shader to represent 
    // nodes in Sdr.
    if (shaderDef.GetImplementationSource() != UsdShadeTokens->sourceAsset)
        return result;

    const UsdPrim shaderDefPrim = shaderDef.GetPrim();
    const TfToken &identifier = shaderDefPrim.GetName();

    // Get the family name, shader name and version information from the 
    // identifier.
    TfToken family;
    TfToken name; 
    SdrVersion version; 
    if (!SdrFsHelpersSplitShaderIdentifier(shaderDefPrim.GetName(), 
                &family, &name, &version)) {
        // A warning has already been issued by SplitShaderIdentifier.
        return result;
    }
    
    static const std::string infoNamespace("info:");
    static const std::string baseSourceAsset(":sourceAsset");

    // This vector will contain all the info:*:sourceAsset properties.
    std::vector<UsdProperty> sourceAssetProperties = 
        shaderDefPrim.GetAuthoredProperties(
        [](const TfToken &propertyName) {
            const std::string &propertyNameStr = propertyName.GetString();
            return TfStringStartsWith(propertyNameStr, infoNamespace) &&
                    TfStringEndsWith(propertyNameStr, baseSourceAsset);
        });

    const TfToken discoveryType(ArGetResolver().GetExtension(sourceUri));

    for (auto &prop : sourceAssetProperties) {
        UsdAttribute attr = prop.As<UsdAttribute>();
        if (!attr) {
            continue;
        }

        SdfAssetPath sourceAssetPath;
        if (attr.Get(&sourceAssetPath) && 
            !sourceAssetPath.GetAssetPath().empty()) {
                
            auto nameTokens = 
                    SdfPath::TokenizeIdentifierAsTokens(attr.GetName());
            if (nameTokens.size() != 3) {
                continue;
            }

            const std::string &resolvedUri = sourceAssetPath.GetResolvedPath();

            // Create a discoveryResult only if the referenced sourceAsset
            // can be resolved. 
            // XXX: Should we do this regardless and expect the parser to be 
            // able to resolve the unresolved asset path?
            if (!resolvedUri.empty()) {
                const TfToken &sourceType = nameTokens[1];

                // Use the prim name as the identifier since it is 
                // guaranteed to be unique in the file. 
                // Use the shader id as the name of the shader.
                result.emplace_back(
                    identifier,
                    version.GetAsDefault(),
                    name,
                    family, 
                    discoveryType,
                    sourceType,
                    /* uri */ sourceUri, 
                    /* resolvedUri */ sourceUri);
            } else {
                TF_WARN("Unable to resolve info:sourceAsset <%s> with value "
                    "@%s@.", attr.GetPath().GetText(), 
                    sourceAssetPath.GetAssetPath().c_str());
            }
        }
    }

    return result;
}

// Called within _GetShaderPropertyTypeAndArraySize in order to fix up the
// default value's type if it was originally a token type because Sdr doesn't
// support token types
static
void
_ConformStringTypeDefaultValue(
    const SdfValueTypeName& typeName,
    VtValue* defaultValue)
{
    // If the SdrPropertyType would be string but the SdfTypeName is token,
    // we need to convert the default value's type from token to string so that
    // there is no inconsistency between the property type and the default value
    if (defaultValue && !defaultValue->IsEmpty()) {
        if (typeName == SdfValueTypeNames->Token) {
            if (defaultValue->IsHolding<TfToken>()) {
                const TfToken& tokenVal =
                    defaultValue->UncheckedGet<TfToken>();
                *defaultValue = VtValue(tokenVal.GetString());
            }
        } else if (typeName == SdfValueTypeNames->TokenArray) {
            if (defaultValue->IsHolding< VtArray<TfToken> >()) {
                const VtArray<TfToken>& tokenVals =
                    defaultValue->UncheckedGet< VtArray<TfToken> >();
                VtStringArray stringVals;
                stringVals.reserve(tokenVals.size());
                for (const TfToken& tokenVal : tokenVals) {
                    stringVals.push_back(tokenVal.GetString());
                }
                *defaultValue = VtValue::Take(stringVals);
            }
        }
    }
}

// Called within _GetShaderPropertyTypeAndArraySize in order to fix up the
// default value's type if it was originally a bool type because Sdr doesn't
// support bool types
static
void
_ConformIntTypeDefaultValue(
    const SdfValueTypeName& typeName,
    VtValue* defaultValue)
{
    // If the SdrPropertyType would be int but the SdfTypeName is bool,
    // we need to convert the default value's type from bool to int so that
    // there is no inconsistency between the property type and the default value
    if (defaultValue && !defaultValue->IsEmpty()) {
        if (typeName == SdfValueTypeNames->Bool) {
            if (defaultValue->IsHolding<bool>()) {
                const bool& boolVal =
                    defaultValue->UncheckedGet<bool>();
                *defaultValue = VtValue(boolVal ? 1 : 0);
            }
        } else if (typeName == SdfValueTypeNames->BoolArray) {
            if (defaultValue->IsHolding< VtArray<bool> >()) {
                const VtArray<bool>& boolVals =
                    defaultValue->UncheckedGet< VtArray<bool> >();
                VtIntArray intVals;
                intVals.reserve(boolVals.size());
                for (const bool& boolVal : boolVals) {
                    intVals.push_back(boolVal ? 1 : 0);
                }
                *defaultValue = VtValue::Take(intVals);
            }
        }
    }
}

// Called within _GetShaderPropertyTypeAndArraySize in order to return the
// correct array size as determined by the given default value
static
size_t
_GetArraySize(VtValue* defaultValue) {
    if (defaultValue && !defaultValue->IsEmpty()
        && defaultValue->IsArrayValued()) {
        return defaultValue->GetArraySize();
    }
    return 0;
}

// This function is called to determine a shader property's type and array size,
// and it will also conform the default value to the correct type if needed
static 
std::pair<TfToken, size_t>
_GetShaderPropertyTypeAndArraySize(
    const SdfValueTypeName &typeName,
    const SdrTokenMap& metadata,
    VtValue* defaultValue)
{
    // XXX Note that the shaderDefParser does not currently parse 'struct' or
    //     'vstruct' types.
    //     Structs are not supported in USD but are allowed as an Sdr property
    //     type. Vstructs are not parsed at the moment because we have no need
    //     for them in USD-backed shaders currently.

    // Determine SdrPropertyType from metadata first, since metadata can
    // override the type dictated otherwise by the SdfValueTypeName
    if (ShaderMetadataHelpers::IsPropertyATerminal(metadata)) {
        return std::make_pair(SdrPropertyTypes->Terminal,
                              _GetArraySize(defaultValue));
    }

    // Determine SdrPropertyType from given SdfValueTypeName
    if (typeName == SdfValueTypeNames->Int ||
        typeName == SdfValueTypeNames->IntArray ||
        typeName == SdfValueTypeNames->Bool ||
        typeName == SdfValueTypeNames->BoolArray) {
        _ConformIntTypeDefaultValue(typeName, defaultValue);
        return std::make_pair(SdrPropertyTypes->Int,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->String ||
               typeName == SdfValueTypeNames->Token ||
               typeName == SdfValueTypeNames->Asset || 
               typeName == SdfValueTypeNames->StringArray || 
               typeName == SdfValueTypeNames->TokenArray || 
               typeName == SdfValueTypeNames->AssetArray) {
        _ConformStringTypeDefaultValue(typeName, defaultValue);
        return std::make_pair(SdrPropertyTypes->String,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Float || 
               typeName == SdfValueTypeNames->FloatArray) {
        return std::make_pair(SdrPropertyTypes->Float,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Float2 || 
               typeName == SdfValueTypeNames->Float2Array) {
        return std::make_pair(SdrPropertyTypes->Float, 2);
    } else if (typeName == SdfValueTypeNames->Float3 || 
               typeName == SdfValueTypeNames->Float3Array) {
        return std::make_pair(SdrPropertyTypes->Float, 3);
    } else if (typeName == SdfValueTypeNames->Float4 || 
               typeName == SdfValueTypeNames->Float4Array) {
        return std::make_pair(SdrPropertyTypes->Float, 4);
    } else if (typeName == SdfValueTypeNames->Color3f || 
               typeName == SdfValueTypeNames->Color3fArray) {
        return std::make_pair(SdrPropertyTypes->Color,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Color4f || 
               typeName == SdfValueTypeNames->Color4fArray) {
        return std::make_pair(SdrPropertyTypes->Color4,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Point3f || 
               typeName == SdfValueTypeNames->Point3fArray) {
        return std::make_pair(SdrPropertyTypes->Point,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Vector3f || 
               typeName == SdfValueTypeNames->Vector3fArray) {
        return std::make_pair(SdrPropertyTypes->Vector,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Normal3f|| 
               typeName == SdfValueTypeNames->Normal3fArray) {
        return std::make_pair(SdrPropertyTypes->Normal,
                              _GetArraySize(defaultValue));
    } else if (typeName == SdfValueTypeNames->Matrix4d || 
               typeName == SdfValueTypeNames->Matrix4dArray) {
        return std::make_pair(SdrPropertyTypes->Matrix,
                              _GetArraySize(defaultValue));
    } else {
        TF_RUNTIME_ERROR("Shader property has unsupported type '%s'", 
            typeName.GetAsToken().GetText());
        return std::make_pair(SdrPropertyTypes->Unknown, 0);
    }
}

template <class ShaderProperty>
static
SdrShaderPropertyUniquePtr
_CreateSdrShaderProperty(
    const ShaderProperty& shaderProperty,
    bool isOutput,
    const VtValue& shaderDefaultValue,
    const SdrTokenMap& shaderMetadata)
{
    const std::string propName = shaderProperty.GetBaseName();
    VtValue defaultValue = shaderDefaultValue;
    SdrTokenMap metadata = shaderMetadata;
    SdrTokenMap hints;
    SdrOptionVec options;

    // Update metadata if string should represent a SdfAssetPath
    if (shaderProperty.GetTypeName() == SdfValueTypeNames->Asset ||
        shaderProperty.GetTypeName() == SdfValueTypeNames->AssetArray) {
        metadata[SdrPropertyMetadata->IsAssetIdentifier] = "1";
    }

    // look for sdrMetadata options field first (which overrides a schema's
    // allowedTokens list)
    auto optionsMetadata = shaderMetadata.find(SdrPropertyMetadata->Options);
    if (optionsMetadata != shaderMetadata.end()) {
        options = ShaderMetadataHelpers::OptionVecVal(
                shaderMetadata.at(SdrPropertyMetadata->Options));
    }

    if (options.empty()) {
        // No options were found in sdrMetadata, look for allowedTokens on the
        // schema attr.
        VtTokenArray attrAllowedTokens;
        shaderProperty.GetAttr().GetMetadata(SdfFieldKeys->AllowedTokens, 
                &attrAllowedTokens);
        for (const TfToken &token : attrAllowedTokens.AsConst()) {
            options.emplace_back(std::make_pair(token, TfToken()));
        }
    }

    // Since sdr types are a subset of SdfValueTypeNames, we save the original
    // types defined in the usdShadeShaderDef representation in the
    // SdrUsdDefinitionType metadata, which can then be used by
    // SdrShaderProperty::GetTypeAsSdfType.
    metadata[SdrPropertyMetadata->SdrUsdDefinitionType] = 
        shaderProperty.GetTypeName().GetAliasesAsTokens()[0].GetString();

    TfToken propertyType;
    size_t arraySize;
    std::tie(propertyType, arraySize) = _GetShaderPropertyTypeAndArraySize(
        shaderProperty.GetTypeName(), shaderMetadata, &defaultValue);

    return SdrShaderPropertyUniquePtr(new SdrShaderProperty(
            shaderProperty.GetBaseName(),
            propertyType,
            defaultValue,
            isOutput,
            arraySize,
            metadata, hints, options));
}

/*static*/
SdrShaderPropertyUniquePtrVec 
UsdShadeShaderDefUtils::GetProperties(
    const UsdShadeConnectableAPI &shaderDef)
{
    SdrShaderPropertyUniquePtrVec result;
    for (auto &shaderInput : shaderDef.GetInputs(/* onlyAuthored */ false)) {
        // Only inputs will have default value provided
        VtValue defaultValue;
        shaderInput.Get(&defaultValue);

        SdrTokenMap metadata = shaderInput.GetSdrMetadata();

        // Only inputs might have this metadata key
        auto iter = metadata.find(_tokens->defaultInput);
        if (iter != metadata.end()) {
            metadata[SdrPropertyMetadata->DefaultInput] = "1";
            metadata.erase(_tokens->defaultInput);
        }

        // Only inputs have the GetConnectability method
        metadata[SdrPropertyMetadata->Connectable] =
            shaderInput.GetConnectability() == UsdShadeTokens->interfaceOnly ?
            "0" : "1";

        auto implementationName = metadata.find(_tokens->implementationName);
        if (implementationName != metadata.end()){
            metadata[SdrPropertyMetadata->ImplementationName] = 
                implementationName->second;
            metadata.erase(implementationName);
        }

        result.emplace_back(
            _CreateSdrShaderProperty(
                /* shaderProperty */ shaderInput,
                /* isOutput */ false,
                /* shaderDefaultValue */ defaultValue,
                /* shaderMetadata */ metadata));
    }

    for (auto &shaderOutput : shaderDef.GetOutputs(/* onlyAuthored */ false)) {
        result.emplace_back(
            _CreateSdrShaderProperty(
                /* shaderProperty */ shaderOutput,
                /* isOutput */ true,
                /* shaderDefaultValue */ VtValue() ,
                /* shaderMetadata */ shaderOutput.GetSdrMetadata()));
    }

    return result;
}

/*static*/
std::string 
UsdShadeShaderDefUtils::GetPrimvarNamesMetadataString(
    const SdrTokenMap metadata,
    const UsdShadeConnectableAPI &shaderDef)
{
    // If there's an existing value in the definition, we must append to it.
    std::vector<std::string> primvarNames; 
    if (metadata.count(SdrNodeMetadata->Primvars)) {
        auto& existingValue = metadata.at(SdrNodeMetadata->Primvars);
        // Only append if it's non-empty - calling code may do, ie,
        //    metadata["primvars"] = this_function()
        // ...and on some compilers / architectures, that will result in a
        // default empty string being inserted into metadata BEFORE this
        // function is called
        if (!existingValue.empty()) {
            primvarNames.push_back(existingValue);
        }
    }

    for (auto &shdInput : shaderDef.GetInputs(/* onlyAuthored */ false)) {
        if (shdInput.HasSdrMetadataByKey(_tokens->primvarProperty)) {
            // Check if the input holds a string here and issue a warning if it 
            // doesn't.
            if (_GetShaderPropertyTypeAndArraySize(
                    shdInput.GetTypeName(),
                    shdInput.GetSdrMetadata(),
                    nullptr).first !=
                    SdrPropertyTypes->String) {
                TF_WARN("Shader input <%s> is tagged as a primvarProperty, "
                    "but isn't string-valued.", 
                    shdInput.GetAttr().GetPath().GetText());
            }

            primvarNames.push_back("$" + shdInput.GetBaseName().GetString());
        }
    }

    return TfStringJoin(primvarNames, "|");
}


PXR_NAMESPACE_CLOSE_SCOPE

