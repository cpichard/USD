//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/usd/pcp/errors.h"

#include "pxr/base/tf/enum.h"
#include "pxr/base/tf/stringUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

///////////////////////////////////////////////////////////////////////////////

TF_REGISTRY_FUNCTION(TfEnum) {
    TF_ADD_ENUM_NAME(PcpErrorType_ArcCycle);
    TF_ADD_ENUM_NAME(PcpErrorType_ArcPermissionDenied);
    TF_ADD_ENUM_NAME(PcpErrorType_IndexCapacityExceeded);
    TF_ADD_ENUM_NAME(PcpErrorType_ArcCapacityExceeded);
    TF_ADD_ENUM_NAME(PcpErrorType_ArcNamespaceDepthCapacityExceeded);
    TF_ADD_ENUM_NAME(PcpErrorType_InconsistentPropertyType);
    TF_ADD_ENUM_NAME(PcpErrorType_InconsistentAttributeType);
    TF_ADD_ENUM_NAME(PcpErrorType_InconsistentAttributeVariability);
    TF_ADD_ENUM_NAME(PcpErrorType_InternalAssetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidPrimPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidAssetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidInstanceTargetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidExternalTargetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidTargetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidReferenceOffset);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidSublayerOffset);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidSublayerOwnership);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidSublayerPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidVariantSelection);
    TF_ADD_ENUM_NAME(PcpErrorType_MutedAssetPath);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidAuthoredRelocation);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidConflictingRelocation);
    TF_ADD_ENUM_NAME(PcpErrorType_InvalidSameTargetRelocations);
    TF_ADD_ENUM_NAME(PcpErrorType_OpinionAtRelocationSource);
    TF_ADD_ENUM_NAME(PcpErrorType_PrimPermissionDenied);
    TF_ADD_ENUM_NAME(PcpErrorType_PropertyPermissionDenied);
    TF_ADD_ENUM_NAME(PcpErrorType_SublayerCycle);
    TF_ADD_ENUM_NAME(PcpErrorType_TargetPermissionDenied);
    TF_ADD_ENUM_NAME(PcpErrorType_UnresolvedPrimPath);
    TF_ADD_ENUM_NAME(PcpErrorType_VariableExpressionError);
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorBase::PcpErrorBase(PcpErrorType errorType) :
    errorType(errorType)
{
}

