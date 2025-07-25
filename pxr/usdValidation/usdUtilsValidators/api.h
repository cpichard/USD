//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_VALIDATION_USD_UTILS_VALIDATORS_API_H
#define PXR_USD_VALIDATION_USD_UTILS_VALIDATORS_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDUTILSVALIDATORS_API
#   define USDUTILSVALIDATORS_API_TEMPLATE_CLASS(...)
#   define USDUTILSVALIDATORS_API_TEMPLATE_STRUCT(...)
#   define USDUTILSVALIDATORS_API_LOCAL
#else
#   if defined(USDUTILSVALIDATORS_EXPORTS)
#       define USDUTILSVALIDATORS_API ARCH_EXPORT
#       define USDUTILSVALIDATORS_API_TEMPLATE_CLASS(...)                     \
           ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDUTILSVALIDATORS_API_TEMPLATE_STRUCT(...)                    \
           ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDUTILSVALIDATORS_API ARCH_IMPORT
#       define USDUTILSVALIDATORS_API_TEMPLATE_CLASS(...)                     \
           ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDUTILSVALIDATORS_API_TEMPLATE_STRUCT(...)                    \
           ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#       define USDUTILSVALIDATORS_API_LOCAL ARCH_HIDDEN
#endif

#endif
