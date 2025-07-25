//
// Copyright 2019 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"
#include "pxr/usd/usd/flattenUtils.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/changeBlock.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/relationshipSpec.h"
#include "pxr/usd/sdf/variableExpression.h"
#include "pxr/usd/sdf/variantSetSpec.h"
#include "pxr/usd/sdf/variantSpec.h"
#include "pxr/usd/sdf/pseudoRootSpec.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/pcp/composeSite.h"
#include "pxr/usd/pcp/expressionVariables.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/usd/common.h"
#include "pxr/usd/usd/clipsAPI.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/tokens.h"
#include "pxr/usd/usd/valueUtils.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/pathUtils.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

////////////////////////////////////////////////////////////////////////
// UsdFlattenLayerStack
//
// The approach to merge layer stacks into a single layer is as follows:
//
// - _FlattenSpecs() recurses down the typed hierarchy of specs,
//   using PcpComposeSiteChildNames() to discover child names
//   of each type of spec, and creating them in the output layer.
//
// - At each output site, _FlattenFields() flattens field data
//   using a _Reduce() helper to apply composition rules for
//   particular value types and fields.  It uses _ApplyLayerOffset()
//   to handle time-remapping needed, depending on the field.

template <typename T>
VtValue
_Reduce(const T &lhs, const T &rhs)
{
    // Generic base case: take stronger opinion.
    return VtValue(lhs);
}

// "Fix" a list op to only use composable features.
template <typename T>
SdfListOp<T>
_FixListOp(SdfListOp<T> op)
{
    if (op.IsExplicit()) {
        return op;
    }
    std::vector<T> items;
    items = op.GetAppendedItems();
    for (const T& item: op.GetAddedItems()) {
        if (std::find(items.begin(), items.end(), item) == items.end()) {
            items.push_back(item);
        }
    }
    op.SetAppendedItems(items);
    op.SetAddedItems(std::vector<T>());
    op.SetOrderedItems(std::vector<T>());
    return op;
}

// List ops that use added or reordered items cannot, in general, be
// composed into another listop. In those cases, we fall back to a
// best-effort approximation by discarding reorders and converting
// adds to appends.
static void
_FixListOpValue(VtValue *v)
{
#define FIX_LISTOP_TYPE(T) \
    if (v->IsHolding<T>()) { \
        *v = _FixListOp(v->UncheckedGet<T>()); \
        return; \
    }
    FIX_LISTOP_TYPE(SdfIntListOp);
    FIX_LISTOP_TYPE(SdfUIntListOp);
    FIX_LISTOP_TYPE(SdfInt64ListOp);
    FIX_LISTOP_TYPE(SdfUInt64ListOp);
    FIX_LISTOP_TYPE(SdfTokenListOp);
    FIX_LISTOP_TYPE(SdfStringListOp);
    FIX_LISTOP_TYPE(SdfPathListOp);
    FIX_LISTOP_TYPE(SdfPayloadListOp);
    FIX_LISTOP_TYPE(SdfReferenceListOp);
    FIX_LISTOP_TYPE(SdfUnregisteredValueListOp);
}

template <typename T>
VtValue
_Reduce(const SdfListOp<T> &lhs, const SdfListOp<T> &rhs)
{
    // We assume the caller has already applied _FixListOp()
    if (std::optional<SdfListOp<T>> r = lhs.ApplyOperations(rhs)) {
        return VtValue(*r);
    }
    // The approximation used should always be composable,
    // so error if that didn't work.
    TF_CODING_ERROR("Could not reduce listOp %s over %s",
                    TfStringify(lhs).c_str(), TfStringify(rhs).c_str());
    return VtValue();
}

template <>
VtValue
_Reduce(const VtDictionary &lhs, const VtDictionary &rhs)
{
    // Dictionaries compose keys recursively.
    return VtValue(VtDictionaryOverRecursive(lhs, rhs));
}

template <>
VtValue
_Reduce(const SdfVariantSelectionMap &lhs, const SdfVariantSelectionMap &rhs)
{
    SdfVariantSelectionMap result(rhs);
    for (auto const& entry: lhs) {
        result[entry.first] = entry.second;
    }
    return VtValue(result);
}

