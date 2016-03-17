//
// spinlock.hpp
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
#ifdef _MSC_VER
#	pragma once
#endif

#ifndef _BD_SPINLOCK_H__
#define _BD_SPINLOCK_H__

#include <boost/thread/thread.hpp>
#include <boost/asio/detail/scoped_lock.hpp>

#include "p2engine/atomic.hpp"
#include "p2engine/time.hpp"

namespace p2engine{

	class spinlock {
	public:
		typedef boost::asio::detail::scoped_lock<spinlock> scoped_lock;

		spinlock() : locked_(false) {}

		inline bool try_lock()
		{
			return locked_.exchange(true, memory_order_acquire) == false;
		}

		void lock()
		{
			for (size_t i = 0; !try_lock(); ++i)
			{
				if (i > 4)
				{
					if (i >= 20 && (i & 4) == 0)
						system_time::sleep_millisec(0);
					else
						boost::this_thread::yield();
				}
			}
		}
		void unlock()
		{
			locked_.store(false, memory_order_release);
		}

	private:
		atomic<bool> locked_;
	};

}



#endif//_BD_SPINLOCK_H__