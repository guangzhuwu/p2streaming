//
// std_or_boost.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009, GuangZhu Wu  <guangzhuwu@gmail.com>
//
//This program is free software; you can redistribute it and/or modify it 
//under the terms of the GNU General Public License or any later version.
//
//This program is distributed in the hope that it will be useful, but 
//WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
//or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//for more details.
//
//You should have received a copy of the GNU General Public License along 
//with this program; if not, contact <guangzhuwu@gmail.com>.
//
#ifndef P2ENGINE_CONFIG_STD_OR_BOOST_HPP
#define P2ENGINE_CONFIG_STD_OR_BOOST_HPP

// Standard library support for arrays.
#if !defined(P2ENGINE_HAS_STD_ARRAY)
# if !defined(P2ENGINE_DISABLE_STD_ARRAY)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_ARRAY 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_ARRAY 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER) 
#   if (_MSC_VER >= 1600)
#    define P2ENGINE_HAS_STD_ARRAY 1
#   endif // (_MSC_VER >= 1600)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_ARRAY)
#endif // !defined(P2ENGINE_HAS_STD_ARRAY)

// Standard library support for shared_ptr and weak_ptr.
#if !defined(P2ENGINE_HAS_STD_SHARED_PTR)
# if !defined(P2ENGINE_DISABLE_STD_SHARED_PTR)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_SHARED_PTR 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_SHARED_PTR 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1600)
#    define P2ENGINE_HAS_STD_SHARED_PTR 1
#   endif // (_MSC_VER >= 1600)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_SHARED_PTR)
#endif // !defined(P2ENGINE_HAS_STD_SHARED_PTR)

// Standard library support for atomic operations.
#if !defined(P2ENGINE_HAS_STD_ATOMIC)
# if !defined(P2ENGINE_DISABLE_STD_ATOMIC)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_ATOMIC 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_ATOMIC 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_ATOMIC 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_ATOMIC)
#endif // !defined(P2ENGINE_HAS_STD_ATOMIC)

// Standard library support for chrono. Some standard libraries (such as the
// libstdc++ shipped with gcc 4.6) provide monotonic_clock as per early C++0x
// drafts, rather than the eventually standardised name of steady_clock.
#if !defined(P2ENGINE_HAS_STD_CHRONO)
# if !defined(P2ENGINE_DISABLE_STD_CHRONO)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_CHRONO 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 6)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_CHRONO 1
#     if ((__GNUC__ == 4) && (__GNUC_MINOR__ == 6))
#      define P2ENGINE_HAS_STD_CHRONO_MONOTONIC_CLOCK 1
#     endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ == 6))
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 6)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_CHRONO 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_CHRONO)
#endif // !defined(P2ENGINE_HAS_STD_CHRONO)

// Boost support for chrono.
#if !defined(P2ENGINE_HAS_BOOST_CHRONO)
# if !defined(P2ENGINE_DISABLE_BOOST_CHRONO)
#  if (BOOST_VERSION >= 104700)
#   define P2ENGINE_HAS_BOOST_CHRONO 1
#  endif // (BOOST_VERSION >= 104700)
# endif // !defined(P2ENGINE_DISABLE_BOOST_CHRONO)
#endif // !defined(P2ENGINE_HAS_BOOST_CHRONO)

// Boost support for the DateTime library.
#if !defined(P2ENGINE_HAS_BOOST_DATE_TIME)
# if !defined(P2ENGINE_DISABLE_BOOST_DATE_TIME)
#  define P2ENGINE_HAS_BOOST_DATE_TIME 1
# endif // !defined(P2ENGINE_DISABLE_BOOST_DATE_TIME)
#endif // !defined(P2ENGINE_HAS_BOOST_DATE_TIME)

// Standard library support for addressof.
#if !defined(P2ENGINE_HAS_STD_ADDRESSOF)
# if !defined(P2ENGINE_DISABLE_STD_ADDRESSOF)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_ADDRESSOF 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_ADDRESSOF 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_ADDRESSOF 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_ADDRESSOF)
#endif // !defined(P2ENGINE_HAS_STD_ADDRESSOF)

// Standard library support for the function class.
#if !defined(P2ENGINE_HAS_STD_FUNCTION)
# if !defined(P2ENGINE_DISABLE_STD_FUNCTION)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_FUNCTION 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_FUNCTION 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_FUNCTION 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_FUNCTION)
#endif // !defined(P2ENGINE_HAS_STD_FUNCTION)

// Standard library support for type traits.
#if !defined(P2ENGINE_HAS_STD_TYPE_TRAITS)
# if !defined(P2ENGINE_DISABLE_STD_TYPE_TRAITS)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_TYPE_TRAITS 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_TYPE_TRAITS 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_TYPE_TRAITS 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_TYPE_TRAITS)
#endif // !defined(P2ENGINE_HAS_STD_TYPE_TRAITS)

// Standard library support for the cstdint header.
#if !defined(P2ENGINE_HAS_CSTDINT)
# if !defined(P2ENGINE_DISABLE_CSTDINT)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_CSTDINT 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_CSTDINT 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_CSTDINT 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_CSTDINT)
#endif // !defined(P2ENGINE_HAS_CSTDINT)

// Standard library support for the thread class.
#if !defined(P2ENGINE_HAS_STD_THREAD)
# if !defined(P2ENGINE_DISABLE_STD_THREAD)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_THREAD 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_THREAD 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_THREAD 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_THREAD)
#endif // !defined(P2ENGINE_HAS_STD_THREAD)

// Standard library support for the mutex and condition variable classes.
#if !defined(P2ENGINE_HAS_STD_MUTEX_AND_CONDVAR)
# if !defined(P2ENGINE_DISABLE_STD_MUTEX_AND_CONDVAR)
#  if defined(P2ENGINE_HAS_CLANG_LIBCXX)
#   define P2ENGINE_HAS_STD_MUTEX_AND_CONDVAR 1
#  endif // defined(P2ENGINE_HAS_CLANG_LIBCXX)
#  if defined(__GNUC__)
#   if ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) || (__GNUC__ > 4)
#    if defined(__GXX_EXPERIMENTAL_CXX0X__)
#     define P2ENGINE_HAS_STD_MUTEX_AND_CONDVAR 1
#    endif // defined(__GXX_EXPERIMENTAL_CXX0X__)
#   endif // ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 7)) || (__GNUC__ > 4)
#  endif // defined(__GNUC__)
#  if defined(_MSC_VER)
#   if (_MSC_VER >= 1700)
#    define P2ENGINE_HAS_STD_MUTEX_AND_CONDVAR 1
#   endif // (_MSC_VER >= 1700)
#  endif // defined(_MSC_VER)
# endif // !defined(P2ENGINE_DISABLE_STD_MUTEX_AND_CONDVAR)
#endif // !defined(P2ENGINE_HAS_STD_MUTEX_AND_CONDVAR)


#endif//P2ENGINE_CONFIG_STD_OR_BOOST_HPP