template <>
VtValue
_Reduce(const SdfRelocates &lhs, const SdfRelocates &rhs)
{
    SdfRelocates result(lhs);
    result.insert(result.end(), rhs.begin(), rhs.end());
    return VtValue::Take(result);
}

template <>
VtValue
_Reduce(const SdfSpecifier &lhs, const SdfSpecifier &rhs)
{
    // SdfSpecifierOver is the equivalent of "no opinion"
    //
    // Note that, in general, specifiers do not simply compose as
    // "strongest wins" -- see UsdStage::_GetPrimSpecifierImpl() for
    // details.  However, in the case of composing strictly within a
    // layer stack, they can be considered as strongest wins.
    return VtValue(lhs == SdfSpecifierOver ? rhs : lhs);
}

// This function is an overload that also accepts a field name.
VtValue
_Reduce(const VtValue &lhs, const VtValue &rhs, const TfToken &field)
{
    // Handle easy generic cases first.
    if (lhs.IsEmpty()) {
        return rhs;
    }
    if (rhs.IsEmpty()) {
        return lhs;
    }
    if (lhs.IsHolding<SdfValueBlock>() || rhs.IsHolding<SdfValueBlock>()) {
        // If the stronger value is a block, return it;
        // if the weaker value is a block, return the stronger value.
        return lhs;
    }
    if (lhs.IsHolding<SdfAnimationBlock>() && 
            (rhs.IsHolding<SdfTimeSampleMap>() || rhs.IsHolding<TsSpline>())) {
        // If stronger is an animation block and weaker is a time sample map or
        // spline, return the stronger value, that is, the animation block.
        return lhs;
    }
    if (lhs.IsHolding<SdfAnimationBlock>() &&
            !(rhs.IsHolding<SdfTimeSampleMap>() || rhs.IsHolding<TsSpline>())) {
        // If the stronger value is an animation block and the weaker value is
        // not a time sample map or spline (default values), return the
        // weaker default value.
        return rhs;
    }
    if (lhs.GetType() != rhs.GetType()) {
        // If the types do not match, there is no reduction rule for
        // combining them, so just use the stronger value.
        return lhs;
    }

    // Dispatch to type-specific reduce / compose rules.
    //
    // XXX WBN to have more generic (i.e. automatically extended)
    // way to handle listop types in case we add more in the future.
    // Maybe sdf/types.h could provide a type-list we could iterate over?
#define TYPE_DISPATCH(T) \
    if (lhs.IsHolding<T>()) { \
        return _Reduce(lhs.UncheckedGet<T>(),  \
                       rhs.UncheckedGet<T>()); \
    }
    TYPE_DISPATCH(SdfSpecifier);
    TYPE_DISPATCH(SdfIntListOp);
    TYPE_DISPATCH(SdfUIntListOp);
    TYPE_DISPATCH(SdfInt64ListOp);
    TYPE_DISPATCH(SdfUInt64ListOp);
    TYPE_DISPATCH(SdfTokenListOp);
    TYPE_DISPATCH(SdfStringListOp);
    TYPE_DISPATCH(SdfPathListOp);
    TYPE_DISPATCH(SdfPayloadListOp);
    TYPE_DISPATCH(SdfReferenceListOp);
    TYPE_DISPATCH(SdfUnregisteredValueListOp);
    TYPE_DISPATCH(VtDictionary);
    TYPE_DISPATCH(SdfRelocates);
    TYPE_DISPATCH(SdfTimeSampleMap);
    TYPE_DISPATCH(TsSpline);
    TYPE_DISPATCH(SdfVariantSelectionMap);
#undef TYPE_DISPATCH

    // TypeName is a special case: empty token represents "no opinion".
    // (That is not true of token-valued fields in general.)
    if (field == SdfFieldKeys->TypeName && lhs.IsHolding<TfToken>()) {
        return lhs.UncheckedGet<TfToken>().IsEmpty() ? rhs : lhs;
    }

    // Generic base case: take stronger opinion.
    return lhs;
}

static void
_ApplyLayerOffsetToClipInfo(
    const SdfLayerOffset &offset,
    const TfToken& infoKey, VtDictionary* clipInfo)
{
    VtValue* v = TfMapLookupPtr(*clipInfo, infoKey);
    if (v && v->IsHolding<VtVec2dArray>()) {
        VtVec2dArray array;
        v->Swap(array);
        for (auto& entry : array) {
            entry[0] = offset * entry[0];
        }
        v->Swap(array);
    }
}

