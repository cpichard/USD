//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_SDF_FILE_IO_COMMON_H
#define PXR_USD_SDF_FILE_IO_COMMON_H

/// \file sdf/fileIO_Common.h 

#include "pxr/pxr.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/declareHandles.h"
#include "pxr/usd/sdf/fileIO.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerOffset.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/reference.h"
#include "pxr/usd/sdf/relationshipSpec.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/variantSetSpec.h"
#include "pxr/usd/sdf/variantSpec.h"

#include "pxr/base/ts/spline.h"
#include "pxr/base/ts/types.h"

#include "pxr/base/vt/dictionary.h"
#include "pxr/base/vt/value.h"

#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"

#include <algorithm>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

////////////////////////////////////////////////////////////////////////
// Simple FileIO Utilities

class Sdf_FileIOUtility {

public:

    // === Stream output helpers

    // Non-formatted string output
    static void Puts(Sdf_TextOutput &out,
                size_t indent, const std::string &str);

    // Printf-style formatted string output
    static void Write(Sdf_TextOutput &out,
                size_t indent, const char *fmt, ...);

    static bool OpenParensIfNeeded(Sdf_TextOutput &out,
                bool didParens, bool multiLine);

    static void CloseParensIfNeeded(Sdf_TextOutput &out,
                size_t indent, bool didParens, bool multiLine);

    static void WriteQuotedString(Sdf_TextOutput &out,
                size_t indent, const std::string &str);

    static void WriteAssetPath(Sdf_TextOutput &out,
                size_t indent, const std::string &str);

    static void WriteDefaultValue(Sdf_TextOutput &out,
                size_t indent, VtValue value);

    static void WriteSdfPath(Sdf_TextOutput &out,
                size_t indent, const SdfPath &path);

    static bool WriteNameVector(Sdf_TextOutput &out,
                size_t indent, const std::vector<std::string> &vec);
    static bool WriteNameVector(Sdf_TextOutput &out,
                size_t indent, const std::vector<TfToken> &vec);

    static bool WriteTimeSamples(Sdf_TextOutput &out,
                size_t indent, const SdfPropertySpec &);

    static void WriteSpline(Sdf_TextOutput &out,
                size_t indent, const TsSpline &spline);

    static bool WriteRelocates(Sdf_TextOutput &out,
                size_t indent, bool multiLine,
                const SdfRelocates &relocates);

    static bool WriteRelocates(Sdf_TextOutput &out,
                size_t indent, bool multiLine,
                const SdfRelocatesMap &reloMap);

    static void WriteDictionary(Sdf_TextOutput &out,
                size_t indent, bool multiLine,
                const VtDictionary &dictionary,
                bool stringValuesOnly=false);

    template <class T>
    static void WriteListOp(Sdf_TextOutput &out,
                size_t indent,
                const TfToken& fieldName,
                const SdfListOp<T>& listOp);

    static void WriteLayerOffset(Sdf_TextOutput &out,
                size_t indent, bool multiline,
                const SdfLayerOffset& offset);

    // === String production and transformation helpers

    /// Quote \p str, adding quotes before and after and escaping
    /// unprintable characters and the quote character itself.  If
    /// the string contains newlines it's quoted with triple quotes
    /// and the newlines are not escaped.
    static std::string Quote(const std::string &str);
    static std::string Quote(const TfToken &token);

    // Create a string from a value
    static std::string StringFromVtValue(const VtValue &value);

    // Convert enums to strings for use in sdf text format syntax.
    // Note that in some cases we use empty strings to represent the
    // default values of these enums.
    static const char* Stringify( SdfPermission val );
    static const char* Stringify( SdfSpecifier val );
    static const char* Stringify( SdfVariability val );
    static const char* Stringify( TsExtrapMode mode );
    static const char* Stringify( TsCurveType curveType );
    static const char* Stringify( TsInterpMode interp );
    static const char* Stringify( TsTangentAlgorithm algorithm );

private:

    // Helper types to write a VtDictionary so that its keys are ordered.
    struct _StringLessThan {
        bool operator()(const std::string *lhs, const std::string *rhs) const {
            return *lhs < *rhs;
        }
    };
    typedef std::map<const std::string *, const VtValue *, _StringLessThan>
        _OrderedDictionary;

    static void _WriteDictionary(
        Sdf_TextOutput &out,
        size_t indent, bool multiLine, _OrderedDictionary &dictionary,
        bool stringValuesOnly);
};

////////////////////////////////////////////////////////////////////////
// Helpers for determining if a field should be included in a spec's
// metadata section.

struct Sdf_IsMetadataField
{
    Sdf_IsMetadataField(const SdfSpecType specType)
        : _specDef(SdfSchema::GetInstance().GetSpecDefinition(specType))
    { }

    bool operator()(const TfToken& field) const
    { 
        // Allow fields tagged explicitly as metadata, or fields
        // that are invalid, as these may be unrecognized plugin
        // metadata fields. In this case, there may be a string
        // representation that needs to be written out.
        return (!_specDef->IsValidField(field) ||
                    _specDef->IsMetadataField(field)); 
    }

    const SdfSchema::SpecDefinition* _specDef;
};

////////////////////////////////////////////////////////////////////////

static inline bool
Sdf_WritePrim(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent);

static inline bool
Sdf_WriteAttribute(
    const SdfAttributeSpec &attr, Sdf_TextOutput &out, size_t indent);

static inline bool
Sdf_WriteRelationship(
    const SdfRelationshipSpec &rel, Sdf_TextOutput &out, size_t indent);

