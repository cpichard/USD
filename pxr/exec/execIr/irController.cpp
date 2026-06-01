//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/execIr/irController.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<ExecIrIrController,
        TfType::Bases< UsdTyped > >();
    
}

/* virtual */
ExecIrIrController::~ExecIrIrController()
{
}

/* static */
ExecIrIrController
ExecIrIrController::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return ExecIrIrController();
    }
    return ExecIrIrController(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind ExecIrIrController::_GetSchemaKind() const
{
    return ExecIrIrController::schemaKind;
}

/* static */
const TfType &
ExecIrIrController::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<ExecIrIrController>();
    return tfType;
}

/* static */
bool 
ExecIrIrController::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
ExecIrIrController::_GetTfType() const
{
    return _GetStaticTfType();
}

/*static*/
const TfTokenVector&
ExecIrIrController::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdTyped::GetSchemaAttributeNames(true);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--
