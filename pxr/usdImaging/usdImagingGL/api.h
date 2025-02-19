//
// Copyright 2017 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_USD_IMAGING_USD_IMAGING_GL_API_H
#define PXR_USD_IMAGING_USD_IMAGING_GL_API_H

#include "pxr/base/arch/export.h"

#if defined(PXR_STATIC)
#   define USDIMAGINGGL_API
#   define USDIMAGINGGL_API_TEMPLATE_CLASS(...)
#   define USDIMAGINGGL_API_TEMPLATE_STRUCT(...)
#   define USDIMAGINGGL_LOCAL
#else
#   if defined(USDIMAGINGGL_EXPORTS)
#       define USDIMAGINGGL_API ARCH_EXPORT
#       define USDIMAGINGGL_API_TEMPLATE_CLASS(...) ARCH_EXPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDIMAGINGGL_API_TEMPLATE_STRUCT(...) ARCH_EXPORT_TEMPLATE(struct, __VA_ARGS__)
#   else
#       define USDIMAGINGGL_API ARCH_IMPORT
#       define USDIMAGINGGL_API_TEMPLATE_CLASS(...) ARCH_IMPORT_TEMPLATE(class, __VA_ARGS__)
#       define USDIMAGINGGL_API_TEMPLATE_STRUCT(...) ARCH_IMPORT_TEMPLATE(struct, __VA_ARGS__)
#   endif
#   define USDIMAGINGGL_LOCAL ARCH_HIDDEN
#endif

#endif // PXR_USD_IMAGING_USD_IMAGING_GL_API_H