static bool
Sdf_WriteVariantSet(
    const SdfVariantSetSpec &spec, Sdf_TextOutput &out, size_t indent);

static bool
Sdf_WriteVariant(
    const SdfVariantSpec &variantSpec, Sdf_TextOutput &out, size_t indent);

////////////////////////////////////////////////////////////////////////

static bool
Sdf_WritePrimPreamble(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    SdfSpecifier spec = prim.GetSpecifier();
    bool writeTypeName = true;
    if (!SdfIsDefiningSpecifier(spec)) {
        // For non-defining specifiers, we write typeName only if we have
        // a setting.
        writeTypeName = prim.HasField(SdfFieldKeys->TypeName);
    }

    TfToken typeName;
    if (writeTypeName) {
        typeName = prim.GetTypeName();
        if (typeName == SdfTokens->AnyTypeToken){
            typeName = TfToken();
        }
    }

    Sdf_FileIOUtility::Write( out, indent, "%s%s%s ",
            Sdf_FileIOUtility::Stringify(spec),
            !typeName.IsEmpty() ? " " : "",
            !typeName.IsEmpty() ? typeName.GetText() : "" );
    Sdf_FileIOUtility::WriteQuotedString( out, 0, prim.GetName().c_str() );

    return true;
}

template <class ListOpType>
static bool
Sdf_WriteIfListOp(
    Sdf_TextOutput& out, size_t indent,
    const TfToken& field, const VtValue& value)
{
    if (value.IsHolding<ListOpType>()) {
        Sdf_FileIOUtility::WriteListOp(
            out, indent, field, value.UncheckedGet<ListOpType>());
        return true;
    }
    return false;
}

static void
Sdf_WriteSimpleField(
    Sdf_TextOutput &out, size_t indent,
    const SdfSpec& spec, const TfToken& field)
{
    const VtValue& value = spec.GetField(field);

    if (Sdf_WriteIfListOp<SdfIntListOp>(out, indent, field, value)      ||
        Sdf_WriteIfListOp<SdfInt64ListOp>(out, indent, field, value)    ||
        Sdf_WriteIfListOp<SdfUIntListOp>(out, indent, field, value)     ||
        Sdf_WriteIfListOp<SdfUInt64ListOp>(out, indent, field, value)   ||
        Sdf_WriteIfListOp<SdfStringListOp>(out, indent, field, value)   ||
        Sdf_WriteIfListOp<SdfTokenListOp>(out, indent, field, value)) {
        return;
    }

    bool isUnregisteredValue = value.IsHolding<SdfUnregisteredValue>();

    if (isUnregisteredValue) {
        // The value boxed inside a SdfUnregisteredValue can either be a
        // std::string, a VtDictionary, or an SdfUnregisteredValueListOp.
        const VtValue &boxedValue = value.Get<SdfUnregisteredValue>().GetValue();
        if (boxedValue.IsHolding<SdfUnregisteredValueListOp>()) {
            Sdf_FileIOUtility::WriteListOp(
                out, indent, field, 
                boxedValue.UncheckedGet<SdfUnregisteredValueListOp>());
        }
        else {
            Sdf_FileIOUtility::Write(out, indent, "%s = ", field.GetText());
            if (boxedValue.IsHolding<VtDictionary>()) {
                Sdf_FileIOUtility::WriteDictionary(out, indent, true, boxedValue.Get<VtDictionary>());
            } 
            else if (boxedValue.IsHolding<std::string>()) {
                Sdf_FileIOUtility::Write(out, 0, "%s\n", boxedValue.Get<std::string>().c_str());
            }
        }
        return;
    }

    Sdf_FileIOUtility::Write(out, indent, "%s = ", field.GetText());
    if (value.IsHolding<VtDictionary>()) {
        Sdf_FileIOUtility::WriteDictionary(out, indent, true, value.Get<VtDictionary>());
    } 
    else if (value.IsHolding<bool>()) {
        Sdf_FileIOUtility::Write(out, 0, "%s\n", TfStringify(value.Get<bool>()).c_str());
    }
    else {
        Sdf_FileIOUtility::Write(out, 0, "%s\n", Sdf_FileIOUtility::StringFromVtValue(value).c_str());
    }
}

// Predicate for determining fields that should be included in a
// prim's metadata section.
struct Sdf_IsPrimMetadataField : public Sdf_IsMetadataField
{
    Sdf_IsPrimMetadataField() : Sdf_IsMetadataField(SdfSpecTypePrim) { }
    
    bool operator()(const TfToken& field) const
    { 
        // Typename is registered as metadata for a prim, but is written
        // outside the metadata section.
        if (field == SdfFieldKeys->TypeName) {
            return false;
        }

        return (Sdf_IsMetadataField::operator()(field) ||
            field == SdfFieldKeys->Payload             ||
            field == SdfFieldKeys->References          ||
            field == SdfFieldKeys->Relocates           ||
            field == SdfFieldKeys->InheritPaths        ||
            field == SdfFieldKeys->Specializes         ||
            field == SdfFieldKeys->VariantSetNames     ||
            field == SdfFieldKeys->VariantSelection);
    }
};

