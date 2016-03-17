//
// smoother.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef smoother_h__
#define smoother_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <deque>
#include <map>

#include "common/config.h"

namespace p2common{

	class smoother
	{
		typedef int time_offset_t;
		//提前发放time_offset_t为正
		typedef boost::function<void(time_offset_t)> send_handler;
		typedef precise_timer timer;
	public:
		//按照窗口catchTime来计算push速度；
		//在push进第一个包后再过startTime开始发放第一个数据包;
		//发放出去的各个包，相比第一个发放出去的包的原始间隔最大延迟maxDellay。
		smoother(
			const time_duration& longSpeedMeterTime, //平均码率计算用的窗口大小
			const time_duration& smoothWindowTime, //平滑缓冲的大小
			const time_duration& preCatchTime, //预先缓存时间
			const time_duration& preDistribTime, //开始发送后，每个包的最大提前发放时间（从进入开始工作状态算起）
			const time_duration& maxDellay, //开始发送后，每个包的最大发放延迟（从进入开始工作状态算起）
			int lowSpeedThresh, //最低发送速度byte p s
			io_service& ios
			);
		virtual ~smoother();

		void reset();
		void stop();
		void push(int64_t connectionID, const send_handler& h, size_t len);
		size_t size()const{return size_;}

	protected:
		void on_timer();

	protected:
		struct element{
			send_handler handler;
			int64_t  t;
			size_t   l;
			element(const send_handler& hd, size_t len, boost::int64_t tm)
				:handler(hd), t(tm), l(len)
			{}
		};
		typedef std::map<int64_t, std::deque<element> > task_map;
		task_map send_handler_list_;
		size_t size_;
		boost::optional<task_map::iterator> last_iterator_;
		boost::optional<int64_t> start_time_offset_;
		boost::optional<int64_t> first_push_time_;
		boost::optional<int64_t> last_push_time_;
		int start_time_delay_;
		int max_time_pre_;
		int max_time_delay_;
		int low_speed_thresh_;
		rough_speed_meter long_in_speed_meter_;
		rough_speed_meter smooth_in_speed_meter_;
		rough_speed_meter out_speed_meter_;
		boost::shared_ptr<timer> timer_;
	};

}

#endif//smoother_h__

