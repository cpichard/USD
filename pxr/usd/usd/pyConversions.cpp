//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usd/pyConversions.h"

#include "pxr/base/vt/dictionary.h"
#include "pxr/base/vt/value.h"

#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/layerOffset.h"
#include "pxr/usd/sdf/listOp.h"
#include "pxr/usd/sdf/reference.h"
#include "pxr/usd/sdf/payload.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"

#include "pxr/base/tf/pyLock.h"
#include "pxr/base/tf/pyUtils.h"

#include "pxr/external/boost/python/object.hpp"
#include <string>

PXR_NAMESPACE_OPEN_SCOPE


// XXX: This function is no longer required -- remove.
TfPyObjWrapper
UsdVtValueToPython(const VtValue &value)
{
    // Convert to python.
    TfPyLock lock;
    return TfPyObjWrapper(TfPyObject(value));
}

VtValue 
UsdPythonToSdfType(TfPyObjWrapper pyVal, SdfValueTypeName const &targetType)
{
    using namespace pxr_boost::python;

    // Extract VtValue from python object.
    VtValue val;
    {
        TfPyLock lock;
        val = extract<VtValue>(pyVal.Get())();
    }

    // Attempt to cast the value to what we want.  Get a default value for this
    // attribute's type name.
    VtValue defVal = targetType.GetDefaultValue();

    // Attempt to cast the given value to the default value's type -- this
    // will convert python buffer protocol objects (e.g. numpy arrays) to the
    // appropriate typed VtArray when possible.  If casting fails, attempt to
    // continue with the given value.  Deeper in the 'Set()' implementation,
    // we'll issue a detailed type mismatch error.
    VtValue cast = VtValue::CastToTypeOf(val, defVal);
    if (!cast.IsEmpty())
        cast.Swap(val);

    return val;
}

bool
UsdPythonToMetadataValue(
    const TfToken &key, const TfToken &keyPath, 
    TfPyObjWrapper pyVal, VtValue *result)
{
    using namespace pxr_boost::python;

    SdfSchema const &schema = SdfSchema::GetInstance();

    SdfSchema::FieldDefinition const *fieldDef = schema.GetFieldDefinition(key);

    if (!fieldDef) {
        TF_CODING_ERROR("Unregistered metadata key: %s", key.GetText());
        return false;
    }

    VtValue value = extract<VtValue>(pyVal.Get())();

    // Empty values are always considered valid.
    if (value.IsEmpty()) {
        *result = value;
        return true;
    }

    // Attempt to obtain a fallback value.
    VtValue fallback = fieldDef->GetFallbackValue();

    if (value.IsHolding<VtDictionary>()) {
        VtDictionary inDict;
        value.UncheckedSwap(inDict);
        std::string errMsg;
        if (!SdfConvertToValidMetadataDictionary(&inDict, &errMsg)) {
            TfPyThrowValueError(
                TfStringPrintf(
                    "Invalid value type for %s%s%s: %s.",
                    key.GetText(), keyPath.IsEmpty() ? "" : ":",
                    keyPath.GetText(), errMsg.c_str()));
        }
        value.UncheckedSwap(inDict);
    }

    if (!keyPath.IsEmpty() && fallback.IsHolding<VtDictionary>()) {
        if (!fieldDef->IsValidMapValue(value)) {
            TfPyThrowValueError(
                TfStringPrintf(
                    "Invalid value type for dictionary key-path '%s:%s': '%s'.",
                    key.GetString().c_str(),
                    keyPath.GetText(),
                    TfPyRepr(pyVal.Get()).c_str()));
        }
        // Clear out the fallback here, since we allow any scene desc type in
        // dicts.
        fallback = VtValue();
    }

    // We have to handle a few things as special cases to disambiguate
    // types from Python.
    if (!fallback.IsEmpty()) {
        if (fallback.IsHolding<TfTokenVector>()) {
            value = extract<TfTokenVector>(pyVal.Get())();
        }
        else if (fallback.IsHolding< std::vector<std::string> >()) {
            extract<std::vector<std::string> > getVecString(pyVal.Get());
            extract<VtStringArray> getStringArray(pyVal.Get());
            if (getVecString.check()) {
                value = getVecString();
            } else if (getStringArray.check()) {
                VtStringArray a = getStringArray();
                value = std::vector<std::string>(a.begin(), a.end());
            }
        }
        else {
            value.CastToTypeOf(fallback);
        }
    }
    // If we failed to produce a value, or if we don't have a fallback and the
    // value we do have is not valid for either the field definition or the
    // schema, then complain.
    if (value.IsEmpty() ||
        (fallback.IsEmpty() &&
         (!fieldDef->IsValidValue(value) || !schema.IsValidValue(value)))) {
        VtValue origValue = extract<VtValue>(pyVal.Get())();
        TfPyThrowValueError(
            TfStringPrintf(
                "Invalid value '%s' (type '%s') for key '%s%s'.%s",
                TfPyRepr(pyVal.Get()).c_str(),
                origValue.GetTypeName().c_str(),
                key.GetText(),
                keyPath.IsEmpty() ? "" :
                TfStringPrintf(":%s", keyPath.GetText()).c_str(),
                fallback.IsEmpty() ? "" :
                TfStringPrintf(" Expected type '%s'", fallback.GetType().
                               GetTypeName().c_str()).c_str()));
    }
    result->Swap(value);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

