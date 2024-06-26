//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/pxr.h"
#include "pxr/base/tf/pyModule.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_WRAP_MODULE
{
    TF_WRAP(UsdCommon);
    TF_WRAP(UsdNotice);
    TF_WRAP(UsdTimeCode);
    TF_WRAP(UsdTokens);
    TF_WRAP(UsdInterpolationType);

    // UsdObject and its subclasses.
    TF_WRAP(UsdObject); 
    TF_WRAP(UsdProperty);
    TF_WRAP(UsdAttribute);
    TF_WRAP(UsdRelationship);
    TF_WRAP(UsdPrim);

    // Value types.
    TF_WRAP(UsdEditTarget);
    TF_WRAP(UsdEditContext);
    TF_WRAP(UsdInherits);
    TF_WRAP(UsdPayloads);
    TF_WRAP(UsdPrimDefinition);
    TF_WRAP(UsdPrimFlags);
    TF_WRAP(UsdPrimTypeInfo);
    TF_WRAP(UsdReferences);
    TF_WRAP(UsdResolveTarget);
    TF_WRAP(UsdSchemaRegistry);
    TF_WRAP(UsdSpecializes);
    TF_WRAP(UsdPrimRange);
    TF_WRAP(UsdVariantSets);

    // SchemaBase, APISchemaBase and subclasses.
    TF_WRAP(UsdSchemaBase);
    TF_WRAP(UsdAPISchemaBase);
    TF_WRAP(UsdTyped);

    // Stage and Stage Cache
    TF_WRAP(UsdStage);
    TF_WRAP(UsdStageCache);
    TF_WRAP(UsdStageCacheContext);
    TF_WRAP(UsdStageLoadRules);
    TF_WRAP(UsdStagePopulationMask);

    // Generated schema.
    TF_WRAP(UsdClipsAPI);
    TF_WRAP(UsdCollectionAPI);
    TF_WRAP(UsdModelAPI);

    // Miscellaenous classes
    TF_WRAP(UsdAttributeQuery);
    TF_WRAP(UsdCollectionMembershipQuery);
    TF_WRAP(UsdCrateInfo);
    TF_WRAP(UsdFileFormat);
    TF_WRAP(UsdNamespaceEditor);
    TF_WRAP(UsdResolveInfo);
    TF_WRAP(Version);
    TF_WRAP(UsdZipFile);
    TF_WRAP(UsdPrimCompositionQueryArc);
    TF_WRAP(UsdPrimCompositionQuery);
    TF_WRAP(UsdFlattenUtils);
}
