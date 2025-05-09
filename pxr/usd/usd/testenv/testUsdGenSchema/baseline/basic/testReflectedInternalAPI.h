//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDCONTRIVED_GENERATED_TESTREFLECTEDINTERNALAPI_H
#define USDCONTRIVED_GENERATED_TESTREFLECTEDINTERNALAPI_H

/// \file usdContrived/testReflectedInternalAPI.h

#include "pxr/pxr.h"
#include "pxr/usd/usdContrived/api.h"
#include "pxr/usd/usd/aPISchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdContrived/tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// TESTREFLECTEDINTERNALAPI                                                   //
// -------------------------------------------------------------------------- //

/// \class UsdContrivedTestReflectedInternalAPI
///
///
class UsdContrivedTestReflectedInternalAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdContrivedTestReflectedInternalAPI on UsdPrim \p prim .
    /// Equivalent to UsdContrivedTestReflectedInternalAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdContrivedTestReflectedInternalAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdContrivedTestReflectedInternalAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdContrivedTestReflectedInternalAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdContrivedTestReflectedInternalAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDCONTRIVED_API
    virtual ~UsdContrivedTestReflectedInternalAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDCONTRIVED_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdContrivedTestReflectedInternalAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdContrivedTestReflectedInternalAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDCONTRIVED_API
    static UsdContrivedTestReflectedInternalAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDCONTRIVED_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "TestReflectedInternalAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdContrivedTestReflectedInternalAPI object is returned upon success. 
    /// An invalid (or empty) UsdContrivedTestReflectedInternalAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDCONTRIVED_API
    static UsdContrivedTestReflectedInternalAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDCONTRIVED_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDCONTRIVED_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDCONTRIVED_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // TESTATTRINTERNAL 
    // --------------------------------------------------------------------- //
    /// 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `int testAttrInternal` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    USDCONTRIVED_API
    UsdAttribute GetTestAttrInternalAttr() const;

    /// See GetTestAttrInternalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDCONTRIVED_API
    UsdAttribute CreateTestAttrInternalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // TESTATTRDUPLICATE 
    // --------------------------------------------------------------------- //
    /// 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `int testAttrDuplicate` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    USDCONTRIVED_API
    UsdAttribute GetTestAttrDuplicateAttr() const;

    /// See GetTestAttrDuplicateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDCONTRIVED_API
    UsdAttribute CreateTestAttrDuplicateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // TESTRELINTERNAL 
    // --------------------------------------------------------------------- //
    /// 
    ///
    USDCONTRIVED_API
    UsdRelationship GetTestRelInternalRel() const;

    /// See GetTestRelInternalRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDCONTRIVED_API
    UsdRelationship CreateTestRelInternalRel() const;

public:
    // --------------------------------------------------------------------- //
    // TESTRELDUPLICATE 
    // --------------------------------------------------------------------- //
    /// 
    ///
    USDCONTRIVED_API
    UsdRelationship GetTestRelDuplicateRel() const;

    /// See GetTestRelDuplicateRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDCONTRIVED_API
    UsdRelationship CreateTestRelDuplicateRel() const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
