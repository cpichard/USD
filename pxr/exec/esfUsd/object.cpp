//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/exec/esfUsd/object.h"

#include "pxr/exec/esfUsd/attribute.h"
#include "pxr/exec/esfUsd/prim.h"

#include "pxr/base/tf/token.h"
#include "pxr/exec/esf/attribute.h"
#include "pxr/exec/esf/object.h"
#include "pxr/exec/esf/prim.h"
#include "pxr/exec/esf/property.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/object.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/property.h"

#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

// EsfObject should not reserve more space than necessary.
static_assert(sizeof(EsfUsd_Object) == sizeof(EsfObject));

template <class InterfaceType, class UsdObjectType>
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::~EsfUsd_ObjectImpl()
    = default;

template <class InterfaceType, class UsdObjectType>
bool
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::_IsValid() const
{
    return _GetWrapped().IsValid();
}

template <class InterfaceType, class UsdObjectType>
TfToken
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::_GetName() const
{
    return _GetWrapped().GetName();
}

template <class InterfaceType, class UsdObjectType>
EsfPrim
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::_GetPrim() const
{
    return {std::in_place_type<EsfUsd_Prim>, _GetWrapped().GetPrim()};
}

template <class InterfaceType, class UsdObjectType>
bool
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::IsPrim() const
{
    return _GetWrapped().template Is<UsdPrim>();
}

template <class InterfaceType, class UsdObjectType>
bool
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::IsAttribute() const
{
    return _GetWrapped().template Is<UsdAttribute>();
}

template <class InterfaceType, class UsdObjectType>
EsfObject
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::AsObject() const
{
    return {
        std::in_place_type<EsfUsd_Object>,
        _GetWrapped().template As<UsdObject>()
    };
}

template <class InterfaceType, class UsdObjectType>
EsfPrim
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::AsPrim() const
{
    return {
        std::in_place_type<EsfUsd_Prim>,
        _GetWrapped().template As<UsdPrim>()
    };
}

template <class InterfaceType, class UsdObjectType>
EsfAttribute
EsfUsd_ObjectImpl<InterfaceType, UsdObjectType>::AsAttribute() const
{
    return {
        std::in_place_type<EsfUsd_Attribute>,
        _GetWrapped().template As<UsdAttribute>()
    };
}

// Explicit template instantiations.
template class EsfUsd_ObjectImpl<EsfAttributeInterface, UsdAttribute>;
template class EsfUsd_ObjectImpl<EsfObjectInterface, UsdObject>;
template class EsfUsd_ObjectImpl<EsfPrimInterface, UsdPrim>;
template class EsfUsd_ObjectImpl<EsfPropertyInterface, UsdProperty>;

PXR_NAMESPACE_CLOSE_SCOPE