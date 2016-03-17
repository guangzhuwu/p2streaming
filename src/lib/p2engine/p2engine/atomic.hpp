//
// atomic.hpp
// ~~~~~~~~~~~
//
// Copyright (c) 2008-2010 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NETWORK_ATOMIC_HPP
#define BOOST_NETWORK_ATOMIC_HPP

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
#  endif // defined(P2ENGINE_MSVC)
# endif // !defined(P2ENGINE_DISABLE_STD_ATOMIC)
#endif // !defined(P2ENGINE_HAS_STD_ATOMIC)

#ifdef P2ENGINE_HAS_STD_ATOMIC
#include <atomic>
#define CXX11_ATOMIC std
#else
#include <boost/atomic.hpp>
#define CXX11_ATOMIC boost
#endif
#include <boost/asio/detail/config.hpp>
namespace p2engine{
	using CXX11_ATOMIC::atomic;
	using CXX11_ATOMIC::atomic_int;
	using CXX11_ATOMIC::atomic_flag;

	using CXX11_ATOMIC::atomic_thread_fence;
	using CXX11_ATOMIC::memory_order_relaxed;
	using CXX11_ATOMIC::memory_order_acquire;
	using CXX11_ATOMIC::memory_order_release;
	using CXX11_ATOMIC::memory_order_acq_rel;
	using CXX11_ATOMIC::memory_order_consume;
	using CXX11_ATOMIC::memory_order_seq_cst;
}

#endif//p2engine_atomic_hpp__
