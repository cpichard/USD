//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#ifndef PXR_USD_VALIDATION_USD_VALIDATION_ERROR_H
#define PXR_USD_VALIDATION_USD_VALIDATION_ERROR_H

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/property.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usdValidation/usdValidation/api.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class UsdValidationValidator;
class UsdValidationFixer;

/// \class UsdValidationErrorType
///
/// UsdValidationErrorType reflects severity of a validation error, which can
/// then be reported appropriately to the users.
///
/// None: No Error.
/// Error: Associates the UsdValidationErrorType with an actual Error reported
///        by the validation task.
/// Warn: Associates the UsdValidationErrorType with a less severe situation and
///       hence reported as warning by the validation task.
/// Info: Associates the UsdValidationErrorType with information which needs to
///       be reported to the users by the validation task.
enum class UsdValidationErrorType
{
    None = 0,
    Error,
    Warn,
    Info
};

/// \class UsdValidationErrorSite
///
/// UsdValidationErrorSite is important information available from a
/// ValidationError, which annotates the site where the Error was reported by a
/// validation task.
///
/// An Error could be reported in a SdfLayer (in layer metadata, for example),
/// or a UsdStage (in stage metadata, for example) or a Prim within a stage, or
/// a property of a prim.
struct UsdValidationErrorSite
{
public:
    UsdValidationErrorSite() = default;

    /// Initialize an UsdValidationErrorSite using a \p layer and an
    /// \p objectPath.
    ///
    /// Object Path here could be a prim or a property spec path.
    ///
    /// Note that to identify a layer metadata, objectPath can be set as the
    /// pseudoRoot.
    USDVALIDATION_API
    UsdValidationErrorSite(const SdfLayerHandle &layer,
                           const SdfPath &objectPath);

    /// Initialize an UsdValidationErrorSite using a \p usdStage and an \p
    /// objectPath.
    ///
    /// An option \p layer can also be provided to provide information about a
    /// specific layer the erroring \p objectPath is found in the property
    /// stack.
    ///
    /// Object Path here could be a prim path or a property path.
    /// Note that to identify stage's root layer metadata, objectPath can be set
    /// as the pseudoRoot.
    USDVALIDATION_API
    UsdValidationErrorSite(const UsdStagePtr &usdStage,
                           const SdfPath &objectPath,
                           const SdfLayerHandle &layer = SdfLayerHandle());

    /// Returns true if UsdValidationErrorSite instance can either point to a
    /// prim or property spec in a layer or a prim or property on a stage.
    bool IsValid() const
    {
        return IsValidSpecInLayer() || IsPrim() || IsProperty();
    }

    /// Returns true if the objectPath and layer represent a spec in the layer;
    /// false otherwise.
    bool IsValidSpecInLayer() const
    {
        if (!_layer || _objectPath.IsEmpty()) {
            return false;
        }
        return _layer->HasSpec(_objectPath);
    }

    /// Returns true if UsdValidationErrorSite represents a prim on a stage,
    /// false otherwise.
    bool IsPrim() const
    {
        return GetPrim().IsValid();
    }

    /// Returns true if UsdValidationErrorSite represents a property on a stage,
    /// false otherwise.
    bool IsProperty() const
    {
        return GetProperty().IsValid();
    }

    /// Returns the SdfPropertySpecHandle associated with this
    /// ValidationErrorSite's layer and objectPath.
    ///
    /// Returns an invalid SdfPropertySpecHandle if no valid property spec is
    /// found, or when UsdValidationErrorSite instance doesn't have a
    /// layer.
    const SdfPropertySpecHandle GetPropertySpec() const
    {
        if (!_layer) {
            return SdfPropertySpecHandle();
        }
        return _layer->GetPropertyAtPath(_objectPath);
    }

    /// Returns the SdfPrimSpecHandle associated with this ValidationErrorSite's
    /// layer and objectPath.
    ///
    /// Returns an invalid SdfPrimSpecHandle if no valid prim spec is found, or
    /// when UsdValidationErrorSite instance doesn't have a layer.
    const SdfPrimSpecHandle GetPrimSpec() const
    {
        if (!_layer) {
            return SdfPrimSpecHandle();
        }
        return _layer->GetPrimAtPath(_objectPath);
    }

