// Copyright (c) 2017-2022 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#ifndef PXR_PEGTL_CONTRIB_INTERNAL_ENDIAN_HPP
#define PXR_PEGTL_CONTRIB_INTERNAL_ENDIAN_HPP

#include <cstdint>
#include <cstring>

#include "../../config.hpp"

#if defined( _WIN32 ) && !defined( __MINGW32__ ) && !defined( __CYGWIN__ )
#include "endian_win.hpp"
#else
#include "endian_gcc.hpp"
#endif

namespace PXR_PEGTL_NAMESPACE::internal
{
   enum class endian
   {
#if defined( _WIN32 )
      little = 0,
      big = 1,
      native = little
#elif defined( __BYTE_ORDER__ )
      little = __ORDER_LITTLE_ENDIAN__,
      big = __ORDER_BIG_ENDIAN__,
      native = __BYTE_ORDER__
#else
#error Unknown endianness.
#endif
   };

   template< typename N >
   [[nodiscard]] N h_to_be( const N n ) noexcept
   {
      return N( to_and_from_be< sizeof( N ) >::convert( n ) );
   }

   template< typename N >
   [[nodiscard]] N be_to_h( const N n ) noexcept
   {
      return h_to_be( n );
   }

   template< typename N >
   [[nodiscard]] N be_to_h( const void* p ) noexcept
   {
      N n;
      std::memcpy( &n, p, sizeof( n ) );
      return internal::be_to_h( n );
   }

   template< typename N >
   [[nodiscard]] N h_to_le( const N n ) noexcept
   {
      return N( to_and_from_le< sizeof( N ) >::convert( n ) );
   }

   template< typename N >
   [[nodiscard]] N le_to_h( const N n ) noexcept
   {
      return h_to_le( n );
   }

   template< typename N >
   [[nodiscard]] N le_to_h( const void* p ) noexcept
   {
      N n;
      std::memcpy( &n, p, sizeof( n ) );
      return internal::le_to_h( n );
   }

}  // namespace PXR_PEGTL_NAMESPACE::internal

#endif
