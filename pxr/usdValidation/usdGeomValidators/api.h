//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_VALIDATION_USD_GEOM_VALIDATORS_API_H
#define PXR_USD_VALIDATION_USD_GEOM_VALIDATORS_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDGEOMVALIDATORS_API
#   define USDGEOMVALIDATORS_API_TEMPLATE_CLASS(...)
#   define USDGEOMVALIDATORS_API_TEMPLATE_STRUCT(...)
#   define USDGEOMVALIDATORS_API_LOCAL
#else
#   if defined(USDGEOMVALIDATORS_EXPORTS)
#       define USDGEOMVALIDATORS_API ARCH_EXPORT
#       define USDGEOMVALIDATORS_API_TEMPLATE_CLASS(...)                      \
           ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDGEOMVALIDATORS_API_TEMPLATE_STRUCT(...)                     \
           ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDGEOMVALIDATORS_API ARCH_IMPORT
#       define USDGEOMVALIDATORS_API_TEMPLATE_CLASS(...)                      \
           ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDGEOMVALIDATORS_API_TEMPLATE_STRUCT(...)                     \
           ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#       define USDGEOMVALIDATORS_API_LOCAL ARCH_HIDDEN
#endif

#endif