    /// Returns the SdfLayerHandle associated with this
    /// UsdValidationValidatorErrorSite
    const SdfLayerHandle &GetLayer() const
    {
        return _layer;
    }

    /// Returns the UsdStage associated with this UsdValidationErrorSite;
    /// nullptr othewrise.
    const UsdStagePtr &GetStage() const
    {
        return _usdStage;
    }

    /// Returns UsdPrim associated with this UsdValidationErrorSite, that is
    /// when UsdStage is present and objectPath represents a prim path on this
    /// stage; if not, an invalid prim is returned.
    UsdPrim GetPrim() const
    {
        if (_usdStage) {
            return _usdStage->GetPrimAtPath(_objectPath);
        }
        return UsdPrim();
    }

    /// Returns UsdProperty associated with this UsdValidationErrorSite, that is
    /// when UsdStage is present and objectPath represents a property path on
    /// this stage; if not, an invalid property is returned.
    UsdProperty GetProperty() const
    {
        if (_usdStage) {
            return _usdStage->GetPropertyAtPath(_objectPath);
        }
        return UsdProperty();
    }

    /// Returns true if \p other UsdValidationErrorSite has same valued members
    /// as this UsdValidationErrorSite, false otherwise.
    bool operator==(const UsdValidationErrorSite &other) const
    {
        return (_layer == other._layer) && (_usdStage == other._usdStage)
            && (_objectPath == other._objectPath);
    }

    /// Returns false if \p other UsdValidationErrorSite has same valued members
    /// as this UsdValidationErrorSite, true otherwise.
    bool operator!=(const UsdValidationErrorSite &other) const
    {
        return !(*this == other);
    }

private:
    UsdStagePtr _usdStage;
    SdfLayerHandle _layer;
    SdfPath _objectPath;

}; // UsdValidationErrorSite

using UsdValidationErrorSites = std::vector<UsdValidationErrorSite>;

/// \class UsdValidationError
///
/// UsdValidationError is an entity returned by a validation task, which is
/// associated with a UsdValidationValidator.
///
/// A UsdValidationError instance contains important information, like:
///
/// - Name - A name the validator writer provided for the error. This is then
///        used to construct an identifier for the error.
///
/// - UsdValidationErrorType - severity of an error,
///
/// - UsdValidationErrorSites - on what sites validationError was reported by a
///                          validation task,
///
/// - Message - Message providing more information associated with the error.
///           Such a message is provided by the validator writer, when providing
///           implementation for the validation task function.
///
/// UsdValidationError instances are typically created by the validation task
/// functions, and returned as part of the vector of errors from a call to
/// UsdValidationValidator::Validate() function.
///
/// A default constructed UsdValidationError instance signifies no error.
///
/// A UsdValidationError instance contains a pointer to the 
/// UsdValidationValidator that generated it, which can be retrieved using
/// GetValidator() method.
///
/// A UsdValidationError instance can also provide access to the fixers 
/// associated with the validator that generated it, which can be retrieved
/// using various GetFixer*() methods.
///
class UsdValidationError
{
public:
    /// A default constructed UsdValidationError signifies no error.
    USDVALIDATION_API
    UsdValidationError();

    /// Instantiate a ValidationError by providing its \p name, \p errorType,
    /// \p errorSites and an \p errorMsg.
    USDVALIDATION_API
    UsdValidationError(const TfToken &name,
                       const UsdValidationErrorType &errorType,
                       const UsdValidationErrorSites &errorSites,
                       const std::string &errorMsg,
                       const VtValue &metadata = VtValue());

    bool operator==(const UsdValidationError &other) const
    {
        return (_name == other._name) && (_errorType == other._errorType)
            && (_errorSites == other._errorSites)
            && (_errorMsg == other._errorMsg)
            && (_validator == other._validator);
    }

    bool operator!=(const UsdValidationError &other) const
    {
        return !(*this == other);
    }

    /// Returns the name token of the UsdValidationError
    const TfToken &GetName() const &
    {
        return _name;
    }

    /// Returns the name token of the UsdValidationError by-value
    TfToken GetName() &&
    {
        return std::move(_name);
    }

    /// Returns the UsdValidationErrorType associated with this
    /// UsdValidationError
    UsdValidationErrorType GetType() const
    {
        return _errorType;
    }