template <class RefOrPayloadType>
static std::optional<RefOrPayloadType>
_ApplyLayerOffsetToRefOrPayload(const SdfLayerOffset &offset,
                                const RefOrPayloadType &refOrPayload)
{
    RefOrPayloadType result = refOrPayload;
    result.SetLayerOffset(offset * refOrPayload.GetLayerOffset());
    return std::optional<RefOrPayloadType>(result);
}

// Apply layer offsets (time remapping) to time-keyed metadata.
static void
_ApplyLayerOffset(const SdfLayerOffset &offset,
                  const TfToken &field, 
                  VtValue *val)
{
    if (offset.IsIdentity()) {
        return;
    }

    if (field == UsdTokens->clips) {
        if (val->IsHolding<VtDictionary>()) {
            VtDictionary clips = val->UncheckedGet<VtDictionary>();
            for (auto &entry: clips) {
                VtValue& clipInfoVal = entry.second;
                if (!clipInfoVal.IsHolding<VtDictionary>()) {
                    // No point is adding a warning here, as if we hit this
                    // condition here, we will also hit it in _FixAssetPaths
                    // which will generate the warning.
                    continue;
                }
                VtDictionary clipInfo =
                    clipInfoVal.UncheckedGet<VtDictionary>();
                _ApplyLayerOffsetToClipInfo(
                    offset, UsdClipsAPIInfoKeys->active, &clipInfo);
                _ApplyLayerOffsetToClipInfo(
                    offset, UsdClipsAPIInfoKeys->times, &clipInfo);
                clipInfoVal = VtValue(clipInfo);
            }
            val->Swap(clips);
        }
    }
    else if (field == SdfFieldKeys->References) {
        if (val->IsHolding<SdfReferenceListOp>()) {
            SdfReferenceListOp refs = val->UncheckedGet<SdfReferenceListOp>();
            refs.ModifyOperations(std::bind(
                _ApplyLayerOffsetToRefOrPayload<SdfReference>, 
                offset, std::placeholders::_1));
            val->Swap(refs);
        }
    }
    else if (field == SdfFieldKeys->Payload) {
        if (val->IsHolding<SdfPayloadListOp>()) {
            SdfPayloadListOp pls = val->UncheckedGet<SdfPayloadListOp>();
            pls.ModifyOperations(std::bind(
                _ApplyLayerOffsetToRefOrPayload<SdfPayload>, 
                offset, std::placeholders::_1));
            val->Swap(pls);
        }
    } else {
        Usd_ApplyLayerOffsetToValue(val, offset);
    }
}

using _ResolveAssetPathFn = std::function<
    std::string(const SdfLayerHandle& sourceLayer,
                const std::string& assetPath)>;

template <class RefOrPayloadType>
static std::optional<RefOrPayloadType>
_FixReferenceOrPayload(const _ResolveAssetPathFn& resolveAssetPathFn,
                       const SdfLayerHandle &sourceLayer,
                       const RefOrPayloadType &refOrPayload)
{
    RefOrPayloadType result = refOrPayload;
    result.SetAssetPath(
        resolveAssetPathFn(sourceLayer, refOrPayload.GetAssetPath()));
    return std::optional<RefOrPayloadType>(result);
}

