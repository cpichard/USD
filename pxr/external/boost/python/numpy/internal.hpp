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

#ifndef PXR_EXTERNAL_BOOST_PYTHON_NUMPY_INTERNAL_HPP
#define PXR_EXTERNAL_BOOST_PYTHON_NUMPY_INTERNAL_HPP

#include "pxr/pxr.h"
#include "pxr/external/boost/python/common.hpp"

/**
 *  @file boost/python/numpy/internal.hpp
 *  @brief Internal header file to include the Numpy C-API headers.
 *
 *  This should only be included by source files in the boost.numpy library itself.
 */

#include "pxr/external/boost/python.hpp"
#include "pxr/external/boost/python/numpy/config.hpp"
#ifdef PXR_BOOST_PYTHON_NUMPY_INTERNAL
#define NO_IMPORT_ARRAY
#define NO_IMPORT_UFUNC
#else
#ifndef PXR_BOOST_PYTHON_NUMPY_INTERNAL_MAIN
ERROR_internal_hpp_is_for_internal_use_only
#endif
#endif
#define PY_ARRAY_UNIQUE_SYMBOL BOOST_NUMPY_ARRAY_API
#define PY_UFUNC_UNIQUE_SYMBOL BOOST_UFUNC_ARRAY_API
#include <numpy/arrayobject.h>
#include <numpy/ufuncobject.h>
#include "pxr/external/boost/python/numpy.hpp"

#define NUMPY_OBJECT_MANAGER_TRAITS_IMPL(pytype,manager)                \
    PyTypeObject const * object_manager_traits<manager>::get_pytype() { return &pytype; }

#endif
