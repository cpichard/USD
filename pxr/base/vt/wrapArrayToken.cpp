//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/base/vt/typeHeaders.h"
#include "pxr/base/vt/wrapArray.h"
#include "pxr/base/vt/wrapArrayEdit.h"

PXR_NAMESPACE_USING_DIRECTIVE

void wrapArrayToken() {
    VtWrapArray<VtArray<TfToken> >();
    VtWrapArrayEdit<VtArrayEdit<TfToken> >();
}