static void
_FixAssetPaths(const SdfLayerHandle &sourceLayer,
               const TfToken &field,
               const _ResolveAssetPathFn& resolveAssetPathFn,
               VtValue *val)
{
    static auto updateAssetPathFn = [](
        const SdfLayerHandle &sourceLayer,
        const _ResolveAssetPathFn& resolveAssetPathFn,
        VtValue &val) {
            SdfAssetPath ap;
            val.Swap(ap);
            ap = SdfAssetPath(
                resolveAssetPathFn(sourceLayer, ap.GetAssetPath()));
            val.Swap(ap);
        };
    static auto updateAssetPathArrayFn = [](
        const SdfLayerHandle &sourceLayer,
        const _ResolveAssetPathFn& resolveAssetPathFn,
        VtValue &val) {
            VtArray<SdfAssetPath> a;
            val.Swap(a);
            for (SdfAssetPath &ap: a) {
                ap = SdfAssetPath(
                    resolveAssetPathFn(sourceLayer, ap.GetAssetPath()));
            }
            val.Swap(a);
        };

    if (val->IsHolding<SdfAssetPath>()) {
        updateAssetPathFn(sourceLayer, resolveAssetPathFn, *val);
        return;
    }
    else if (val->IsHolding<VtArray<SdfAssetPath>>()) {
        updateAssetPathArrayFn(sourceLayer, resolveAssetPathFn, *val);
        return;
    }
    else if (val->IsHolding<SdfTimeSampleMap>()) {
        const SdfTimeSampleMap &tsmc = val->UncheckedGet<SdfTimeSampleMap>();
        if (!tsmc.empty()) {
            // Quick test that the first entry of the time sample map is
            // holding either an asset path or an array of asset paths.
            bool holdingAssetPath = 
                tsmc.begin()->second.IsHolding<SdfAssetPath>();
            bool holdingAssetPathArray = 
                tsmc.begin()->second.IsHolding<VtArray<SdfAssetPath>>();
            if (holdingAssetPath || holdingAssetPathArray) {
                SdfTimeSampleMap tsmap;
                val->Swap(tsmap);
                // Go through each time sampled value and execute the resolve
                // function on each asset path (or array of asset paths).
                if (holdingAssetPath) {
                    for (auto &ts : tsmap) {
                        updateAssetPathFn(
                            sourceLayer, resolveAssetPathFn, ts.second);
                    }
                }
                else { // holdingAssetPathArray must be true
                    for (auto &ts : tsmap) {
                        updateAssetPathArrayFn(
                            sourceLayer, resolveAssetPathFn, ts.second);
                    }
                }
                val->Swap(tsmap);
            }
        }
    }
    else if (val->IsHolding<SdfReference>()) {
        SdfReference ref;
        val->Swap(ref);
        ref = *_FixReferenceOrPayload(resolveAssetPathFn, sourceLayer, ref);
        val->Swap(ref);
        return;
    }
    else if (val->IsHolding<SdfReferenceListOp>()) {
        SdfReferenceListOp refs;
        val->Swap(refs);
        refs.ModifyOperations(std::bind(_FixReferenceOrPayload<SdfReference>,
                    resolveAssetPathFn, sourceLayer, std::placeholders::_1));
        val->Swap(refs);
        return;
    }
    else if (val->IsHolding<SdfPayload>()) {
        SdfPayload pl;
        val->Swap(pl);
        pl = *_FixReferenceOrPayload(resolveAssetPathFn, sourceLayer, pl);
        val->Swap(pl);
        return;
    }
    else if (val->IsHolding<SdfPayloadListOp>()) {
        SdfPayloadListOp pls;
        val->Swap(pls);
        pls.ModifyOperations(std::bind(_FixReferenceOrPayload<SdfPayload>,
                    resolveAssetPathFn, sourceLayer, std::placeholders::_1));
        val->Swap(pls);
        return;
    }
    else if (field == UsdTokens->clips) {
        if (val->IsHolding<VtDictionary>()) {
            VtDictionary clips = val->UncheckedGet<VtDictionary>();
            for (auto &entry: clips) {
                const std::string& clipSetName = entry.first;
                VtValue& clipInfoVal = entry.second;
                if (!clipInfoVal.IsHolding<VtDictionary>()) {
                    TF_WARN("Expected dictionary for entry '%s' in 'clips'",
                            clipSetName.c_str());
                    continue;
                }
                VtDictionary clipInfo =
                    clipInfoVal.UncheckedGet<VtDictionary>();
                VtValue* v;
                v = TfMapLookupPtr(clipInfo,
                    UsdClipsAPIInfoKeys->assetPaths);
                if (v && v->IsHolding<VtArray<SdfAssetPath>>()) {
                    _FixAssetPaths(sourceLayer,
                        UsdClipsAPIInfoKeys->assetPaths,
                        resolveAssetPathFn, v);
                }
                v = TfMapLookupPtr(clipInfo,
                    UsdClipsAPIInfoKeys->manifestAssetPath);
                if (v && v->IsHolding<SdfAssetPath>()) {
                    _FixAssetPaths(sourceLayer,
                        UsdClipsAPIInfoKeys->manifestAssetPath,
                        resolveAssetPathFn, v);
                }
                clipInfoVal = VtValue(clipInfo);
            }
            val->Swap(clips);
        }
    }
}