static bool
Sdf_WritePrimMetadata(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    // Partition this prim's fields so that all fields to write out are
    // in the range [fields.begin(), metadataFieldsEnd).
    TfTokenVector fields = prim.ListFields();
    TfTokenVector::iterator metadataFieldsEnd = 
        std::partition(fields.begin(), fields.end(), Sdf_IsPrimMetadataField());

    // Comment isn't tagged as a metadata field but gets special cased
    // because it wants to be at the top of the metadata section.
    std::string comment = prim.GetComment();
    bool hasComment     = !comment.empty();

    bool didParens = false;

    // As long as there's anything to write in the metadata section, we'll
    // always use the multi-line format.
    bool multiLine = hasComment || (fields.begin() != metadataFieldsEnd);

    // Write comment at the top of the metadata section for readability.
    if (hasComment) {
        didParens = 
            Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);
        Sdf_FileIOUtility::WriteQuotedString(out, indent+1, comment);
        Sdf_FileIOUtility::Puts(out, 0, "\n");
    }

    // Write out remaining fields in the metadata section in dictionary-sorted
    // order.
    std::sort(fields.begin(), metadataFieldsEnd, TfDictionaryLessThan());
    for (TfTokenVector::const_iterator fieldIt = fields.begin();
         fieldIt != metadataFieldsEnd; ++fieldIt) {

        didParens = 
            Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);

        const TfToken& field = *fieldIt;

        if (field == SdfFieldKeys->Documentation) {
            Sdf_FileIOUtility::Puts(out, indent+1, "doc = ");
            Sdf_FileIOUtility::WriteQuotedString(out, 0, prim.GetDocumentation());
            Sdf_FileIOUtility::Puts(out, 0, "\n");
        }
        else if (field == SdfFieldKeys->Permission) {
            if (multiLine) {
                Sdf_FileIOUtility::Write(out, indent+1, "permission = %s\n",
                    Sdf_FileIOUtility::Stringify(prim.GetPermission()) );
            } else {
                Sdf_FileIOUtility::Write(out, 0, "permission = %s",
                    Sdf_FileIOUtility::Stringify(prim.GetPermission()) );
            }
        }
        else if (field == SdfFieldKeys->SymmetryFunction) {
            Sdf_FileIOUtility::Write(out, multiLine ? indent+1 : 0, "symmetryFunction = %s%s",
                prim.GetSymmetryFunction().GetText(),
                multiLine ? "\n" : "");
        }
        else if (field == SdfFieldKeys->Payload) {
            const VtValue v = prim.GetField(field);
            if (!Sdf_WriteIfListOp<SdfPayloadListOp>(
                    out, indent+1, TfToken("payload"), v)) {
                TF_CODING_ERROR(
                    "'%s' field holding unexpected type '%s'",
                    field.GetText(), v.GetTypeName().c_str());
            }
        }
        else if (field == SdfFieldKeys->References) {
            const VtValue v = prim.GetField(field);
            if (!Sdf_WriteIfListOp<SdfReferenceListOp>(
                    out, indent+1, TfToken("references"), v)) {
                TF_CODING_ERROR(
                    "'%s' field holding unexpected type '%s'",
                    field.GetText(), v.GetTypeName().c_str());
            }
        }
        else if (field == SdfFieldKeys->VariantSetNames) {
            SdfVariantSetNamesProxy variantSetNameList = prim.GetVariantSetNameList();
            if (variantSetNameList.IsExplicit()) {
                // Explicit list
                SdfVariantSetNamesProxy::ListProxy setNames = variantSetNameList.GetExplicitItems();
                Sdf_FileIOUtility::Puts(out, indent+1, "variantSets = ");
                Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                Sdf_FileIOUtility::Puts(out, 0, "\n");
            } else {
                // List operations
                SdfVariantSetNamesProxy::ListProxy setNames = variantSetNameList.GetDeletedItems();
                if (!setNames.empty()) {
                    Sdf_FileIOUtility::Puts(out, indent+1, "delete variantSets = ");
                    Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                    Sdf_FileIOUtility::Puts(out, 0, "\n");
                }
                setNames = variantSetNameList.GetAddedItems();
                if (!setNames.empty()) {
                    Sdf_FileIOUtility::Puts(out, indent+1, "add variantSets = ");
                    Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                    Sdf_FileIOUtility::Puts(out, 0, "\n");
                }
                setNames = variantSetNameList.GetPrependedItems();
                if (!setNames.empty()) {
                    Sdf_FileIOUtility::Puts(out, indent+1, "prepend variantSets = ");
                    Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                    Sdf_FileIOUtility::Puts(out, 0, "\n");
                }
                setNames = variantSetNameList.GetAppendedItems();
                if (!setNames.empty()) {
                    Sdf_FileIOUtility::Puts(out, indent+1, "append variantSets = ");
                    Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                    Sdf_FileIOUtility::Puts(out, 0, "\n");
                }
                setNames = variantSetNameList.GetOrderedItems();
                if (!setNames.empty()) {
                    Sdf_FileIOUtility::Puts(out, indent+1, "reorder variantSets = ");
                    Sdf_FileIOUtility::WriteNameVector(out, indent+1, setNames);
                    Sdf_FileIOUtility::Puts(out, 0, "\n");
                }
            }
        }
        else if (field == SdfFieldKeys->InheritPaths) {
            const VtValue v = prim.GetField(field);
            if (!Sdf_WriteIfListOp<SdfPathListOp>(
                    out, indent+1, TfToken("inherits"), v)) {
                TF_CODING_ERROR(
                    "'%s' field holding unexpected type '%s'",
                    field.GetText(), v.GetTypeName().c_str());
            }
        }
        else if (field == SdfFieldKeys->Specializes) {
            const VtValue v = prim.GetField(field);
            if (!Sdf_WriteIfListOp<SdfPathListOp>(
                    out, indent+1, TfToken("specializes"), v)) {
                TF_CODING_ERROR(
                    "'%s' field holding unexpected type '%s'",
                    field.GetText(), v.GetTypeName().c_str());
            }
        }
        else if (field == SdfFieldKeys->Relocates) {

            // Relativize all paths in the relocates.
            SdfRelocatesMap result;
            SdfPath primPath = prim.GetPath();
            
            SdfRelocatesMap finalRelocates;
            const SdfRelocatesMapProxy relocates = prim.GetRelocates();
            TF_FOR_ALL(mapIt, relocates) {
                finalRelocates[mapIt->first.MakeRelativePath(primPath)] =
                    mapIt->second.MakeRelativePath(primPath);
            }

            Sdf_FileIOUtility::WriteRelocates(
                out, indent+1, multiLine, finalRelocates);
        }
        else if (field == SdfFieldKeys->PrefixSubstitutions) {
            VtDictionary prefixSubstitutions = prim.GetPrefixSubstitutions();
            Sdf_FileIOUtility::Puts(out, indent+1, "prefixSubstitutions = ");
            Sdf_FileIOUtility::WriteDictionary(out, indent+1, multiLine,
                          prefixSubstitutions, /* stringValuesOnly = */ true );
        }
        else if (field == SdfFieldKeys->SuffixSubstitutions) {
            VtDictionary suffixSubstitutions = prim.GetSuffixSubstitutions();
            Sdf_FileIOUtility::Puts(out, indent+1, "suffixSubstitutions = ");
            Sdf_FileIOUtility::WriteDictionary(out, indent+1, multiLine,
                          suffixSubstitutions, /* stringValuesOnly = */ true );
        }
        else if (field == SdfFieldKeys->VariantSelection) {
            SdfVariantSelectionMap refVariants = prim.GetVariantSelections();
            if (refVariants.size() > 0) {
                VtDictionary dictionary;
                TF_FOR_ALL(it, refVariants) {
                    dictionary[it->first] = VtValue(it->second);
                }
                Sdf_FileIOUtility::Puts(out, indent+1, "variants = ");
                Sdf_FileIOUtility::WriteDictionary(out, indent+1, multiLine, dictionary);
            }
        }
        else {
            Sdf_WriteSimpleField(out, indent+1, prim, field);
        }

    } // end for each field

    Sdf_FileIOUtility::CloseParensIfNeeded(out, indent, didParens, multiLine);

    return true;
}

