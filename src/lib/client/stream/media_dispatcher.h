//
// media_dispatcher.h
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_media_dispatcher_h__
#define peer_media_dispatcher_h__

#include "client/stream/scheduling_typedef.h"

namespace p2client{

	class stream_scheduling;

	class media_dispatcher
		:public basic_stream_scheduling
	{
	public:
		enum{MAX_PRE_PUSH_TIME=10};
		enum{OVERSTOCKED_SIZE=2*1024*1024};

	protected:
		media_dispatcher(stream_scheduling& scheduling);

	public:
		virtual void stop();
		virtual void start();
		virtual void reset();

		virtual int on_timer(timestamp_t now);
	
	public:
		void set_flush(bool flush=false){flush_=flush;}
		bool is_player_started()const
		{
			return is_player_started_;
		}

		const boost::optional<seqno_t>& get_smallest_seqno_i_care()const
		{
			return smallest_seqno_i_care_;
		}

		const boost::optional<seqno_t>& get_smallest_txt_seqno_i_care()const
		{
			return smallest_txt_seqno_i_care_;
		}

		timestamp_t get_current_playing_timestamp(timestamp_t now)const
		{
			BOOST_ASSERT(is_live());
			BOOST_ASSERT(is_player_started());
			return timestamp_t(sento_player_timestamp_offset_+now);
		}

		double get_buffer_health()const;

		size_t get_buffer_size()const
		{
			return media_pkt_to_player_cache_.size();
		}

		int  get_buffer_duration(double bufferHealth)const;

		seqno_t get_min_seqno_in_buffer()const
		{
			BOOST_ASSERT(get_buffer_size()>0);
			return media_pkt_to_player_cache_.begin()->first;
		}
		seqno_t get_max_seqno_in_buffer()const
		{
			BOOST_ASSERT(get_buffer_size()>0);
			return media_pkt_to_player_cache_.rbegin()->first;
		}
		const media_packet& get_min_packet_in_buffer()const
		{
			BOOST_ASSERT(get_buffer_size()>0);
			return media_pkt_to_player_cache_.begin()->second;
		}
		const media_packet& get_max_packet_in_buffer()const
		{
			BOOST_ASSERT(get_buffer_size()>0);
			return media_pkt_to_player_cache_.rbegin()->second;
		}
		int get_delay_play_time()const
		{
			return delay_play_time_;
		}
		void set_smallest_seqno_i_care(seqno_t seqno);

	public:
		void do_process_recvd_media_packet(const media_packet& pkt);

	protected:
		//vod live 使用不同的实现
		virtual void dispatch_media_packet(media_packet& data, 
			client_service_logic_base&)=0;
		virtual bool be_about_to_play(const media_packet& pkt, double bufferHealth, 
			int overstockedToPlayerSize, timestamp_t now)=0;
		virtual bool can_player_start(double bufferHealth, timestamp_t now)=0;
		virtual void check_scheduling_health()=0;

		bool fec_decode(timestamp_t now, seqno_t theLostSeqno);
	protected:
		int on_dispatch_timer(timestamp_t now);
		void dispatch(const media_packet& orgPkt, client_service& svc, 
			client_service_logic_base& svcLogic);
		void dispatch_system_level_data(safe_buffer data, client_service& svc);
		bool fec_decode(timestamp_t now)
		{
			return fec_decode(now, *smallest_seqno_i_care_);
		}

	protected:
		std::string cas_string_;

		typedef std::map<seqno_t, media_packet, wrappable_less<seqno_t> > media_packet_map;
		media_packet_map media_pkt_to_player_cache_;
		media_packet_map fec_media_pkt_cache_;
		media_packet_map txt_pkt_cache_;

		timestamp_t sento_player_timestamp_offset_;//发送到播放器时的timeoffset, 但会随着播放速度控制而发生变化
		timestamp_t last_send_to_player_timestamp_;
		timestamp_t player_start_time_;
		int delay_play_time_;

		boost::optional<seqno_t> smallest_seqno_i_care_;
		boost::optional<seqno_t> smallest_txt_seqno_i_care_;

		fec_decoder packet_fec_decoder_;

		bool is_player_started_:1;
		bool flush_;
	};

}

#endif//peer_media_dispatcher_h__