// List of fields that we do not want to flatten generically.
TF_MAKE_STATIC_DATA(std::set<TfToken>, _fieldsToSkip) {
    // SdfChildrenKeys fields are maintained internally by Sdf.
    _fieldsToSkip->insert(SdfChildrenKeys->allTokens.begin(),
                           SdfChildrenKeys->allTokens.end());
    // We need to go through the SdfListEditorProxy API to
    // properly create attribute connections and rel targets,
    // so don't process the fields.
    _fieldsToSkip->insert(SdfFieldKeys->TargetPaths);
    _fieldsToSkip->insert(SdfFieldKeys->ConnectionPaths);
    // We flatten out sublayers, so discard them.
    _fieldsToSkip->insert(SdfFieldKeys->SubLayers);
    _fieldsToSkip->insert(SdfFieldKeys->SubLayerOffsets);
    // TimeSamples may be masked by Defaults, so handle them separately.
    _fieldsToSkip->insert(SdfFieldKeys->TimeSamples);
    // Splines may also be masked by Defaults, so handle them separately.
    _fieldsToSkip->insert(SdfFieldKeys->Spline);
}

static VtValue
_ReduceField(const PcpLayerStackRefPtr &layerStack,
             const SdfSpecHandle &targetSpec,
             const TfToken &field,
             const _ResolveAssetPathFn& resolveAssetPathFn)
{
    const SdfLayerRefPtrVector &layers = layerStack->GetLayers();
    const SdfPath &path = targetSpec->GetPath();
    const SdfSpecType specType = targetSpec->GetSpecType();

    VtValue val;
    for (size_t i=0; i < layers.size(); ++i) {
        if (!layers[i]->HasSpec(path)) {
            continue;
        }
        // Ignore mismatched specs (which should be very rare).
        // An example would a property that is declared as an
        // attribute in one layer, and a relationship in another.
        if (layers[i]->GetSpecType(path) != specType) {
            TF_WARN("UsdFlattenLayerStack: Ignoring spec at "
                    "<%s> in @%s@: expected spec type %s but found %s",
                    path.GetText(),
                    layers[i]->GetIdentifier().c_str(),
                    TfStringify(specType).c_str(),
                    TfStringify(layers[i]->GetSpecType(path)).c_str());
            continue;
        }
        VtValue layerVal;
        if (!layers[i]->HasField(path, field, &layerVal)) {
            continue;
        }
        // Apply layer offsets.
        if (const SdfLayerOffset *offset =
            layerStack->GetLayerOffsetForLayer(i)) {
            _ApplyLayerOffset(*offset, field, &layerVal);
        }
        // Fix asset paths.
        _FixAssetPaths(layers[i], field, resolveAssetPathFn, &layerVal);
        // Fix any list ops
        _FixListOpValue(&layerVal);
        val = _Reduce(val, layerVal, field);
    }
    return val;
}

static void
_FlattenFields(const PcpLayerStackRefPtr &layerStack,
               const SdfSpecHandle &targetSpec,
               const _ResolveAssetPathFn& resolveAssetPathFn)
{
    const SdfLayerRefPtrVector &layers = layerStack->GetLayers();
    const SdfSchemaBase &schema = targetSpec->GetLayer()->GetSchema();
    const SdfSpecType specType = targetSpec->GetSpecType();
    const SdfPath &path = targetSpec->GetPath();
    for (const TfToken &field: schema.GetFields(specType)) {
        if (_fieldsToSkip->find(field) != _fieldsToSkip->end()) {
            continue;
        }
        VtValue val = _ReduceField(
                layerStack, targetSpec, field, resolveAssetPathFn);
        targetSpec->GetLayer()->SetField(path, field, val);
    }
    if (specType == SdfSpecTypeAttribute) {
        // Only flatten TimeSamples or Spline if not masked by stronger 
        // Defaults.
        for (const SdfLayerRefPtr& layer : layers) {
            auto _ProcessField = 
                [&layerStack, &targetSpec, &resolveAssetPathFn, &path,
                    &layer](const TfToken& field) {
                if (layer->HasField(path, field)) {
                    VtValue val = _ReduceField(
                        layerStack, targetSpec, field, resolveAssetPathFn);
                    targetSpec->GetLayer()->SetField(path, field, val);
                    return true;
                }
                return false;
            };
            if (_ProcessField(SdfFieldKeys->TimeSamples) ||
                _ProcessField(SdfFieldKeys->Spline)) {
                break;
            } else if (layer->HasField(path, SdfFieldKeys->Default)) {
                // This layer has defaults that mask any underlying
                // TimeSamples or Spline in weaker layers.
                break;
            }
        }
    }
}