namespace {
struct _SortByNameThenType {
    template <class T>
    bool operator()(T const &lhs, T const &rhs) const {
        // If the names are identical, order by spectype.  This puts Attributes
        // before Relationships (if identically named).
        std::string const &lhsName = lhs->GetName();
        std::string const &rhsName = rhs->GetName();
        return (lhsName == rhsName && lhs->GetSpecType() < rhs->GetSpecType())
            || TfDictionaryLessThan()(lhsName, rhsName);
    }
};
}

static bool
Sdf_WritePrimProperties(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    std::vector<SdfPropertySpecHandle> properties =
        prim.GetProperties().values_as<std::vector<SdfPropertySpecHandle> >();
    std::sort(properties.begin(), properties.end(), _SortByNameThenType());

    for (const SdfPropertySpecHandle& specHandle : properties) {
        const SdfPropertySpec& spec = specHandle.GetSpec();
        const SdfSpecType specType = spec.GetSpecType();

        if (specType == SdfSpecTypeAttribute) {
            Sdf_WriteAttribute(
                Sdf_CastAccess::CastSpec<
                    SdfAttributeSpec, SdfPropertySpec>(spec),
                out, indent+1);
        }
        else {
            Sdf_WriteRelationship(
                Sdf_CastAccess::CastSpec<
                    SdfRelationshipSpec, SdfPropertySpec>(spec),
                out, indent+1);
        }
    }

    return true;
}

static bool
Sdf_WritePrimNamespaceReorders(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    const std::vector<TfToken>& propertyNames = prim.GetPropertyOrder();

    if ( propertyNames.size() > 1 ) {
        Sdf_FileIOUtility::Puts( out, indent+1, "reorder properties = " );
        Sdf_FileIOUtility::WriteNameVector( out, indent+1, propertyNames );
        Sdf_FileIOUtility::Puts( out, 0, "\n" );
    }

    const std::vector<TfToken>& childrenNames = prim.GetNameChildrenOrder();

    if ( childrenNames.size() > 1 ) {

        Sdf_FileIOUtility::Puts( out, indent+1, "reorder nameChildren = " );
        Sdf_FileIOUtility::WriteNameVector( out, indent+1, childrenNames );
        Sdf_FileIOUtility::Puts( out, 0, "\n" );
    }

    return true;
}

static bool
Sdf_WritePrimChildren(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    bool newline = false;
    for (const SdfPrimSpecHandle& childPrim : prim.GetNameChildren()) {
        if (newline) {
            Sdf_FileIOUtility::Puts(out, 0, "\n");
        }
        else {
            newline = true;
        }
        Sdf_WritePrim(childPrim.GetSpec(), out, indent+1);
    }

    return true;
}

static bool
Sdf_WritePrimVariantSets(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    SdfVariantSetsProxy variantSets = prim.GetVariantSets();
    if (variantSets) {
        for (const auto& variantNameAndSet : variantSets) {
            const SdfVariantSetSpecHandle& vset = variantNameAndSet.second;
            Sdf_WriteVariantSet(vset.GetSpec(), out, indent+1);
        }
    }
    return true;
}

