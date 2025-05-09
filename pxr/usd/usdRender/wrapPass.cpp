//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdRender/pass.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreatePassTypeAttr(UsdRenderPass &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreatePassTypeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}
        
static UsdAttribute
_CreateCommandAttr(UsdRenderPass &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateCommandAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->StringArray), writeSparsely);
}
        
static UsdAttribute
_CreateFileNameAttr(UsdRenderPass &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateFileNameAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Asset), writeSparsely);
}

static std::string
_Repr(const UsdRenderPass &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdRender.Pass(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapUsdRenderPass()
{
    typedef UsdRenderPass This;

    class_<This, bases<UsdTyped> >
        cls("Pass");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("Define", &This::Define, (arg("stage"), arg("path")))
        .staticmethod("Define")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetPassTypeAttr",
             &This::GetPassTypeAttr)
        .def("CreatePassTypeAttr",
             &_CreatePassTypeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetCommandAttr",
             &This::GetCommandAttr)
        .def("CreateCommandAttr",
             &_CreateCommandAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetFileNameAttr",
             &This::GetFileNameAttr)
        .def("CreateFileNameAttr",
             &_CreateFileNameAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        
        .def("GetRenderSourceRel",
             &This::GetRenderSourceRel)
        .def("CreateRenderSourceRel",
             &This::CreateRenderSourceRel)
        
        .def("GetInputPassesRel",
             &This::GetInputPassesRel)
        .def("CreateInputPassesRel",
             &This::CreateInputPassesRel)
        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

WRAP_CUSTOM {
    _class
    .def("GetRenderVisibilityCollectionAPI",
             &UsdRenderPass::GetRenderVisibilityCollectionAPI)
    ;
}

}