static SdfSpecType
_GetSiteSpecType(const SdfLayerRefPtrVector &layers, const SdfPath &path)
{
    for (const auto &l: layers) {
        if (l->HasSpec(path)) {
            return l->GetSpecType(path);
        } 
    }
    return SdfSpecTypeUnknown;
}

// Fwd decl
static void
_FlattenSpec(const PcpLayerStackRefPtr &layerStack,
             const SdfPrimSpecHandle &prim,
             const _ResolveAssetPathFn& resolveAssetPathFn);

static void
_FlattenSpec(const PcpLayerStackRefPtr &layerStack,
             const SdfVariantSpecHandle &var,
             const _ResolveAssetPathFn& resolveAssetPathFn)
{
    _FlattenSpec(layerStack, var->GetPrimSpec(), resolveAssetPathFn);
}

static void
_FlattenSpec(const PcpLayerStackRefPtr &layerStack,
             const SdfVariantSetSpecHandle &vset,
             const _ResolveAssetPathFn& resolveAssetPathFn)
{
    // Variants
    TfTokenVector nameOrder;
    PcpTokenSet nameSet;
    PcpComposeSiteChildNames(layerStack->GetLayers(), vset->GetPath(),
                             SdfChildrenKeys->VariantChildren,
                             &nameOrder, &nameSet);
    for (const TfToken &varName: nameOrder) {
        if (SdfVariantSpecHandle var = SdfVariantSpec::New(vset, varName)) {
            _FlattenFields(layerStack, var, resolveAssetPathFn);
            _FlattenSpec(layerStack, var, resolveAssetPathFn);
        }
    }
}

// Attribute connections / relationship targets
static void
_FlattenTargetPaths(const PcpLayerStackRefPtr &layerStack,
                    const SdfSpecHandle &spec,
                    const TfToken &field,
                    SdfPathEditorProxy targetProxy,
                    const _ResolveAssetPathFn& resolveAssetPathFn)
{
    VtValue val = _ReduceField(layerStack, spec, field, resolveAssetPathFn);
    if (val.IsHolding<SdfPathListOp>()) {
        SdfPathListOp listOp = val.UncheckedGet<SdfPathListOp>();
        // We want to recreate the set of listOp operations, but we
        // must go through the proxy editor in order for the target
        // path specs to be created as a side effect.  So, we replay the
        // operations against the proxy.
        if (listOp.IsExplicit()) {
            targetProxy.ClearEditsAndMakeExplicit();
            targetProxy.GetExplicitItems() = listOp.GetExplicitItems();
        } else {
            targetProxy.ClearEdits();
            targetProxy.GetPrependedItems() = listOp.GetPrependedItems();
            targetProxy.GetAppendedItems() = listOp.GetAppendedItems();
            targetProxy.GetDeletedItems() = listOp.GetDeletedItems();
            // We deliberately do not handle reordered or added items.
        }
    }
}