static bool
Sdf_WritePrimBody(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    Sdf_WritePrimNamespaceReorders( prim, out, indent );

    Sdf_WritePrimProperties( prim, out, indent );

    if (!prim.GetProperties().empty() && !prim.GetNameChildren().empty())
        Sdf_FileIOUtility::Puts(out, 0, "\n");

    Sdf_WritePrimChildren( prim, out, indent );

    Sdf_WritePrimVariantSets( prim, out, indent );

    return true;
}

static inline bool
Sdf_WritePrim(
    const SdfPrimSpec &prim, Sdf_TextOutput &out, size_t indent)
{
    Sdf_WritePrimPreamble( prim, out, indent );
    Sdf_WritePrimMetadata( prim, out, indent );

    Sdf_FileIOUtility::Puts(out, 0, "\n");
    Sdf_FileIOUtility::Puts(out, indent, "{\n");

    Sdf_WritePrimBody( prim, out, indent );

    Sdf_FileIOUtility::Puts(out, indent, "}\n");

    return true;
}

static bool
Sdf_WriteVariant(
    const SdfVariantSpec &variantSpec, Sdf_TextOutput &out, size_t indent)
{
    SdfPrimSpec primSpec = variantSpec.GetPrimSpec().GetSpec();
    Sdf_FileIOUtility::WriteQuotedString(out, indent, variantSpec.GetName());

    Sdf_WritePrimMetadata( primSpec, out, indent );

    Sdf_FileIOUtility::Write(out, 0, " {\n");

    Sdf_WritePrimBody( primSpec, out, indent );

    Sdf_FileIOUtility::Write(out, 0, "\n");
    Sdf_FileIOUtility::Write(out, indent, "}\n");

    return true;
}

static bool
Sdf_WriteVariantSet(
    const SdfVariantSetSpec &spec, Sdf_TextOutput &out, size_t indent)
{
    SdfVariantSpecHandleVector variants = spec.GetVariantList();
    std::sort(
        variants.begin(), variants.end(), 
        [](const SdfVariantSpecHandle& a, const SdfVariantSpecHandle& b) {
            return a->GetName() < b->GetName();
        });

    if (!variants.empty()) {
        Sdf_FileIOUtility::Write(out, indent, "variantSet ");
        Sdf_FileIOUtility::WriteQuotedString(out, 0, spec.GetName());
        Sdf_FileIOUtility::Write(out, 0, " = {\n");
        for (const SdfVariantSpecHandle& v : variants) {
            Sdf_WriteVariant(v.GetSpec(), out, indent+1);
        }
        Sdf_FileIOUtility::Write(out, indent, "}\n");
    }

    return true;
}

static bool
Sdf_WriteConnectionStatement(
    Sdf_TextOutput &out,
    size_t indent, const SdfConnectionsProxy::ListProxy &connections,
    const std::string &opStr,
    const std::string &variabilityStr,
    const std::string &typeStr, const std::string &nameStr,
    const SdfAttributeSpec* attrOwner)
{
    Sdf_FileIOUtility::Write(out, indent, "%s%s%s %s.connect = ",
                            opStr.c_str(),
                            variabilityStr.c_str(),
                            typeStr.c_str(), nameStr.c_str());

    if (connections.size() == 0) {
        Sdf_FileIOUtility::Puts(out, 0, "None\n");
    } 
    else if (connections.size() == 1) {
        Sdf_FileIOUtility::WriteSdfPath(out, 0, connections.front());
        Sdf_FileIOUtility::Puts(out, 0, "\n");
    } 
    else {
        Sdf_FileIOUtility::Puts(out, 0, "[\n");
        TF_FOR_ALL(it, connections) {
            Sdf_FileIOUtility::WriteSdfPath(out, indent+1, (*it));
            Sdf_FileIOUtility::Puts(out, 0, ",\n");
        }
        Sdf_FileIOUtility::Puts(out, indent, "]\n");
    }
    return true;
}

static bool
Sdf_WriteConnectionList(
    Sdf_TextOutput &out,
    size_t indent, const SdfConnectionsProxy &connList,
    const std::string &variabilityStr,
    const std::string &typeStr, const std::string &nameStr,
    const SdfAttributeSpec *attrOwner)
{
    if (connList.IsExplicit()) {
        SdfConnectionsProxy::ListProxy vec = connList.GetExplicitItems();
        Sdf_WriteConnectionStatement(out, indent, vec, "",
                                    variabilityStr,
                                    typeStr, nameStr, attrOwner);
    } else {
        SdfConnectionsProxy::ListProxy vec = connList.GetDeletedItems();
        if (!vec.empty()) {
            Sdf_WriteConnectionStatement(out, indent, vec, "delete ",
                                              variabilityStr, typeStr, nameStr,
                                              NULL);
        }
        vec = connList.GetAddedItems();
        if (!vec.empty()) {
            Sdf_WriteConnectionStatement(out, indent, vec, "add ",
                                        variabilityStr, typeStr,
                                        nameStr, attrOwner);
        }
        vec = connList.GetPrependedItems();
        if (!vec.empty()) {
            Sdf_WriteConnectionStatement(out, indent, vec, "prepend ",
                                        variabilityStr, typeStr,
                                        nameStr, attrOwner);
        }
        vec = connList.GetAppendedItems();
        if (!vec.empty()) {
            Sdf_WriteConnectionStatement(out, indent, vec, "append ",
                                        variabilityStr, typeStr,
                                        nameStr, attrOwner);
        }
        vec = connList.GetOrderedItems();
        if (!vec.empty()) {
            Sdf_WriteConnectionStatement(out, indent, vec, "reorder ",
                                              variabilityStr, typeStr, nameStr,
                                              NULL);
        }
    }
    return true;
}

