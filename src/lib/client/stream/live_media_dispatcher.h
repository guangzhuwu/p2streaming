//
// live_media_dispatcher.h
// ~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef client_live_media_dispatcher_h__
#define client_live_media_dispatcher_h__

#include "client/stream/media_dispatcher.h"

namespace p2client{

	class live_media_dispatcher
		:public media_dispatcher
	{
		typedef live_media_dispatcher this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create(stream_scheduling& scheduling)
		{
			return shared_ptr(new this_type(scheduling), 
				shared_access_destroy<this_type>());
		}

	protected:
		live_media_dispatcher(stream_scheduling& scheduling);
		virtual ~live_media_dispatcher();

	protected:
		//vod live 使用不同的实现
		virtual void dispatch_media_packet(media_packet& data, 
			client_service_logic_base&);
		virtual bool be_about_to_play(const media_packet& pkt, double bufferHealth, 
			int overstockedToPlayerSize, timestamp_t now);
		virtual bool can_player_start(double bufferHealth, timestamp_t now);
		virtual void check_scheduling_health();

	private:
		void change_delay_play_time(timestamp_t now, bool speedup);

	private:
		timestamp_t last_change_delay_play_time_time_;
	};

}

#endif//client_live_media_dispatcher_h__

