//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdLux/boundableLightBase.h"
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


static std::string
_Repr(const UsdLuxBoundableLightBase &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "UsdLux.BoundableLightBase(%s)",
        primRepr.c_str());
}

        
static UsdAttribute
_CreateShaderIdAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateShaderIdAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}
        
static UsdAttribute
_CreateMaterialSyncModeAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateMaterialSyncModeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}
        
static UsdAttribute
_CreateIntensityAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateIntensityAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateExposureAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateExposureAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateDiffuseAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateDiffuseAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateSpecularAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateSpecularAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateNormalizeAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateNormalizeAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreateColorAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateColorAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Color3f), writeSparsely);
}
        
static UsdAttribute
_CreateEnableColorTemperatureAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEnableColorTemperatureAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Bool), writeSparsely);
}
        
static UsdAttribute
_CreateColorTemperatureAttr(UsdLuxBoundableLightBase &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateColorTemperatureAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
} // anonymous namespace

void wrapUsdLuxBoundableLightBase()
{
    typedef UsdLuxBoundableLightBase This;

    class_<This, bases<UsdGeomBoundable> >
        cls("BoundableLightBase");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)


        
        .def("GetShaderIdAttr",
             &This::GetShaderIdAttr)
        .def("CreateShaderIdAttr",
             &_CreateShaderIdAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetMaterialSyncModeAttr",
             &This::GetMaterialSyncModeAttr)
        .def("CreateMaterialSyncModeAttr",
             &_CreateMaterialSyncModeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetIntensityAttr",
             &This::GetIntensityAttr)
        .def("CreateIntensityAttr",
             &_CreateIntensityAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetExposureAttr",
             &This::GetExposureAttr)
        .def("CreateExposureAttr",
             &_CreateExposureAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetDiffuseAttr",
             &This::GetDiffuseAttr)
        .def("CreateDiffuseAttr",
             &_CreateDiffuseAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetSpecularAttr",
             &This::GetSpecularAttr)
        .def("CreateSpecularAttr",
             &_CreateSpecularAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetNormalizeAttr",
             &This::GetNormalizeAttr)
        .def("CreateNormalizeAttr",
             &_CreateNormalizeAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetColorAttr",
             &This::GetColorAttr)
        .def("CreateColorAttr",
             &_CreateColorAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetEnableColorTemperatureAttr",
             &This::GetEnableColorTemperatureAttr)
        .def("CreateEnableColorTemperatureAttr",
             &_CreateEnableColorTemperatureAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetColorTemperatureAttr",
             &This::GetColorTemperatureAttr)
        .def("CreateColorTemperatureAttr",
             &_CreateColorTemperatureAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("GetFiltersRel",
             &This::GetFiltersRel)
        .def("CreateFiltersRel",
             &This::CreateFiltersRel)

        .def("LightAPI", &This::LightAPI)
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
}

}