static void
_FlattenSpec(const PcpLayerStackRefPtr &layerStack,
             const SdfPrimSpecHandle &prim,
             const _ResolveAssetPathFn& resolveAssetPathFn)
{
    const SdfLayerRefPtrVector &layers = layerStack->GetLayers();

    // Child prims
    TfTokenVector nameOrder;
    PcpTokenSet nameSet;
    PcpComposeSiteChildNames(layers, prim->GetPath(),
                             SdfChildrenKeys->PrimChildren,
                             &nameOrder, &nameSet,
                             &SdfFieldKeys->PrimOrder);
    for (const TfToken &childName: nameOrder) {
        // Use SdfSpecifierDef as a placeholder specifier; it will be
        // fixed up when we _FlattenFields().
        if (SdfPrimSpecHandle child =
            SdfPrimSpec::New(prim, childName, SdfSpecifierDef)) {
            _FlattenFields(layerStack, child, resolveAssetPathFn);
            _FlattenSpec(layerStack, child, resolveAssetPathFn);
        }
    }

    if (prim->GetSpecType() == SdfSpecTypePseudoRoot) {
        return;
    }

    // Variant sets
    nameOrder.clear();
    nameSet.clear();
    PcpComposeSiteChildNames(layers, prim->GetPath(),
                             SdfChildrenKeys->VariantSetChildren,
                             &nameOrder, &nameSet);
    for (const TfToken &vsetName: nameOrder) {
        if (SdfVariantSetSpecHandle vset =
            SdfVariantSetSpec::New(prim, vsetName)) {
            _FlattenFields(layerStack, vset, resolveAssetPathFn);
            _FlattenSpec(layerStack, vset, resolveAssetPathFn);
        }
    }

    // Properties
    nameOrder.clear();
    nameSet.clear();
    PcpComposeSiteChildNames(layers, prim->GetPath(),
                             SdfChildrenKeys->PropertyChildren,
                             &nameOrder, &nameSet);
    for (const TfToken &childName: nameOrder) {
        SdfPath childPath = prim->GetPath().AppendProperty(childName);
        SdfSpecType specType = _GetSiteSpecType(layers, childPath);
        if (specType == SdfSpecTypeAttribute) {
            // Use Int as a (required) placeholder type; it will
            // be updated when we _FlattenFields().
            if (SdfAttributeSpecHandle attr =
                SdfAttributeSpec::New(prim, childName,
                                      SdfValueTypeNames->Int)) {
                _FlattenFields(layerStack, attr, resolveAssetPathFn);
                _FlattenTargetPaths(layerStack, attr,
                                    SdfFieldKeys->ConnectionPaths,
                                    attr->GetConnectionPathList(),
                                    resolveAssetPathFn);
            }
        } else if (specType == SdfSpecTypeRelationship) {
            if (SdfRelationshipSpecHandle rel =
                SdfRelationshipSpec::New(prim, childName)) {
                _FlattenFields(layerStack, rel, resolveAssetPathFn);
                _FlattenTargetPaths(layerStack, rel,
                                    SdfFieldKeys->TargetPaths,
                                    rel->GetTargetPathList(),
                                    resolveAssetPathFn);
            }
        } else {
            TF_RUNTIME_ERROR("Unknown spec type %s at <%s> in %s\n",
                             TfStringify(specType).c_str(),
                             childPath.GetText(),
                             TfStringify(layerStack).c_str());
            continue;
        }
    }
}

static std::string
_EvaluateAssetPathExpression(
    const std::string& expression,
    const VtDictionary& expressionVars)
{
    SdfVariableExpression::Result r = 
        SdfVariableExpression(expression)
        .EvaluateTyped<std::string>(expressionVars);
            
    if (!r.errors.empty()) {
        const std::string combinedError = TfStringJoin(
            r.errors.begin(), r.errors.end(), "; ");
        TF_WARN(
            "Error evaluating expression %s: %s",
            expression.c_str(), combinedError.c_str());
    }

    return r.value.IsHolding<std::string>() ? 
        r.value.UncheckedGet<std::string>() : std::string();
}