// Predicate for determining fields that should be included in an
// attribute's metadata section.
struct Sdf_IsAttributeMetadataField : public Sdf_IsMetadataField
{
    Sdf_IsAttributeMetadataField() : Sdf_IsMetadataField(SdfSpecTypeAttribute)
    { }
    
    bool operator()(const TfToken& field) const
    {
        return (Sdf_IsMetadataField::operator()(field) ||
            field == SdfFieldKeys->DisplayUnit);
    }
};

static inline bool
Sdf_WriteAttribute(
    const SdfAttributeSpec &attr, Sdf_TextOutput &out, size_t indent)
{
    std::string variabilityStr =
        Sdf_FileIOUtility::Stringify( attr.GetVariability() );
    if (!variabilityStr.empty())
        variabilityStr += ' ';

    // Retrieve spline, if any.  If the attribute has a spline, but it's empty,
    // treat that the same as not having a spline.  We don't serialize empty
    // splines, because they don't affect anything.
    const VtValue splineVal = attr.GetField(SdfFieldKeys->Spline);
    const TsSpline spline =
        (splineVal.IsHolding<TsSpline>() ?
            splineVal.UncheckedGet<TsSpline>() : TsSpline());

    bool hasComment           = !attr.GetComment().empty();
    bool hasDefault           = attr.HasField(SdfFieldKeys->Default);
    bool hasCustomDeclaration = attr.IsCustom();
    bool hasConnections       = attr.HasField(SdfFieldKeys->ConnectionPaths);
    bool hasTimeSamples       = attr.HasField(SdfFieldKeys->TimeSamples);
    bool hasSpline            = !spline.IsEmpty();

    std::string typeName =
        SdfValueTypeNames->GetSerializationName(attr.GetTypeName()).GetString();

    // Partition this attribute's fields so that all fields to write in the
    // metadata section are in the range [fields.begin(), metadataFieldsEnd).
    TfTokenVector fields = attr.ListFields();
    TfTokenVector::iterator metadataFieldsEnd = std::partition(
        fields.begin(), fields.end(), Sdf_IsAttributeMetadataField());

    // As long as there's anything to write in the metadata section, we'll
    // always use the multi-line format.
    bool hasInfo = hasComment || (fields.begin() != metadataFieldsEnd);
    bool multiLine = hasInfo;

    bool didParens = false;

    // Write the basic line if we have info or a default or if we
    // have nothing else to write.
    if (hasInfo || hasDefault || hasCustomDeclaration ||
        (!hasConnections && !hasTimeSamples && !hasSpline))
    {
        VtValue value;

        if(hasDefault)
            value = attr.GetDefaultValue();

        Sdf_FileIOUtility::Write( out, indent, "%s%s%s %s",
            (hasCustomDeclaration ? "custom " : ""),
            variabilityStr.c_str(),
            typeName.c_str(),
            attr.GetName().c_str() );

        // If we have a default value, write it...
        if (!value.IsEmpty()) {
            Sdf_FileIOUtility::WriteDefaultValue(out, indent, value);
        }

        // Write comment at the top of the metadata section for readability.
        if (hasComment) {
            didParens = Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);
            Sdf_FileIOUtility::WriteQuotedString(out, indent+1, attr.GetComment());
            Sdf_FileIOUtility::Puts(out, 0, "\n");
        }

        // Write out remaining fields in the metadata section in
        // dictionary-sorted order.
        std::sort(fields.begin(), metadataFieldsEnd, TfDictionaryLessThan());
        for (TfTokenVector::const_iterator fieldIt = fields.begin();
             fieldIt != metadataFieldsEnd; ++fieldIt) {

            didParens = 
                Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);

            const TfToken& field = *fieldIt;

            if (field == SdfFieldKeys->Documentation) {
                Sdf_FileIOUtility::Puts(out, indent+1, "doc = ");
                Sdf_FileIOUtility::WriteQuotedString(out, 0, attr.GetDocumentation());
                Sdf_FileIOUtility::Puts(out, 0, "\n");
            }
            else if (field == SdfFieldKeys->Permission) {
                Sdf_FileIOUtility::Write(out, multiLine ? indent+1 : 0, "permission = %s%s",
                                    Sdf_FileIOUtility::Stringify(attr.GetPermission()),
                                    multiLine ? "\n" : "");
            }
            else if (field == SdfFieldKeys->SymmetryFunction) {
                Sdf_FileIOUtility::Write(out, multiLine ? indent+1 : 0, "symmetryFunction = %s%s",
                                    attr.GetSymmetryFunction().GetText(),
                                    multiLine ? "\n" : "");
            }
            else if (field == SdfFieldKeys->DisplayUnit) {
                Sdf_FileIOUtility::Write(out, multiLine ? indent+1 : 0, "displayUnit = %s%s",
                                    SdfGetNameForUnit(attr.GetDisplayUnit()).c_str(),
                                    multiLine ? "\n" : "");
            }
            else {
                Sdf_WriteSimpleField(out, indent+1, attr, field);
            }

        } // end for each field

        Sdf_FileIOUtility::CloseParensIfNeeded(out, indent, didParens, multiLine);
        Sdf_FileIOUtility::Puts(out, 0, "\n");
    }

    if (hasTimeSamples) {
        Sdf_FileIOUtility::Write(out, indent, "%s%s %s.timeSamples = {\n",
                                 variabilityStr.c_str(),
                                 typeName.c_str(), attr.GetName().c_str() );
        Sdf_FileIOUtility::WriteTimeSamples(out, indent, attr);
        Sdf_FileIOUtility::Puts(out, indent, "}\n");
    }

    if (hasSpline) {
        Sdf_FileIOUtility::Write(out, indent, "%s%s %s.spline = {\n",
                                 variabilityStr.c_str(),
                                 typeName.c_str(), attr.GetName().c_str() );
        Sdf_FileIOUtility::WriteSpline(out, indent, spline);
        Sdf_FileIOUtility::Puts(out, indent, "}\n");
    }

    if (hasConnections) {
        Sdf_WriteConnectionList(out, indent, attr.GetConnectionPathList(),
                               variabilityStr, typeName,
                               attr.GetName(), &attr);
    }

    return true;
}

