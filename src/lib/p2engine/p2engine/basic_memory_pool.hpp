//
// basic_memory_pool.hpp
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

#ifndef P2ENGINE_BASIC_MEMORY_POOL_HPP
#define P2ENGINE_BASIC_MEMORY_POOL_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"

#ifdef WINDOWS_OS
# define P2ENGINE_MEMPOOL_USE_NEDMALLOC
#else
# define P2ENGINE_MEMPOOL_USE_STDMALLOC
#endif

#if !defined(P2ENGINE_MEMPOOL_USE_STDMALLOC)\
	&&!defined(P2ENGINE_MEMPOOL_USE_NEDMALLOC)
# define P2ENGINE_MEMPOOL_USE_STDMALLOC
#endif

#ifdef P2ENGINE_MEMPOOL_USE_NEDMALLOC
#	include "p2engine/nedmalloc/nedmalloc.h"
#endif

#include "p2engine/pop_warning_option.hpp"

namespace p2engine {

	//////////////////////////////////////////////////////////////////////////
	//default_user_allocator_malloc_free
	struct std_malloc_free
	{
		typedef size_t size_type;
		typedef std::ptrdiff_t difference_type;

		static char * malloc(const size_type bytes)
		{
			return reinterpret_cast<char *>(::std::malloc(bytes));
		}

		static void free(void * const block)
		{
			::std::free(block);
		}

		static char * realloc(void * const block, const size_type bytes)
		{
			return (char*)::std::realloc(block, bytes);
		}
	};

	//////////////////////////////////////////////////////////////////////////
	//nedmalloc_malloc_free
#if defined P2ENGINE_MEMPOOL_USE_STDMALLOC
	typedef std_malloc_free default_allocator;
	using std::allocator;

#elif defined P2ENGINE_MEMPOOL_USE_NEDMALLOC
	struct nedmalloc_malloc_free
	{
		typedef size_t size_type;
		typedef std::ptrdiff_t difference_type;

		static char * malloc(const size_type bytes)
		{
			return reinterpret_cast<char *>(nedalloc::nedmalloc(bytes));
		}

		static void free(void * const block)
		{
			nedalloc::nedfree(block);
		}

		static char * realloc(void * const block, size_type bytes)
		{
			return reinterpret_cast<char *>(nedalloc::nedrealloc(block, bytes));
		}
	};
	typedef nedmalloc_malloc_free default_allocator;
#else

#endif
	template <typename T>
	class allocator {
	public:
		typedef allocator<T> other;

		typedef T value_type;

		typedef value_type *pointer;
		typedef const value_type *const_pointer;
		typedef void *void_pointer;
		typedef const void *const_void_pointer;

		typedef value_type& reference;
		typedef const value_type& const_reference;

		typedef size_t size_type;
		typedef ptrdiff_t difference_type;

		template <class T1> struct rebind {
			typedef allocator<T1> other;
		};

		allocator() { }
		allocator(const allocator&) { }
		template <class T1> allocator(const allocator<T1>&) { }
		~allocator() { }

		pointer address(reference x) const { return &x; }
		const_pointer address(const_reference x) const { return &x; }

		pointer allocate(size_type n, const void* = 0) {
			BOOST_ASSERT(((n * sizeof(T)) / sizeof(T) == n) && "n is too big to allocate");
			return (pointer)default_allocator::malloc(sizeof(T)*n);
		}
		void deallocate(pointer p, size_type n) {
			default_allocator::free(p);
		}

		size_type max_size() const { return size_t(-1) / sizeof(T); }

		void construct(pointer p, const T& val) { ::new(p)T(val); }
		void construct(pointer p) { ::new(p)T(); }
		void destroy(pointer p) { p->~T(); }

		// There's no state, so these allocators are always equal
		bool operator==(const allocator&) const { return true; }
	};

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//basic_memory_pool
	template < typename UserAllocator = default_allocator>
	class basic_memory_pool
	{
		typedef basic_memory_pool<UserAllocator> this_type;
	public:
		static void* malloc(size_t bytes)
		{
			if (bytes == 0) bytes = 1;
			return UserAllocator::malloc(bytes);
		}
		static void free(void * const pointer)
		{
			if (!pointer)
				return;
			UserAllocator::free(pointer);
		}
	};

	typedef basic_memory_pool<> memory_pool;

} // namespace p2engine

#endif // p2engine_BASIC_MEMORY_POOL_HPP

