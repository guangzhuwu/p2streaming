//
// raw_buffer.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009-2010  GuangZhu Wu
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
//
// THANKS  Meng Zhang <albert.meng.zhang@gmail.com>
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
//
// THANKS  Meng Zhang <albert.meng.zhang@gmail.com>
//


#ifndef P2ENGINE_RAW_BUFFER_HPP
#define P2ENGINE_RAW_BUFFER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/basic_object.hpp"
#include "p2engine/intrusive_ptr_base.hpp"

namespace p2engine {

	class raw_buffer 
		:public basic_intrusive_ptr<raw_buffer>
	{
		typedef raw_buffer this_type;
		SHARED_ACCESS_DECLARE;

		typedef object_allocator::memory_pool_type memory_pool_type;

	private:
		raw_buffer(size_t length):length_(length)
		{
			DEBUG_SCOPE(released_in_the_proper_way_=(int64_t)(void*)this;);
		}
		~raw_buffer()
		{
			DEBUG_SCOPE(
				BOOST_ASSERT(
				released_in_the_proper_way_==8256272458LL
				&&
				"can only be released by function of \"void release(const raw_buffer* p)\""
				)
				);
		}

	public:
		static intrusive_ptr create(size_t len)
		{
			DEBUG_SCOPE(counter()++;);

			char* p=(char*)memory_pool_type::malloc(aligned_size()+len);
			if (!p)
				return intrusive_ptr();

			char* allignedPtr = (char*)ROUND_UP(((ptrdiff_t)p), ALIGN_OF(this_type));
			this_type* _ptr = ::new(allignedPtr)this_type(len);
			_ptr->orig_mallocd_ptr_offset_ = (uint8_t)((char*)_ptr - p);
			BOOST_ASSERT(((char*)_ptr) - p < 256);
			BOOST_ASSERT((uintptr_t)(_ptr->buffer()) % 4 == 0);//ALLIGNed

			return intrusive_ptr(_ptr);
		}
		static void release(const raw_buffer* p)
		{
			DEBUG_SCOPE(BOOST_ASSERT(p->released_in_the_proper_way_ == (int64_t)(void*)p));
			DEBUG_SCOPE(p->released_in_the_proper_way_ = 8256272458LL;);
			DEBUG_SCOPE(counter()--;);

			char* _ptr = ((char*)(p)) - p->orig_mallocd_ptr_offset_;
			p->~raw_buffer();
			memory_pool_type::free(_ptr);
		}
		size_t length() const {return length_;}
		size_t size() const {return length_;}
		char* buffer() 
		{
			DEBUG_SCOPE(BOOST_ASSERT(released_in_the_proper_way_==(int64_t)(void*)this));
			return orig_ptr()+aligned_size();
		}
		const char* buffer() const 
		{
			DEBUG_SCOPE(BOOST_ASSERT(released_in_the_proper_way_==(int64_t)(void*)this));
			return orig_ptr()+aligned_size();
		}

	private:
		char* orig_ptr()const
		{
			return ((char*)this)+orig_mallocd_ptr_offset_;
		}
		static size_t aligned_size()
		{
			enum {align_size=ROUND_UP(sizeof(this_type)+ALIGN_OF(this_type), sizeof(size_t))};
			return align_size;
		}

		DEBUG_SCOPE(
			static atomic<int32_t>& counter()
		{
			static atomic<int32_t> counter_(0);
			BOOST_ASSERT(counter_>=0);
			return counter_;
		}
		mutable int64_t released_in_the_proper_way_;
		);
		size_t length_;
		uint8_t orig_mallocd_ptr_offset_;
	};

	//to make  raw_buffer be deleted in the right way
	template<>
	struct  shared_access_destroy<const raw_buffer>
	{
		void operator()(const raw_buffer* p)const
		{
			raw_buffer::release(p);
		}
	};
	template<>
	struct  shared_access_destroy<raw_buffer>
	{
		void operator()(raw_buffer* p)const
		{
			raw_buffer::release(p);
		}
	};
} // namespace p2engine

#endif // P2ENGINE_RAW_BUFFER_HPP