SdfLayerRefPtr
UsdFlattenLayerStack(
    const PcpLayerStackRefPtr &layerStack,
    const UsdFlattenResolveAssetPathAdvancedFn& resolveAssetPathFn,
    const std::string &tag)
{
    // Explicitly compute the expression variables for this layer stack instead
    // of using the values from PcpLayerStack::GetExpressionVariables. This
    // ensures we get the same variables as if we loaded layerStack as the
    // root layer of a UsdStage.
    //
    // For example, if layerStack was referenced from some other layer stack R,
    // and R had authored expression variables, those variables would show up
    // in the object returned by layerStack->GetExpressionVariables(). However,
    // if layerStack was used as a UsdStage's root layer stack, those variables
    // from R would not show up.
    const PcpExpressionVariables layerStackExprVars =
        PcpExpressionVariables::Compute(
            layerStack->GetIdentifier(), layerStack->GetIdentifier());

    // Wrap the resolve function we're given to pass along the computed
    // expression variables.
    auto resolveFnWrapper = [&resolveAssetPathFn, &layerStackExprVars](
        const SdfLayerHandle& sourceLayer, 
        const std::string& assetPath) -> std::string
    {
        return resolveAssetPathFn(
            {sourceLayer, assetPath, layerStackExprVars.GetVariables()});
    };

    ArResolverContextBinder arBinder(
        layerStack->GetIdentifier().pathResolverContext);
    SdfChangeBlock changeBlock;
    // XXX Currently, SdfLayer::CreateAnonymous() examines the tag
    // file extension to determine the file type.  Provide an
    // extension here if needed.
    const bool hasExtension = !TfGetExtension(tag).empty();
    SdfLayerRefPtr outputLayer = SdfLayer::CreateAnonymous(hasExtension ? tag : tag+".usda");
    _FlattenFields(layerStack, outputLayer->GetPseudoRoot(), resolveFnWrapper);
    _FlattenSpec(layerStack, outputLayer->GetPseudoRoot(), resolveFnWrapper);
    return outputLayer;
}

SdfLayerRefPtr
UsdFlattenLayerStack(const PcpLayerStackRefPtr &layerStack, 
                     const std::string& tag)
{
    return UsdFlattenLayerStack(layerStack,
            UsdFlattenLayerStackResolveAssetPathAdvanced, 
            tag);
}

SdfLayerRefPtr
UsdFlattenLayerStack(const PcpLayerStackRefPtr &layerStack,
                     const UsdFlattenResolveAssetPathFn& resolveAssetPathFn,
                     const std::string &tag)
{
    // Wrap the resolve function we're given so that we evaluate any asset
    // path expressions before passing them along to the callback.
    auto resolveFnWrapper = [&resolveAssetPathFn](
        const UsdFlattenResolveAssetPathContext& ctx) {

        if (SdfVariableExpression::IsExpression(ctx.assetPath)) {
            const std::string evaluatedAssetPath =
                _EvaluateAssetPathExpression(
                    ctx.assetPath, ctx.expressionVariables);
            return resolveAssetPathFn(ctx.sourceLayer, evaluatedAssetPath);
        }
        return resolveAssetPathFn(ctx.sourceLayer, ctx.assetPath);
    };

    return UsdFlattenLayerStack(layerStack, resolveFnWrapper, tag);
}

std::string
UsdFlattenLayerStackResolveAssetPathAdvanced(
    const UsdFlattenResolveAssetPathContext& ctx)
{
    const std::string* assetPath = &ctx.assetPath;

    // If the asset path is an expression, compute its value before anchoring
    // it below.
    std::string evaluatedAssetPath;
    if (SdfVariableExpression::IsExpression(ctx.assetPath)) {
        evaluatedAssetPath = _EvaluateAssetPathExpression(
            ctx.assetPath, ctx.expressionVariables);
        assetPath = &evaluatedAssetPath;
    }

    return UsdFlattenLayerStackResolveAssetPath(ctx.sourceLayer, *assetPath);
}

std::string 
UsdFlattenLayerStackResolveAssetPath(
    const SdfLayerHandle &sourceLayer, 
    const std::string &assetPath)
{
    // Treat empty asset paths specially, since they cause coding errors in
    // SdfComputeAssetPathRelativeToLayer.

    if (assetPath.empty()) {
        return assetPath;
    }

    // If anchoring has no effect on asset path, return it as-is. For additional
    // details, please see comments in _MakeResolvedAssetPathsImpl in stage.cpp.
    const std::string anchoredPath =
        SdfComputeAssetPathRelativeToLayer(sourceLayer, assetPath);

    const std::string unanchoredPath = ArGetResolver().CreateIdentifier(
        assetPath);

    if (unanchoredPath == anchoredPath) {
        return assetPath;
    }

    return anchoredPath;
}

PXR_NAMESPACE_CLOSE_SCOPE
