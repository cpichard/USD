//
// Copyright 2024 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Copyright Jim Bosch 2010-2012.
// Copyright Stefan Seefeld 2016.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef PXR_EXTERNAL_BOOST_PYTHON_NUMPY_INVOKE_MATCHING_HPP
#define PXR_EXTERNAL_BOOST_PYTHON_NUMPY_INVOKE_MATCHING_HPP

#include "pxr/pxr.h"
#include "pxr/external/boost/python/common.hpp"

/**
 *  @brief Template invocation based on dtype matching.
 */

#include "pxr/external/boost/python/numpy/dtype.hpp"
#include "pxr/external/boost/python/numpy/ndarray.hpp"
#include "pxr/external/boost/python/type_list.hpp"
#include <functional>
#include <type_traits>

namespace PXR_BOOST_NAMESPACE { namespace python { namespace numpy {
namespace detail 
{

template <typename T, T... I, typename Function>
void for_each(std::integer_sequence<T, I...>, Function const& f)
{
    (f((std::integral_constant<T, I>*)nullptr), ...);
};

template <typename ...T, typename Function>
void for_each(python::type_list<T...>, Function const& f)
{
    (f((T*)nullptr), ...);
};

struct PXR_BOOST_NUMPY_DECL dtype_template_match_found {};
struct PXR_BOOST_NUMPY_DECL nd_template_match_found {};

template <typename Function>
struct dtype_template_invoker 
{
    
  template <typename T>
  void operator()(T *) const 
  {
    if (dtype::get_builtin<T>() == m_dtype) 
    {
      m_func.Function::template apply<T>();
      throw dtype_template_match_found();
    }
  }

  dtype_template_invoker(dtype const & dtype_, Function func) 
    : m_dtype(dtype_), m_func(func) {}

private:
  dtype const & m_dtype;
  Function m_func;
};

template <typename Function>
struct dtype_template_invoker< std::reference_wrapper<Function> > 
{
    
  template <typename T>
  void operator()(T *) const 
  {
    if (dtype::get_builtin<T>() == m_dtype) 
    {
      m_func.Function::template apply<T>();
      throw dtype_template_match_found();
    }
  }

  dtype_template_invoker(dtype const & dtype_, Function & func)
    : m_dtype(dtype_), m_func(func) {}

private:
  dtype const & m_dtype;
  Function & m_func;
};

template <typename Function>
struct nd_template_invoker 
{    
  template <int N>
  void operator()(std::integral_constant<int,N> *) const 
  {
    if (m_nd == N) 
    {
      m_func.Function::template apply<N>();
      throw nd_template_match_found();
    }
  }

  nd_template_invoker(int nd, Function func) : m_nd(nd), m_func(func) {}

private:
  int m_nd;
  Function m_func;
};

template <typename Function>
struct nd_template_invoker< std::reference_wrapper<Function> > 
{    
  template <int N>
  void operator()(std::integral_constant<int,N> *) const 
  {
    if (m_nd == N) 
    {
      m_func.Function::template apply<N>();
      throw nd_template_match_found();
    }
  }

  nd_template_invoker(int nd, Function & func) : m_nd(nd), m_func(func) {}

private:
  int m_nd;
  Function & m_func;
};

} // namespace PXR_BOOST_NAMESPACE::python::numpy::detail

template <typename Sequence, typename Function>
void invoke_matching_nd(int nd, Function f) 
{
  detail::nd_template_invoker<Function> invoker(nd, f);
  try { for_each(Sequence(), invoker); }
  catch (detail::nd_template_match_found &) { return;}
  PyErr_SetString(PyExc_TypeError, "number of dimensions not found in template list.");
  python::throw_error_already_set();
}

template <typename Sequence, typename Function>
void invoke_matching_dtype(dtype const & dtype_, Function f) 
{
  detail::dtype_template_invoker<Function> invoker(dtype_, f);
  try { for_each(Sequence(), invoker); }
  catch (detail::dtype_template_match_found &) { return;}
  PyErr_SetString(PyExc_TypeError, "dtype not found in template list.");
  python::throw_error_already_set();
}

namespace detail 
{

template <typename T, typename Function>
struct array_template_invoker_wrapper_2 
{
  template <int N>
  void apply() const { m_func.Function::template apply<T,N>();}
  array_template_invoker_wrapper_2(Function & func) : m_func(func) {}

private:
  Function & m_func;
};

template <typename DimSequence, typename Function>
struct array_template_invoker_wrapper_1 
{
  template <typename T>
  void apply() const { invoke_matching_nd<DimSequence>(m_nd, array_template_invoker_wrapper_2<T,Function>(m_func));}
  array_template_invoker_wrapper_1(int nd, Function & func) : m_nd(nd), m_func(func) {}

private:
  int m_nd;
  Function & m_func;
};

template <typename DimSequence, typename Function>
struct array_template_invoker_wrapper_1< DimSequence, std::reference_wrapper<Function> >
  : public array_template_invoker_wrapper_1< DimSequence, Function >
{
  array_template_invoker_wrapper_1(int nd, Function & func)
    : array_template_invoker_wrapper_1< DimSequence, Function >(nd, func) {}
};

} // namespace PXR_BOOST_NAMESPACE::python::numpy::detail

template <typename TypeSequence, typename DimSequence, typename Function>
void invoke_matching_array(ndarray const & array_, Function f) 
{
  detail::array_template_invoker_wrapper_1<DimSequence,Function> wrapper(array_.get_nd(), f);
  invoke_matching_dtype<TypeSequence>(array_.get_dtype(), wrapper);
}

}}} // namespace PXR_BOOST_NAMESPACE::python::numpy

#endif