// virtual
PcpErrorBase::~PcpErrorBase()
{
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorArcCyclePtr
PcpErrorArcCycle::New()
{
    return PcpErrorArcCyclePtr(new PcpErrorArcCycle);
}

PcpErrorArcCycle::PcpErrorArcCycle() :
    PcpErrorBase(PcpErrorType_ArcCycle)
{
}

PcpErrorArcCycle::~PcpErrorArcCycle()
{
}

// virtual
std::string
PcpErrorArcCycle::ToString() const
{
    if (cycle.empty())
        return std::string();

    std::string msg = "Cycle detected:\n";
    for (size_t i = 0; i < cycle.size(); i++) {
        const PcpSiteTrackerSegment &segment = cycle[i];
        if (i > 0) {
            if (i + 1 < cycle.size()) {
                switch(segment.arcType) {
                case PcpArcTypeInherit:
                    msg += "inherits from:\n";
                    break;
                case PcpArcTypeRelocate:
                    msg += "is relocated from:\n";
                    break;
                case PcpArcTypeVariant:
                    msg += "uses variant:\n";
                    break;
                case PcpArcTypeReference:
                    msg += "references:\n";
                    break;
                case PcpArcTypePayload:
                    msg += "gets payload from:\n";
                    break;
                default:
                    msg += "refers to:\n";
                    break;
                }   
            }
            else {
                msg += "CANNOT ";
                switch(segment.arcType) {
                case PcpArcTypeInherit:
                    msg += "inherit from:\n";
                    break;
                case PcpArcTypeRelocate:
                    msg += "be relocated from:\n";
                    break;
                case PcpArcTypeVariant:
                    msg += "use variant:\n";
                    break;
                case PcpArcTypeReference:
                    msg += "reference:\n";
                    break;
                case PcpArcTypePayload:
                    msg += "get payload from:\n";
                    break;
                default:
                    msg += "refer to:\n";
                    break;                
                }
            }
        }
        msg += TfStringPrintf("%s\n", TfStringify(segment.site).c_str());
        if ((i > 0) && (i + 1 < cycle.size())) 
            msg += "which ";
    }    
    return msg;
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorArcPermissionDeniedPtr
PcpErrorArcPermissionDenied::New()
{
    return PcpErrorArcPermissionDeniedPtr(new PcpErrorArcPermissionDenied);
}

PcpErrorArcPermissionDenied::PcpErrorArcPermissionDenied() :
    PcpErrorBase(PcpErrorType_ArcPermissionDenied)
{
}

PcpErrorArcPermissionDenied::~PcpErrorArcPermissionDenied()
{
}

// virtual
std::string
PcpErrorArcPermissionDenied::ToString() const
{
    std::string msg = TfStringPrintf("%s\nCANNOT ", TfStringify(site).c_str());
    switch(arcType) {
    case PcpArcTypeInherit:
        msg += "inherit from:\n";
        break;
    case PcpArcTypeRelocate:
        msg += "be relocated from:\n";
        break;
    case PcpArcTypeVariant:
        msg += "use variant:\n";
        break;
    case PcpArcTypeReference:
        msg += "reference:\n";
        break;
    case PcpArcTypePayload:
        msg += "get payload from:\n";
        break;
    default:
        msg += "refer to:\n";
        break;
    }   
    msg += TfStringPrintf("%s\nwhich is private.", 
                          TfStringify(privateSite).c_str());
    return msg;
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorArcToProhibitedChildPtr
PcpErrorArcToProhibitedChild::New()
{
    return PcpErrorArcToProhibitedChildPtr(
        new PcpErrorArcToProhibitedChild);
}

PcpErrorArcToProhibitedChild::PcpErrorArcToProhibitedChild() :
    PcpErrorBase(PcpErrorType_ArcToProhibitedChild)
{
}

PcpErrorArcToProhibitedChild::
~PcpErrorArcToProhibitedChild()
{
}

// virtual
std::string
PcpErrorArcToProhibitedChild::ToString() const
{
    std::string msg = TfStringPrintf("%s\nCANNOT ", TfStringify(site).c_str());
    switch(arcType) {
    case PcpArcTypeInherit:
        msg += "inherit from:\n";
        break;
    case PcpArcTypeRelocate:
        msg += "be relocated from:\n";
        break;
    case PcpArcTypeVariant:
        msg += "use variant:\n";
        break;
    case PcpArcTypeReference:
        msg += "reference:\n";
        break;
    case PcpArcTypePayload:
        msg += "get payload from:\n";
        break;
    default:
        msg += "refer to:\n";
        break;
    }   
    msg += TfStringPrintf("%s\nwhich is a prohibited child of its parent "
        "because it would require allowing opinions from the source of a "
        "relocation at %s.", 
        TfStringify(targetSite).c_str(),
        TfStringify(relocationSourceSite).c_str());

    return msg;
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorCapacityExceededPtr
PcpErrorCapacityExceeded::New(PcpErrorType errorType)
{
    return PcpErrorCapacityExceededPtr(new PcpErrorCapacityExceeded(errorType));
}

PcpErrorCapacityExceeded::PcpErrorCapacityExceeded(PcpErrorType errorType) :
    PcpErrorBase(errorType)
{
}

PcpErrorCapacityExceeded::~PcpErrorCapacityExceeded()
{
}

// virtual
std::string
PcpErrorCapacityExceeded::ToString() const
{
    return std::string("Composition graph capacity exceeded: ") +
        TfEnum::GetDisplayName(errorType);
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInconsistentPropertyBase::PcpErrorInconsistentPropertyBase(
    PcpErrorType errorType)
    : PcpErrorBase(errorType)
{
}

// virtual
PcpErrorInconsistentPropertyBase::~PcpErrorInconsistentPropertyBase()
{
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInconsistentPropertyTypePtr
PcpErrorInconsistentPropertyType::New()
{
    return PcpErrorInconsistentPropertyTypePtr(
        new PcpErrorInconsistentPropertyType);
}

PcpErrorInconsistentPropertyType::PcpErrorInconsistentPropertyType() :
    PcpErrorInconsistentPropertyBase(PcpErrorType_InconsistentPropertyType)
{
}

PcpErrorInconsistentPropertyType::~PcpErrorInconsistentPropertyType()
{
}

// virtual
std::string
PcpErrorInconsistentPropertyType::ToString() const
{
    return TfStringPrintf(
        "The property <%s> has inconsistent spec types.  "
        "The defining spec is @%s@<%s> and is %s spec.  "
        "The conflicting spec is @%s@<%s> and is %s spec.  "
        "The conflicting spec will be ignored.",
        rootSite.path.GetString().c_str(),
        definingLayerIdentifier.c_str(),
        definingSpecPath.GetString().c_str(),
        (definingSpecType == SdfSpecTypeAttribute ? 
            "an attribute" : "a relationship"),
        conflictingLayerIdentifier.c_str(),
        conflictingSpecPath.GetString().c_str(),
        (conflictingSpecType == SdfSpecTypeAttribute ? 
            "an attribute" : "a relationship"));
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInconsistentAttributeTypePtr
PcpErrorInconsistentAttributeType::New()
{
    return PcpErrorInconsistentAttributeTypePtr(
        new PcpErrorInconsistentAttributeType);
}

PcpErrorInconsistentAttributeType::PcpErrorInconsistentAttributeType() :
    PcpErrorInconsistentPropertyBase(PcpErrorType_InconsistentAttributeType)
{
}

PcpErrorInconsistentAttributeType::~PcpErrorInconsistentAttributeType()
{
}

// virtual
std::string
PcpErrorInconsistentAttributeType::ToString() const
{
    return TfStringPrintf(
        "The attribute <%s> has specs with inconsistent value types.  "
        "The defining spec is @%s@<%s> with value type '%s'.  "
        "The conflicting spec is @%s@<%s> with value type '%s'.  " 
        "The conflicting spec will be ignored.",
        rootSite.path.GetString().c_str(),
        definingLayerIdentifier.c_str(),
        definingSpecPath.GetString().c_str(),
        definingValueType.GetText(),
        conflictingLayerIdentifier.c_str(),
        conflictingSpecPath.GetString().c_str(),
        conflictingValueType.GetText());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInconsistentAttributeVariabilityPtr
PcpErrorInconsistentAttributeVariability::New()
{
    return PcpErrorInconsistentAttributeVariabilityPtr(
        new PcpErrorInconsistentAttributeVariability);
}

PcpErrorInconsistentAttributeVariability::
PcpErrorInconsistentAttributeVariability() :
    PcpErrorInconsistentPropertyBase(
        PcpErrorType_InconsistentAttributeVariability)
{
}

PcpErrorInconsistentAttributeVariability::
~PcpErrorInconsistentAttributeVariability()
{
}

// virtual
std::string
PcpErrorInconsistentAttributeVariability::ToString() const
{
    return TfStringPrintf(
        "The attribute <%s> has specs with inconsistent variability.  "
        "The defining spec is @%s@<%s> with variability '%s'.  The "
        "conflicting spec is @%s@<%s> with variability '%s'.  The "
        "conflicting variability will be ignored.",
        rootSite.path.GetString().c_str(),
        definingLayerIdentifier.c_str(),
        definingSpecPath.GetString().c_str(),
        TfEnum::GetName(definingVariability).c_str(),
        conflictingLayerIdentifier.c_str(),
        conflictingSpecPath.GetString().c_str(),
        TfEnum::GetName(conflictingVariability).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidPrimPathPtr
PcpErrorInvalidPrimPath::New()
{
    return PcpErrorInvalidPrimPathPtr(new PcpErrorInvalidPrimPath);
}

PcpErrorInvalidPrimPath::PcpErrorInvalidPrimPath() :
    PcpErrorBase(PcpErrorType_InvalidPrimPath)
{
}

PcpErrorInvalidPrimPath::~PcpErrorInvalidPrimPath()
{
}

// virtual
std::string
PcpErrorInvalidPrimPath::ToString() const
{
    return TfStringPrintf("Invalid %s path <%s> introduced by %s"
                          "-- must be an absolute prim path with no "
                          "variant selections.", 
                          TfEnum::GetDisplayName(arcType).c_str(), 
                          primPath.GetText(),
                          TfStringify(PcpSite(sourceLayer, site.path)).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidAssetPathBase::PcpErrorInvalidAssetPathBase(
    PcpErrorType errorType)
    : PcpErrorBase(errorType)
{
}

// virtual
PcpErrorInvalidAssetPathBase::~PcpErrorInvalidAssetPathBase()
{
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidAssetPathPtr
PcpErrorInvalidAssetPath::New()
{
    return PcpErrorInvalidAssetPathPtr(new PcpErrorInvalidAssetPath);
}

PcpErrorInvalidAssetPath::PcpErrorInvalidAssetPath() :
    PcpErrorInvalidAssetPathBase(PcpErrorType_InvalidAssetPath)
{
}

PcpErrorInvalidAssetPath::~PcpErrorInvalidAssetPath()
{
}

// virtual
std::string
PcpErrorInvalidAssetPath::ToString() const
{
    return TfStringPrintf("Could not open asset @%s@ for "
                          "%s introduced by %s%s%s.",
                          resolvedAssetPath.c_str(), 
                          TfEnum::GetDisplayName(arcType).c_str(), 
                          TfStringify(PcpSite(sourceLayer, site.path)).c_str(),
                          messages.empty() ? "" : " -- ",
                          messages.c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorMutedAssetPathPtr
PcpErrorMutedAssetPath::New()
{
    return PcpErrorMutedAssetPathPtr(new PcpErrorMutedAssetPath);
}

PcpErrorMutedAssetPath::PcpErrorMutedAssetPath() :
    PcpErrorInvalidAssetPathBase(PcpErrorType_MutedAssetPath)
{
}

PcpErrorMutedAssetPath::~PcpErrorMutedAssetPath()
{
}

// virtual
std::string
PcpErrorMutedAssetPath::ToString() const
{
    return TfStringPrintf("Asset @%s@ was muted for %s introduced by %s.",
                          resolvedAssetPath.c_str(), 
                          TfEnum::GetDisplayName(arcType).c_str(), 
                          TfStringify(PcpSite(sourceLayer, site.path)).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorTargetPathBase::PcpErrorTargetPathBase(PcpErrorType errorType)
    : PcpErrorBase(errorType)
{
}

PcpErrorTargetPathBase::~PcpErrorTargetPathBase()
{
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidInstanceTargetPathPtr
PcpErrorInvalidInstanceTargetPath::New()
{
    return PcpErrorInvalidInstanceTargetPathPtr(
        new PcpErrorInvalidInstanceTargetPath);
}

PcpErrorInvalidInstanceTargetPath::PcpErrorInvalidInstanceTargetPath() 
    : PcpErrorTargetPathBase(PcpErrorType_InvalidInstanceTargetPath)
{
}

PcpErrorInvalidInstanceTargetPath::~PcpErrorInvalidInstanceTargetPath()
{
}

// virtual
std::string
PcpErrorInvalidInstanceTargetPath::ToString() const
{
    TF_VERIFY(ownerSpecType == SdfSpecTypeAttribute ||
              ownerSpecType == SdfSpecTypeRelationship);
    return TfStringPrintf(
        "The %s <%s> from <%s> in layer @%s@ is authored in a class "
        "but refers to an instance of that class.  Ignoring.",
        (ownerSpecType == SdfSpecTypeAttribute
            ? "attribute connection"
            : "relationship target"),
        targetPath.GetText(),
        owningPath.GetText(),
        layer->GetIdentifier().c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidExternalTargetPathPtr
PcpErrorInvalidExternalTargetPath::New()
{
    return PcpErrorInvalidExternalTargetPathPtr(
        new PcpErrorInvalidExternalTargetPath);
}

PcpErrorInvalidExternalTargetPath::PcpErrorInvalidExternalTargetPath() 
    : PcpErrorTargetPathBase(PcpErrorType_InvalidExternalTargetPath)
{
}

PcpErrorInvalidExternalTargetPath::~PcpErrorInvalidExternalTargetPath()
{
}

// virtual
std::string
PcpErrorInvalidExternalTargetPath::ToString() const
{
    TF_VERIFY(ownerSpecType == SdfSpecTypeAttribute ||
              ownerSpecType == SdfSpecTypeRelationship);
    return TfStringPrintf("The %s <%s> from <%s> in layer @%s@ refers "
                          "to a path outside the scope of the %s from <%s>.  "
                          "Ignoring.",
                          (ownerSpecType == SdfSpecTypeAttribute
                           ? "attribute connection"
                           : "relationship target"),
                          targetPath.GetText(), 
                          owningPath.GetText(),
                          layer->GetIdentifier().c_str(),
                          TfEnum::GetDisplayName(TfEnum(ownerArcType)).c_str(),
                          ownerIntroPath.GetText());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidTargetPathPtr
PcpErrorInvalidTargetPath::New()
{
    return PcpErrorInvalidTargetPathPtr(
        new PcpErrorInvalidTargetPath);
}

PcpErrorInvalidTargetPath::PcpErrorInvalidTargetPath() 
    : PcpErrorTargetPathBase(PcpErrorType_InvalidTargetPath)
{
}

PcpErrorInvalidTargetPath::~PcpErrorInvalidTargetPath()
{
}

// virtual
std::string
PcpErrorInvalidTargetPath::ToString() const
{
    TF_VERIFY(ownerSpecType == SdfSpecTypeAttribute ||
              ownerSpecType == SdfSpecTypeRelationship);
    return TfStringPrintf(
        "The %s <%s> from <%s> in layer @%s@ is invalid.  This may be "
        "because the path is the pre-relocated source path of a "
        "relocated prim.  Ignoring.",
        (ownerSpecType == SdfSpecTypeAttribute
            ? "attribute connection"
            : "relationship target"),
        targetPath.GetText(),
        owningPath.GetText(),
        layer->GetIdentifier().c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidSublayerOffsetPtr
PcpErrorInvalidSublayerOffset::New()
{
    return PcpErrorInvalidSublayerOffsetPtr(new PcpErrorInvalidSublayerOffset);
}

PcpErrorInvalidSublayerOffset::PcpErrorInvalidSublayerOffset() :
    PcpErrorBase(PcpErrorType_InvalidSublayerOffset)
{
}

PcpErrorInvalidSublayerOffset::~PcpErrorInvalidSublayerOffset()
{
}

// virtual
std::string
PcpErrorInvalidSublayerOffset::ToString() const
{
    return TfStringPrintf("Invalid sublayer offset %s in sublayer @%s@ of "
                          "layer @%s@. Using no offset instead.", 
                          TfStringify(offset).c_str(), 
                          sublayer->GetIdentifier().c_str(), 
                          layer->GetIdentifier().c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidReferenceOffsetPtr
PcpErrorInvalidReferenceOffset::New()
{
    return PcpErrorInvalidReferenceOffsetPtr(new PcpErrorInvalidReferenceOffset);
}

PcpErrorInvalidReferenceOffset::PcpErrorInvalidReferenceOffset() :
    PcpErrorBase(PcpErrorType_InvalidReferenceOffset)
{
}

PcpErrorInvalidReferenceOffset::~PcpErrorInvalidReferenceOffset()
{
}

// virtual
std::string
PcpErrorInvalidReferenceOffset::ToString() const
{
    return TfStringPrintf(
        "Invalid %s offset %s for @%s@<%s> introduced by %s. "
        "Using no offset instead.",
        TfEnum::GetDisplayName(arcType).c_str(), 
        TfStringify(offset).c_str(),
        assetPath.c_str(),
        targetPath.GetText(),
        TfStringify(PcpSite(sourceLayer, sourcePath)).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidSublayerOwnershipPtr
PcpErrorInvalidSublayerOwnership::New()
{
    return PcpErrorInvalidSublayerOwnershipPtr(
        new PcpErrorInvalidSublayerOwnership);
}

PcpErrorInvalidSublayerOwnership::PcpErrorInvalidSublayerOwnership() :
    PcpErrorBase(PcpErrorType_InvalidSublayerOwnership)
{
}

PcpErrorInvalidSublayerOwnership::~PcpErrorInvalidSublayerOwnership()
{
}

// virtual
std::string
PcpErrorInvalidSublayerOwnership::ToString() const
{
    std::vector<std::string> sublayerStrVec;
    TF_FOR_ALL(sublayer, sublayers) {
        sublayerStrVec.push_back("@" + (*sublayer)->GetIdentifier() + "@");
    }
    return TfStringPrintf("The following sublayers for layer @%s@ have the "
                          "same owner '%s': %s",
                          layer->GetIdentifier().c_str(),
                          owner.c_str(), 
                          TfStringJoin(sublayerStrVec, ", ").c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidSublayerPathPtr
PcpErrorInvalidSublayerPath::New()
{
    return PcpErrorInvalidSublayerPathPtr(new PcpErrorInvalidSublayerPath);
}

PcpErrorInvalidSublayerPath::PcpErrorInvalidSublayerPath() :
    PcpErrorBase(PcpErrorType_InvalidSublayerPath)
{
}

PcpErrorInvalidSublayerPath::~PcpErrorInvalidSublayerPath()
{
}

// virtual
std::string
PcpErrorInvalidSublayerPath::ToString() const
{
    return TfStringPrintf("Could not load sublayer @%s@ of layer @%s@%s%s; "
                          "skipping.", 
                          sublayerPath.c_str(), 
                          layer ? layer->GetIdentifier().c_str()
                                : "<NULL>",
                          messages.empty() ? "" : " -- ",
                          messages.c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorRelocationBase::PcpErrorRelocationBase(PcpErrorType errorType)
    : PcpErrorBase(errorType)
{
}

PcpErrorRelocationBase::~PcpErrorRelocationBase()
{
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidAuthoredRelocationPtr
PcpErrorInvalidAuthoredRelocation::New()
{
    return PcpErrorInvalidAuthoredRelocationPtr(
        new PcpErrorInvalidAuthoredRelocation);
}

PcpErrorInvalidAuthoredRelocation::PcpErrorInvalidAuthoredRelocation() :
    PcpErrorRelocationBase(PcpErrorType_InvalidAuthoredRelocation)
{
}

PcpErrorInvalidAuthoredRelocation::~PcpErrorInvalidAuthoredRelocation()
{
}

// virtual
std::string
PcpErrorInvalidAuthoredRelocation::ToString() const
{
    return TfStringPrintf("Relocation from <%s> to <%s> authored at @%s@<%s> is "
                          "invalid and will be ignored: %s",
                          sourcePath.GetText(),
                          targetPath.GetText(),
                          layer->GetIdentifier().c_str(), 
                          owningPath.GetText(),
                          messages.c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidConflictingRelocationPtr
PcpErrorInvalidConflictingRelocation::New()
{
    return PcpErrorInvalidConflictingRelocationPtr(
        new PcpErrorInvalidConflictingRelocation);
}

PcpErrorInvalidConflictingRelocation::PcpErrorInvalidConflictingRelocation() :
    PcpErrorRelocationBase(PcpErrorType_InvalidConflictingRelocation)
{
}

PcpErrorInvalidConflictingRelocation::~PcpErrorInvalidConflictingRelocation()
{
}

// virtual
std::string
PcpErrorInvalidConflictingRelocation::ToString() const
{
    auto conflictReasonCstr = [](ConflictReason reason) {
        switch (reason) {
            case ConflictReason::TargetIsConflictSource:
                return "The target of a relocate cannot be the source of "
                    "another relocate in the same layer stack.";
            case ConflictReason::SourceIsConflictTarget:
                return "The source of a relocate cannot be the target of "
                    "another relocate in the same layer stack.";
            case ConflictReason::TargetIsConflictSourceDescendant:
                return "The target of a relocate cannot be a descendant of "
                    "the source of another relocate.";
            case ConflictReason::SourceIsConflictSourceDescendant:
                return "The source of a relocate cannot be a descendant of "
                    "the source of another relocate.";
        };
        return "Invalid conflict reason.";
    };

    return TfStringPrintf("Relocation from <%s> to <%s> authored at @%s@<%s> "
                          "conflicts with another relocation from <%s> to <%s> "
                          "authored at @%s@<%s> and will be ignored: %s",
                          sourcePath.GetText(),
                          targetPath.GetText(),
                          layer->GetIdentifier().c_str(), 
                          owningPath.GetText(),
                          conflictSourcePath.GetText(),
                          conflictTargetPath.GetText(),
                          conflictLayer->GetIdentifier().c_str(), 
                          conflictOwningPath.GetText(),
                          conflictReasonCstr(conflictReason));
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorInvalidSameTargetRelocationsPtr
PcpErrorInvalidSameTargetRelocations::New()
{
    return PcpErrorInvalidSameTargetRelocationsPtr(
        new PcpErrorInvalidSameTargetRelocations);
}

PcpErrorInvalidSameTargetRelocations::PcpErrorInvalidSameTargetRelocations() :
    PcpErrorRelocationBase(PcpErrorType_InvalidSameTargetRelocations)
{
}

PcpErrorInvalidSameTargetRelocations::~PcpErrorInvalidSameTargetRelocations()
{
}

// virtual
std::string
PcpErrorInvalidSameTargetRelocations::ToString() const
{
    auto sourceToString = [](const RelocationSource &source) {
        return TfStringPrintf("relocation from <%s> authored at @%s@<%s>",
            source.sourcePath.GetText(),
            source.layer->GetIdentifier().c_str(),
            source.owningPath.GetText());
    };

    auto sourceIt = sources.begin();
    if (sourceIt == sources.end()) {
        TF_CODING_ERROR(
            "PcpErrorInvalidSameTargetRelocations must have sources");
        return std::string();
    }

    std::string sourcesString = sourceToString(*sourceIt);
    while (++sourceIt != sources.end()) {
        sourcesString.append("; ");
        sourcesString.append(sourceToString(*sourceIt));
    }

    return TfStringPrintf("The path <%s> is the target of multiple relocations "
                          "from different sources. The following relocates to "
                          "this target are invalid and will be ignored: %s.",
                          targetPath.GetText(),
                          sourcesString.c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorOpinionAtRelocationSourcePtr
PcpErrorOpinionAtRelocationSource::New()
{
    return PcpErrorOpinionAtRelocationSourcePtr(
        new PcpErrorOpinionAtRelocationSource);
}

PcpErrorOpinionAtRelocationSource::PcpErrorOpinionAtRelocationSource() :
    PcpErrorBase(PcpErrorType_OpinionAtRelocationSource)
{
}

PcpErrorOpinionAtRelocationSource::~PcpErrorOpinionAtRelocationSource()
{
}

// virtual
std::string
PcpErrorOpinionAtRelocationSource::ToString() const
{
    return TfStringPrintf("The layer @%s@ has an invalid opinion at the "
                          "relocation source path <%s>, which will be "
                          "ignored.",
                          layer->GetIdentifier().c_str(), 
                          path.GetText());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorPrimPermissionDeniedPtr
PcpErrorPrimPermissionDenied::New()
{
    return PcpErrorPrimPermissionDeniedPtr(new PcpErrorPrimPermissionDenied);
}

PcpErrorPrimPermissionDenied::PcpErrorPrimPermissionDenied() :
    PcpErrorBase(PcpErrorType_PrimPermissionDenied)
{
}

PcpErrorPrimPermissionDenied::~PcpErrorPrimPermissionDenied()
{
}

// virtual
std::string
PcpErrorPrimPermissionDenied::ToString() const
{
    return TfStringPrintf("%s\n"
                          "will be ignored because:\n"
                          "%s\n"
                          "is private and overrides its opinions.",
                          TfStringify(site).c_str(),
                          TfStringify(privateSite).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorPropertyPermissionDeniedPtr
PcpErrorPropertyPermissionDenied::New()
{
    return PcpErrorPropertyPermissionDeniedPtr(
        new PcpErrorPropertyPermissionDenied);
}

PcpErrorPropertyPermissionDenied::PcpErrorPropertyPermissionDenied() :
    PcpErrorBase(PcpErrorType_PropertyPermissionDenied)
{
}

PcpErrorPropertyPermissionDenied::~PcpErrorPropertyPermissionDenied()
{
}

// virtual
std::string
PcpErrorPropertyPermissionDenied::ToString() const
{
    return TfStringPrintf("The layer at @%s@ has an illegal opinion about "
                          "%s <%s> which is private across a reference, "
                          "inherit, or variant.  Ignoring.",
                          layerPath.c_str(),
                          propType == SdfSpecTypeAttribute ?
                          "an attribute" : "a relationship",
                          propPath.GetText());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorSublayerCyclePtr
PcpErrorSublayerCycle::New()
{
    return PcpErrorSublayerCyclePtr(new PcpErrorSublayerCycle);
}

PcpErrorSublayerCycle::PcpErrorSublayerCycle() :
    PcpErrorBase(PcpErrorType_SublayerCycle)
{
}

PcpErrorSublayerCycle::~PcpErrorSublayerCycle()
{
}

// virtual
std::string
PcpErrorSublayerCycle::ToString() const
{
    return TfStringPrintf("Sublayer hierarchy with root layer @%s@ has cycles. "
                          "Detected when layer @%s@ was seen in the layer "
                          "stack for the second time.", 
                          layer->GetIdentifier().c_str(), 
                          sublayer->GetIdentifier().c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorTargetPermissionDeniedPtr
PcpErrorTargetPermissionDenied::New()
{
    return PcpErrorTargetPermissionDeniedPtr(
        new PcpErrorTargetPermissionDenied);
}

PcpErrorTargetPermissionDenied::PcpErrorTargetPermissionDenied() 
    : PcpErrorTargetPathBase(PcpErrorType_TargetPermissionDenied)
{
}

PcpErrorTargetPermissionDenied::~PcpErrorTargetPermissionDenied()
{
}

// virtual
std::string
PcpErrorTargetPermissionDenied::ToString() const
{
    TF_VERIFY(ownerSpecType == SdfSpecTypeAttribute ||
              ownerSpecType == SdfSpecTypeRelationship);
    return TfStringPrintf(
        "The %s <%s> from <%s> in layer @%s@ targets an object that is "
        "private on the far side of a reference or inherit.  This %s "
        "will be ignored.",
        (ownerSpecType == SdfSpecTypeAttribute
            ? "attribute connection" : "relationship target"),
        targetPath.GetText(), 
        owningPath.GetText(),
        layer->GetIdentifier().c_str(),
        (ownerSpecType == SdfSpecTypeAttribute
            ? "connection" : "target"));
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorUnresolvedPrimPathPtr
PcpErrorUnresolvedPrimPath::New()
{
    return PcpErrorUnresolvedPrimPathPtr(new PcpErrorUnresolvedPrimPath);
}

PcpErrorUnresolvedPrimPath::PcpErrorUnresolvedPrimPath() :
    PcpErrorBase(PcpErrorType_UnresolvedPrimPath)
{
}

PcpErrorUnresolvedPrimPath::~PcpErrorUnresolvedPrimPath()
{
}

// virtual
std::string
PcpErrorUnresolvedPrimPath::ToString() const
{
    return TfStringPrintf(
        "Unresolved %s prim path %s introduced by %s",
        TfEnum::GetDisplayName(arcType).c_str(), 
        TfStringify(PcpSite(targetLayer, unresolvedPath)).c_str(),
        TfStringify(PcpSite(sourceLayer, site.path)).c_str());
}

///////////////////////////////////////////////////////////////////////////////

PcpErrorVariableExpressionErrorPtr
PcpErrorVariableExpressionError::New()
{
    return PcpErrorVariableExpressionErrorPtr(
        new PcpErrorVariableExpressionError);
}

PcpErrorVariableExpressionError::PcpErrorVariableExpressionError()
    : PcpErrorBase(PcpErrorType_VariableExpressionError)
{
}

PcpErrorVariableExpressionError::~PcpErrorVariableExpressionError()
{
}

std::string
PcpErrorVariableExpressionError::ToString() const
{
    // Example error messages:
    // Error evaluating expression "`if(${FOO}, ..."
    // for sublayer in @foo.usda@: invalid syntax
    //
    // Error evaluating expression "`if(${FOO}, ..."
    // for reference at </Foo> in @bar.usda@: invalid syntax
    auto makeSourceStr = [this]() {
        std::string result;
        if (!sourcePath.IsAbsoluteRootPath()) {
            result += TfStringPrintf(
                "at %s ", sourcePath.GetAsString().c_str());
        }
        result += TfStringPrintf(
            "in @%s@", 
            sourceLayer ? sourceLayer->GetIdentifier().c_str() : "<expired>");
        return result;

    };

    return TfStringPrintf(
        R"(Error evaluating expression %s for %s %s: %s)",
        expression.substr(0, 32).c_str(), 
        context.c_str(), makeSourceStr().c_str(),
        expressionError.c_str());
}

///////////////////////////////////////////////////////////////////////////////

void 
PcpRaiseErrors(const PcpErrorVector &errors)
{
    TF_FOR_ALL(err, errors) {
        TF_RUNTIME_ERROR("%s", (*err)->ToString().c_str());
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
