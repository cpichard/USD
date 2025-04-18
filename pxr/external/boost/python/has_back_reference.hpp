//
// Copyright 2024 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Copyright David Abrahams 2002.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#ifndef PXR_EXTERNAL_BOOST_PYTHON_HAS_BACK_REFERENCE_HPP
# define PXR_EXTERNAL_BOOST_PYTHON_HAS_BACK_REFERENCE_HPP

#include "pxr/pxr.h"
#include "pxr/external/boost/python/common.hpp"

# include "pxr/external/boost/python/detail/prefix.hpp"
# include "pxr/external/boost/python/detail/mpl2/bool.hpp"

namespace PXR_BOOST_NAMESPACE { namespace python { 

// traits class which users can specialize to indicate that a class
// contains a back-reference to its owning PyObject*
template <class T>
struct has_back_reference
  : detail::mpl2::false_
{
};


}} // namespace PXR_BOOST_NAMESPACE::python

#endif // PXR_EXTERNAL_BOOST_PYTHON_HAS_BACK_REFERENCE_HPP
