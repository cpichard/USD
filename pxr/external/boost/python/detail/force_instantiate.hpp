//
// Copyright 2024 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Copyright David Abrahams 2002.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#ifndef PXR_EXTERNAL_BOOST_PYTHON_DETAIL_FORCE_INSTANTIATE_HPP
# define PXR_EXTERNAL_BOOST_PYTHON_DETAIL_FORCE_INSTANTIATE_HPP

#include "pxr/pxr.h"
#include "pxr/external/boost/python/common.hpp"

namespace PXR_BOOST_NAMESPACE { namespace python { namespace detail { 

// Allows us to force the argument to be instantiated without
// incurring unused variable warnings

template <class T>
inline void force_instantiate(T const&) {}

}}} // namespace PXR_BOOST_NAMESPACE::python::detail

#endif // PXR_EXTERNAL_BOOST_PYTHON_DETAIL_FORCE_INSTANTIATE_HPP
