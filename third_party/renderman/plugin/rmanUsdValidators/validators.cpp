//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/property.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usdRender/settings.h"
#include "pxr/usd/usdRi/tokens.h"
#include "pxr/usd/usdShade/tokens.h"

#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/fixer.h"
#include "pxr/usdValidation/usdValidation/registry.h"
#include "pxr/usdValidation/usdValidation/timeRange.h"
#include "rmanUsdValidators/validatorTokens.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _terminalsTokens,
    (PxrRenderTerminalsAPI)
    (RenderSettings)
    ((OutputsRiIntegratorAttr, "outputs:ri:integrator"))
    ((OutputsRiDisplayFiltersAttr, "outputs:ri:displayFilters"))
    ((OutputsRiSampleFiltersAttr, "outputs:ri:sampleFilters"))
);

const std::vector<UsdValidationFixer>
_PxrRenderTerminalsApiRelationshipsFixers() 
{
    std::vector<UsdValidationFixer> fixers;

    FixerCanApplyFn fixerCanApplyFn = 
        [](const UsdValidationError &error, const UsdEditTarget &editTarget,
           const UsdTimeCode &/*timeCode*/) -> bool {
            if (!editTarget.IsValid() || !editTarget.GetLayer()) {
                return false;
            }
            if (error.GetSites().size() != 1) {
                // Must have one and only one error site to fix
                return false;
            }
            const UsdValidationErrorSite &site = error.GetSites().front();
            if (!site.IsValid() || !site.IsPrim()) {
                return false;
            }
            UsdPrim prim = site.GetPrim();
            return prim.IsA<UsdRenderSettings>()
                && prim.HasAPI(_terminalsTokens->PxrRenderTerminalsAPI);
        };

    FixerImplFn fixerImplFn = 
        [](const UsdValidationError &error, const UsdEditTarget &editTarget,
           const UsdTimeCode &/*timeCode*/) -> bool {
            if (!editTarget.IsValid() || !editTarget.GetLayer()) {
                return false;
            }
            if (error.GetSites().size() != 1) {
                // Must have one and only one error site to fix
                return false;
            }
            const UsdValidationErrorSite &site = error.GetSites()[0];
            if (!site.IsValid() || !site.IsPrim()) {
                return false;
            }
            UsdPrim prim = site.GetPrim();

            const std::vector<TfToken> unsupportedTerminalsAttrs = {
                _terminalsTokens->OutputsRiIntegratorAttr,
                _terminalsTokens->OutputsRiDisplayFiltersAttr,
                _terminalsTokens->OutputsRiSampleFiltersAttr};

            for (const TfToken& attrName : unsupportedTerminalsAttrs) {
                UsdAttribute attr = prim.GetAttribute(attrName);
                if (!attr || !attr.HasAuthoredConnections()) {
                    continue;
                }
                const TfToken relName = 
                    TfToken(SdfPath::StripPrefixNamespace(
                        attrName.GetString(),
                        UsdShadeTokens->outputs.GetString()).first);
                prim.CreateRelationship(relName);
                std::vector<SdfPath> targets;
                attr.GetConnections(&targets);
                UsdRelationship rel = prim.GetRelationship(relName);
                rel.SetTargets(targets);
                attr.ClearConnections();
            }

            return true;
        };

    fixers.emplace_back(
        TfToken("ConvertRenderTerminalsAttrsToRels"),
        "Converts PxrRenderTerminalsAPI unsupported attributes to relationships.",
        fixerImplFn, fixerCanApplyFn, TfTokenVector{}, 
        RmanUsdValidatorsErrorNameTokens->invalidRenderTerminalsAttr);

    return fixers;
}

static UsdValidationErrorVector
_PxrRenderTerminalsApiRelationships(
    const UsdPrim &usdPrim, 
    const UsdValidationTimeRange &/*timeRange*/)
{
    if (!usdPrim.IsA<UsdRenderSettings>()
        || !usdPrim.HasAPI(_terminalsTokens->PxrRenderTerminalsAPI)) {
        return {};
    }

    UsdValidationErrorVector errors;
    
    const std::vector<TfToken> unsupportedTerminalsAttrs = {
        _terminalsTokens->OutputsRiIntegratorAttr,
        _terminalsTokens->OutputsRiDisplayFiltersAttr,
        _terminalsTokens->OutputsRiSampleFiltersAttr};

    const std::string prefix = 
        UsdShadeTokens->outputs.GetString() +
        UsdRiTokens->renderContext.GetString();

    const std::vector<UsdProperty> properties
        = usdPrim.GetPropertiesInNamespace(prefix);
    for (const UsdProperty& prop : properties) {
        UsdAttribute attr = prop.As<UsdAttribute>();
        if (!attr || !attr.HasAuthoredConnections()) {
            continue;
        }
        const TfToken propName = attr.GetName();

        for (const TfToken& attrName : unsupportedTerminalsAttrs) {
            if (propName == attrName) {
                errors.emplace_back(
                    RmanUsdValidatorsErrorNameTokens->invalidRenderTerminalsAttr,
                    UsdValidationErrorType::Warn,
                    UsdValidationErrorSites { UsdValidationErrorSite(
                        usdPrim.GetStage(), usdPrim.GetPath()) },
                    TfStringPrintf(("Found a PxrRenderTerminalsAPI "
                                    "unsupported attribute (%s) that should "
                                    "instead be a relationship; see the "
                                    "schema for more info."),
                                    propName.GetText()));
            }
        }
    }

    return errors;
}

TF_REGISTRY_FUNCTION(UsdValidationRegistry)
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();

    registry.RegisterPluginValidator(
        RmanUsdValidatorsNameTokens->PxrRenderTerminalsAPIRelationships,
        _PxrRenderTerminalsApiRelationships,
        _PxrRenderTerminalsApiRelationshipsFixers());
}

PXR_NAMESPACE_CLOSE_SCOPE