enum Sdf_WriteFlag {
    Sdf_WriteFlagDefault = 0,
    Sdf_WriteFlagAttributes = 1,
    Sdf_WriteFlagNoLastNewline = 2,
};

inline Sdf_WriteFlag operator |(Sdf_WriteFlag a, Sdf_WriteFlag b)
{
    return (Sdf_WriteFlag)(static_cast<int>(a) | static_cast<int>(b));
}

static bool
Sdf_WriteRelationshipTargetList(
    const SdfRelationshipSpec &rel,
    const SdfTargetsProxy::ListProxy &targetPaths,
    Sdf_TextOutput &out, size_t indent, Sdf_WriteFlag flags)
{
    if (targetPaths.size() > 1) {
        Sdf_FileIOUtility::Write(out, 0," = [\n");
        ++indent;
    } else {
        Sdf_FileIOUtility::Write(out, 0," = ");
    }

    for (size_t i=0; i < targetPaths.size(); ++i) {
        if (targetPaths.size() > 1) {
            Sdf_FileIOUtility::Write(out, indent, "");
        }
        Sdf_FileIOUtility::WriteSdfPath( out, 0, targetPaths[i] );
        if (targetPaths.size() > 1) {
            Sdf_FileIOUtility::Write(out, 0,",\n");
        }
    }

    if (targetPaths.size() > 1) {
        --indent;
        Sdf_FileIOUtility::Write(out, indent, "]");
    }
    if (!(flags & Sdf_WriteFlagNoLastNewline)) {
        Sdf_FileIOUtility::Write(out, 0,"\n");
    }
    return true;
}

// Predicate for determining fields that should be included in an
// relationship's metadata section.
struct Sdf_IsRelationshipMetadataField : public Sdf_IsMetadataField
{
    Sdf_IsRelationshipMetadataField() 
        : Sdf_IsMetadataField(SdfSpecTypeRelationship) { }
    
    bool operator()(const TfToken& field) const
    {
        return Sdf_IsMetadataField::operator()(field);
    }
};

