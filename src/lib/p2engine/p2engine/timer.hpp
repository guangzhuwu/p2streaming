//
// timer.hpp
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

#ifndef P2ENGINE_TIMER_HPP
#define P2ENGINE_TIMER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "p2engine/push_warning_option.hpp"
#include "p2engine/config.hpp"

#include <iostream>

#include <boost/function.hpp>
#include <boost/asio/basic_deadline_timer.hpp>

#include "p2engine/time.hpp"
#include "p2engine/basic_engine_object.hpp"
#include "p2engine/operation_mark.hpp"

namespace p2engine{

	template<typename Time, typename TimeTraits>
	class basic_timer
		:public basic_engine_object
		, public operation_mark
	{
		typedef basic_timer<Time, TimeTraits> this_type;
		SHARED_ACCESS_DECLARE;
		typedef boost::asio::basic_deadline_timer<Time, TimeTraits> deadline_timer;

	public:
		typedef boost::function<void(void)> timer_handle_type;

		typedef typename deadline_timer::traits_type time_traits_type;
		typedef typename deadline_timer::time_type time_type;
		typedef typename deadline_timer::duration_type duration_type;

		enum status{ NOT_WAITING = 0, ASYNC_WAITING = (1 << 0), ASYNC_KEEP_WAITING = (1 << 1) };
	public:
		static shared_ptr create(io_service& engine_svc)
		{
			return shared_ptr(new this_type(engine_svc), 
				shared_access_destroy<this_type>());
		}

	protected:
		basic_timer(io_service& engine_svc)
			: basic_engine_object(engine_svc)
			, deadline_timer_(get_io_service())
			, repeat_times_(0)
			, repeated_times_(0)
			, status_(NOT_WAITING)
		{
			set_obj_desc("basic_timer");
			next_op_stamp();
		}
		virtual ~basic_timer(){ cancel(); }

	public:
		void register_time_handler(timer_handle_type h)
		{
			ON_TIMER_ = h;
		}
		void unregister_time_handler()
		{
			ON_TIMER_.clear();
		}

		bool is_idle()const
		{
			return  status_ == NOT_WAITING;
		}

		static time_type now()
		{
			return time_traits_type::now();
		}

		time_type expires_at() const
		{
			return deadline_timer_.expires_at();
		}

		duration_type expires_from_now() const
		{
			return deadline_timer_.expires_from_now();
		}

		size_t repeated_times() const
		{
			return repeated_times_;
		}

		size_t repeat_times_left() const
		{
			return (repeat_times_ == (std::numeric_limits<size_t>::max)()) ?
				(std::numeric_limits<size_t>::max)()
				: repeat_times_ - repeated_times_;
		}

		void cancel()
		{
			error_code ec;
			deadline_timer_.cancel(ec);
			repeat_times_ = 0;
			status_ = NOT_WAITING;
			repeat_times_ = 0;
			repeated_times_ = 0;

			set_cancel();
			next_op_stamp();

			BOOST_ASSERT(is_idle());
		}

		void async_wait_at(const time_type& expiry_time)
		{
			async_wait(expiry_time - now());
		}

		void async_wait(const duration_type& expiry_duration)
		{
			set_cancel();
			next_op_stamp();

			error_code ec;
			status_ |= ASYNC_WAITING;
			deadline_timer_.expires_from_now(expiry_duration, ec);
			deadline_timer_.async_wait(
				make_alloc_handler(boost::bind(&this_type::handle_timeout, 
				SHARED_OBJ_FROM_THIS, _1, false, op_stamp()))
				);
		}

		//The repeat_times included the first expiration
		void async_keep_waiting(const duration_type& expiry_duration, 
			const duration_type& periodical_duration, 
			size_t repeat_times = (std::numeric_limits<size_t>::max)())
		{
			BOOST_ASSERT(periodical_duration != duration_type());
			//if (status_)
			//	cancel();
			error_code ec;
			deadline_timer_.expires_from_now(expiry_duration, ec);
			expiry_duration_ = expiry_duration;
			periodical_duration_ = periodical_duration;
			repeat_times_ = repeat_times;
			repeated_times_ = 0;
			status_ |= ASYNC_KEEP_WAITING;
			repeat_start_time_ = now();

			deadline_timer_.async_wait(
				make_alloc_handler(boost::bind(&this_type::handle_timeout, 
				SHARED_OBJ_FROM_THIS, _1, true, op_stamp()))
				);

			//return ON_TIMER_;
		}

	protected:
		void handle_timeout(const error_code& ec, bool keep_waiting, op_stamp_t stamp)
		{
			if (!ec)
			{
				if (!is_canceled_op(stamp))//to fix asio's inappropriate design(in my opinion)
				{
					if (!keep_waiting)
					{
						status_ &= ~ASYNC_WAITING;
						if ((status_&ASYNC_KEEP_WAITING) == ASYNC_KEEP_WAITING)
							async_wait_next(true);
					}
					else //if (keep_waiting)
					{
						++repeated_times_;
						async_wait_next(false);//must before ON_TIMER_.otherwise, status_ may be wrong
					}
					ON_TIMER_();
				}
				else
				{
					//std::cout<<get_obj_desc()<<", !!canceled_op, now="<<time_traits_type::now_tick_count()<<"\n";
				}
			}
			else
			{
				//std::cout<<get_obj_desc()<<", on_timer error: "<<ec.message()<<", now="<<time_traits_type::now_tick_count()<<"\n";
				//status_ = NOT_WAITING;
			}
		}

		void async_wait_next(bool recalcExpireTime)
		{
			if (status_ != NOT_WAITING
				&& (repeated_times_ == (std::numeric_limits<size_t>::max)()
				|| repeated_times_ < repeat_times_
				)
				)
			{
				duration_type periodical_duration = periodical_duration_;
				if (recalcExpireTime)
				{
					duration_type t = now() - (repeat_start_time_ + expiry_duration_);
					t -= periodical_duration_*(t.total_milliseconds() / periodical_duration_.total_milliseconds());
					periodical_duration = t;
				}
				error_code ec;
				deadline_timer_.expires_from_now(periodical_duration, ec);
				deadline_timer_.async_wait(
					make_alloc_handler(boost::bind(&this_type::handle_timeout, 
					SHARED_OBJ_FROM_THIS, _1, true, op_stamp()))
					);
			}
			else
			{
				status_ &= ~ASYNC_KEEP_WAITING;
			}
		}

	private:
		deadline_timer deadline_timer_;
		duration_type expiry_duration_, periodical_duration_;
		size_t repeat_times_, repeated_times_;
		time_type repeat_start_time_;
		int status_;
		timer_handle_type ON_TIMER_;
	};

	typedef basic_timer<precise_tick_time::time_type, precise_tick_time> precise_timer;
	typedef basic_timer<rough_tick_time::time_type, rough_tick_time> rough_timer;

	PTR_TYPE_DECLARE(precise_timer);
	PTR_TYPE_DECLARE(rough_timer)
}

#include "p2engine/pop_warning_option.hpp"

#endif//P2ENGINE_TIMER_HPP
