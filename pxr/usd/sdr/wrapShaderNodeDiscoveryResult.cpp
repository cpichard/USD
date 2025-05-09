//
// Copyright 2025 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdr/shaderNodeDiscoveryResult.h"

#include "pxr/external/boost/python.hpp"

PXR_NAMESPACE_USING_DIRECTIVE

namespace bp = pxr_boost::python;

namespace {


static
std::string
_Repr(const SdrShaderNodeDiscoveryResult& x)
{
    std::vector<std::string> args = {
        TfPyRepr(x.identifier),
        TfPyRepr(x.version),
        TfPyRepr(x.name),
        TfPyRepr(x.family),
        TfPyRepr(x.discoveryType),
        TfPyRepr(x.sourceType),
        TfPyRepr(x.uri),
        TfPyRepr(x.resolvedUri)
    };

    #define ADD_KW_ARG(kwArgs, propName) \
        kwArgs.push_back(TfStringPrintf(#propName "=%s", \
                                        TfPyRepr(x.propName).c_str()));

    if (!x.sourceCode.empty()) { ADD_KW_ARG(args, sourceCode); };
    if (!x.metadata.empty()) { ADD_KW_ARG(args, metadata); };
    if (!x.blindData.empty()) { ADD_KW_ARG(args, blindData); };
    if (!x.subIdentifier.IsEmpty()) { ADD_KW_ARG(args, subIdentifier); };

    #undef ADD_KW_ARG

    return TF_PY_REPR_PREFIX +
        TfStringPrintf("NodeDiscoveryResult(%s)", 
                       TfStringJoin(args, ", ").c_str());
}


// XXX: WBN if Tf provided this sort of converter for stl maps.
template <typename MAP>
struct MapConverter
{
    typedef MAP Map;
    typedef typename Map::key_type Key;
    typedef typename Map::mapped_type Value;

    MapConverter()
    {
        pxr_boost::python::type_info info = pxr_boost::python::type_id<Map>();
        pxr_boost::python::converter::registry::push_back(&convertible, &construct, 
                                                      info);

        const pxr_boost::python::converter::registration* reg =
                pxr_boost::python::converter::registry::query(info);
        if (reg == NULL || reg->m_to_python == NULL) {
            pxr_boost::python::to_python_converter<Map, MapConverter<Map>>();
        }
    }
    static PyObject* convert(const Map& map)
    {
        pxr_boost::python::dict result;
        for (const auto& entry : map) {
            result[entry.first] = entry.second;
        }
        return pxr_boost::python::incref(result.ptr());
    }

    static void* convertible(PyObject* obj_ptr)
    {
        if (!PyDict_Check(obj_ptr)) {
            return nullptr;
        }

        pxr_boost::python::dict map = pxr_boost::python::extract<pxr_boost::python::dict>(
                obj_ptr);
        pxr_boost::python::list keys = map.keys();
        pxr_boost::python::list values = map.values();
        for (int i = 0; i < len(keys); ++i) {

            pxr_boost::python::object keyObj = keys[i];
            if (!pxr_boost::python::extract<Key>(keyObj).check()) {
                return nullptr;
            }

            pxr_boost::python::object valueObj = values[i];
            if (!pxr_boost::python::extract<Value>(valueObj).check()) {
                return nullptr;
            }
        }

        return obj_ptr;
    }
    static void construct(PyObject* obj_ptr,
        pxr_boost::python::converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = (
                (pxr_boost::python::converter::rvalue_from_python_storage<Map>*)data
            )->storage.bytes;
        new (storage)Map();
        data->convertible = storage;

        Map& result = *((Map*)storage);

        pxr_boost::python::dict map = 
                pxr_boost::python::extract<pxr_boost::python::dict>(obj_ptr);
        pxr_boost::python::list keys = map.keys();
        pxr_boost::python::list values = map.values();
        for (int i = 0; i < len(keys); ++i) {

            pxr_boost::python::object keyObj = keys[i];
            pxr_boost::python::object valueObj = values[i];
            result.emplace(pxr_boost::python::extract<Key>(keyObj),
                           pxr_boost::python::extract<Value>(valueObj));
        }
    }
};

}

void wrapShaderNodeDiscoveryResult()
{
    MapConverter<SdrTokenMap>();

    using namespace pxr_boost::python;

    typedef SdrShaderNodeDiscoveryResult This;
    class_<This>("NodeDiscoveryResult", no_init)
        .def(init<SdrIdentifier, SdrVersion, std::string, TfToken, TfToken, 
                  TfToken, std::string, std::string, std::string,
                  SdrTokenMap, std::string, TfToken>(
                  (arg("identifier"),
                   arg("version"),
                   arg("name"),
                   arg("family"),
                   arg("discoveryType"),
                   arg("sourceType"),
                   arg("uri"),
                   arg("resolvedUri"),
                   arg("sourceCode")=std::string(), 
                   arg("metadata")=SdrTokenMap(),
                   arg("blindData")=std::string(),
                   arg("subIdentifier")=TfToken())))
        .add_property("identifier", make_getter(&This::identifier,
                          return_value_policy<return_by_value>()))
        .add_property("version", &This::version)
        .add_property("name", &This::name)
        .add_property("family", make_getter(&This::family, 
                          return_value_policy<return_by_value>()))
        .add_property("discoveryType", make_getter(&This::discoveryType, 
                          return_value_policy<return_by_value>()))
        .add_property("sourceType", make_getter(&This::sourceType, 
                          return_value_policy<return_by_value>()))
        .add_property("uri", &This::uri)
        .add_property("resolvedUri", &This::resolvedUri)
        .add_property("sourceCode", &This::sourceCode)
        .add_property("metadata", make_getter(&This::metadata,  
                          return_value_policy<TfPyMapToDictionary>()))
        .add_property("blindData", &This::blindData)
        .add_property("subIdentifier", make_getter(&This::subIdentifier, 
                          return_value_policy<return_by_value>()))
        .def("__repr__", _Repr)
        ;

    TfPyContainerConversions::from_python_sequence<
        std::vector<This>,
        TfPyContainerConversions::variable_capacity_policy >();
    pxr_boost::python::to_python_converter<
        std::vector<This>, 
        TfPySequenceToPython<std::vector<This> > >();
}