static inline bool
Sdf_WriteRelationship(
    const SdfRelationshipSpec &rel, Sdf_TextOutput &out, size_t indent)
{
    // When a new metadata field is added to the spec, it will be automatically
    // written out generically, so you probably don't need to add a special case
    // here. If you need to special-case the output of a metadata field, you will
    // also need to prevent the automatic output by adding the token inside
    // Sdf_GetGenericRelationshipMetadataFields().
    //
    // These special cases below were all kept to prevent reordering in existing
    // Pixar files, which would create noise in file diffs.
    bool hasComment           = !rel.GetComment().empty();
    bool hasTargets           = rel.HasField(SdfFieldKeys->TargetPaths);
    bool hasDefaultValue      = rel.HasField(SdfFieldKeys->Default);

    bool hasCustom            = rel.IsCustom();

    // Partition this attribute's fields so that all fields to write in the
    // metadata section are in the range [fields.begin(), metadataFieldsEnd).
    TfTokenVector fields = rel.ListFields();
    TfTokenVector::iterator metadataFieldsEnd = std::partition(
        fields.begin(), fields.end(), Sdf_IsRelationshipMetadataField());

    bool hasInfo = hasComment || (fields.begin() != metadataFieldsEnd);
    bool multiLine = hasInfo;

    bool didParens = false;

    bool hasExplicitTargets = false;
    bool hasTargetListOps = false;
    if (hasTargets) {
        SdfTargetsProxy targetPathList = rel.GetTargetPathList();
        hasExplicitTargets = targetPathList.IsExplicit()  &&
                             targetPathList.HasKeys();
        hasTargetListOps   = !targetPathList.IsExplicit() &&
                             targetPathList.HasKeys();
    }

    // If relationship is a varying relationship, use varying keyword.
    bool isVarying = (rel.GetVariability() == SdfVariabilityVarying);
    std::string varyingStr = isVarying ? "varying " : ""; // the space in "varying " is required...


    // Write the basic line if we have info or a default (i.e. explicit
    // targets) or if we have nothing else to write and we're not custom
    if (hasInfo || (hasTargets && hasExplicitTargets) ||
        (!hasTargetListOps && !rel.IsCustom()))
    {

        if (hasCustom) {
            Sdf_FileIOUtility::Write( out, indent, "custom %srel %s",
                                     varyingStr.c_str(),
                                     rel.GetName().c_str() );
        } else {
            Sdf_FileIOUtility::Write( out, indent, "%srel %s",
                                     varyingStr.c_str(),
                                     rel.GetName().c_str() );
        }

        if (hasTargets && hasExplicitTargets) {
            SdfTargetsProxy targetPathList = rel.GetTargetPathList();
            SdfTargetsProxy::ListProxy targetPaths = targetPathList.GetExplicitItems();
            if (targetPaths.size() == 0) {
                Sdf_FileIOUtility::Write(out, 0, " = None");
            } else {
                // Write explicit targets
                Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent,
                            Sdf_WriteFlagAttributes | Sdf_WriteFlagNoLastNewline);
            }
        }

        // Write comment at the top of the metadata section for readability.
        if (hasComment) {
            didParens = Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);
            Sdf_FileIOUtility::WriteQuotedString(out, indent+1, rel.GetComment());
            Sdf_FileIOUtility::Write(out, 0, "\n");
        }

        // Write out remaining fields in the metadata section in
        // dictionary-sorted order.
        std::sort(fields.begin(), metadataFieldsEnd, TfDictionaryLessThan());
        for (TfTokenVector::const_iterator fieldIt = fields.begin();
             fieldIt != metadataFieldsEnd; ++fieldIt) {

            didParens = 
                Sdf_FileIOUtility::OpenParensIfNeeded(out, didParens, multiLine);

            const TfToken& field = *fieldIt;

            if (field == SdfFieldKeys->Documentation) {
                Sdf_FileIOUtility::Write(out, indent+1, "doc = ");
                Sdf_FileIOUtility::WriteQuotedString(out, 0, rel.GetDocumentation());
                Sdf_FileIOUtility::Write(out, 0, "\n");
            }
            else if (field == SdfFieldKeys->Permission) {
                if (multiLine) {
                    Sdf_FileIOUtility::Write(out, indent+1, "permission = %s\n",
                            Sdf_FileIOUtility::Stringify(rel.GetPermission()));
                } else {
                    Sdf_FileIOUtility::Write(out, 0, "permission = %s",
                            Sdf_FileIOUtility::Stringify(rel.GetPermission()));
                }
            }
            else if (field == SdfFieldKeys->SymmetryFunction) {
                Sdf_FileIOUtility::Write(out, multiLine ? indent+1 : 0, "symmetryFunction = %s%s",
                                    rel.GetSymmetryFunction().GetText(),
                                    multiLine ? "\n" : "");
            }
            else {
                Sdf_WriteSimpleField(out, indent+1, rel, field);                
            }

        } // end for each field

        Sdf_FileIOUtility::CloseParensIfNeeded(out, indent, didParens, multiLine);
        Sdf_FileIOUtility::Write(out, 0,"\n");
    }
    else if (hasCustom) {
        // If we did not write out the "basic" line AND we are custom,
        // we need to add a custom decl line, because we won't include
        // custom in any of the output below
        Sdf_FileIOUtility::Write( out, indent, "custom %srel %s\n",
                                 varyingStr.c_str(),        
                                 rel.GetName().c_str() );
    }

    if (hasTargets && hasTargetListOps) {
        // Write deleted targets
        SdfTargetsProxy targetPathList = rel.GetTargetPathList();
        SdfTargetsProxy::ListProxy targetPaths = targetPathList.GetDeletedItems();
        if (!targetPaths.empty()) {
            Sdf_FileIOUtility::Write( out, indent, "delete %srel %s",
                varyingStr.c_str(), rel.GetName().c_str());
            Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent, Sdf_WriteFlagDefault);
        }

        // Write added targets
        targetPaths = targetPathList.GetAddedItems();
        if (!targetPaths.empty()) {
            Sdf_FileIOUtility::Write( out, indent, "add %srel %s",
                varyingStr.c_str(), rel.GetName().c_str());
            Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent, Sdf_WriteFlagAttributes);
        }
        targetPaths = targetPathList.GetPrependedItems();
        if (!targetPaths.empty()) {
            Sdf_FileIOUtility::Write( out, indent, "prepend %srel %s",
                varyingStr.c_str(), rel.GetName().c_str());
            Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent, Sdf_WriteFlagAttributes);
        }
        targetPaths = targetPathList.GetAppendedItems();
        if (!targetPaths.empty()) {
            Sdf_FileIOUtility::Write( out, indent, "append %srel %s",
                varyingStr.c_str(), rel.GetName().c_str());
            Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent, Sdf_WriteFlagAttributes);
        }

        // Write ordered targets
        targetPaths = targetPathList.GetOrderedItems();
        if (!targetPaths.empty()) {
            Sdf_FileIOUtility::Write( out, indent, "reorder %srel %s",
                varyingStr.c_str(), rel.GetName().c_str());
            Sdf_WriteRelationshipTargetList(rel, targetPaths, out, indent, Sdf_WriteFlagDefault);
        }
    }

    // Write out the default value for the relationship if we have one...
    if (hasDefaultValue)
    {
        VtValue value = rel.GetDefaultValue();
        if (!value.IsEmpty())
        {
            Sdf_FileIOUtility::Write(out, indent, "%srel %s.default = ",
                                    varyingStr.c_str(),
                                    rel.GetName().c_str());
            Sdf_FileIOUtility::WriteDefaultValue(out, 0, value);
            Sdf_FileIOUtility::Puts(out, indent, "\n");
        }
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_SDF_FILE_IO_COMMON_H