    /// Returns the UsdValidationErrorSite associated with this
    /// UsdValidationError
    const UsdValidationErrorSites &GetSites() const &
    {
        return _errorSites;
    }

    /// Returns the UsdValidationErrorSite associated with this
    /// UsdValidationError by-value
    UsdValidationErrorSites GetSites() &&
    {
        return std::move(_errorSites);
    }

    /// Returns the UsdValidationValidator that reported this error.
    ///
    /// This will return nullptr if there is no UsdValidationValidator
    /// associated with this error. This will never be nullptr for validation
    /// errors returned from calls to UsdValidationValidator::Validate.
    const UsdValidationValidator *GetValidator() const
    {
        return _validator;
    }

    /// Returns the message associated with this UsdValidationError
    const std::string &GetMessage() const
    {
        return _errorMsg;
    }

    /// Returns the metadata associated with this UsdValidationError
    ///
    /// Validator writers can provide additional metadata when creating a
    /// UsdValidationError instance, which can then be retrieved using this
    /// method, and may be used by the fixer associated with the validator.
    const VtValue &GetMetadata() const
    {
        return _metadata;
    }

    /// An identifier for the error constructed from the validator name this
    /// error was generated from and its name.
    ///
    /// Since a validator may result in multiple distinct errors, the identifier
    /// helps in distinguishing and categorizing the errors. The identifier
    /// returned will be in the following form:
    /// For a plugin validator: "plugName":"validatorName"."ErrorName"
    /// For a non-plugin validator: "validatorName"."ErrorName"
    ///
    /// For an error that was generated without a name, the identifier will be
    /// same as the validator name which generated the error.
    ///
    /// For an error which is created directly and not via
    /// UsdValidationValidator::Validate() call, we throw a coding error, as its
    /// an improper use of the API.
    USDVALIDATION_API
    TfToken GetIdentifier() const;

    /// Returns UsdValidationErrorType and ErrorMessage concatenated as a string
    USDVALIDATION_API
    std::string GetErrorAsString() const;

    /// Returns true if UsdValidationErrorType is UsdValidationErrorType::None,
    /// false otherwise
    bool HasNoError() const
    {
        return _errorType == UsdValidationErrorType::None;
    }

    /// Return a vector of fixers associated with this Validator.
    ///
    USDVALIDATION_API
    const std::vector<const UsdValidationFixer *> GetFixers() const;

    /// Return an immutable fixer given its \p name if it exists, else return
    /// nullptr.
    ///
    USDVALIDATION_API
    const UsdValidationFixer *GetFixerByName(const TfToken &name) const;

    /// Return a vector of immutable fixers catering to a specific 
    /// \p errorName.
    ///
    USDVALIDATION_API
    const std::vector<const UsdValidationFixer *> 
    GetFixersByErrorName() const;

    /// Return an immutable fixer given its \p name and catering to a specific
    /// error name if it exists, else return nullptr.
    /// 
    USDVALIDATION_API
    const UsdValidationFixer *GetFixerByNameAndErrorName(
        const TfToken &name) const;

    /// Return a vector of immutable fixers catering to any of the given
    /// \p keywords.
    ///
    /// Fixers can be associated with keywords, like unit, department, etc. which
    /// can be used to filter fixers based on the context in which they are
    /// being queried.
    USDVALIDATION_API
    const std::vector<const UsdValidationFixer *> GetFixersByKeywords(
        const TfTokenVector &keywords) const;


private:
    // UsdValidationValidatorError holds a pointer to the UsdValidationValidator
    // that generated it, so we need to provide friend access to allow the
    // necessary mutation.
    friend class UsdValidationValidator;

    // Used by UsdValidationValidator::Validate methods to embed itself to the
    // reported errors.
    void _SetValidator(const UsdValidationValidator *validator);

    // _validator is set when ValidationError is generated via a
    // UsdValidationValidator::Validate() call.
    const UsdValidationValidator *_validator;

    // These data members should not be modified other than during
    // initialization by the validate task functions.
    TfToken _name;
    UsdValidationErrorType _errorType;
    UsdValidationErrorSites _errorSites;
    std::string _errorMsg;
    VtValue _metadata;

}; // UsdValidationError

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_VALIDATION_USD_VALIDATION_ERROR_H
