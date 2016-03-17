//
// scheduling_typedef.h
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef scheduling_typedef_h__
#define scheduling_typedef_h__

#include "client/typedef.h"
#include "client/stream/absent_packet_list.h"

namespace p2client{

	class stream_scheduling;
	class stream_monitor;
	class stream_seed;

	//struct session_status{
	//	typedef boost::shared_ptr<asfio::async_dskcache> disk_cache_sptr;
	//	stream_monitor					m_stream_monitor;
	//	absent_packet_list				m_absent_packet_list;
	//	packet_buffer					m_memory_packet_cache;//收到的数据片段的内存缓存
	//	disk_cache_sptr					m_disk_packet_cache;//硬盘存储，当前未在直播中使用

	//	//一些多个模块会用到的变量
	//	timestamp_t						m_scheduling_start_time;
	//	boost::optional<timestamp_t>	m_recvd_first_packet_time;
	//	boost::optional<seqno_t>		m_smallest_seqno_i_care;
	//	boost::optional<seqno_t>		m_bigest_seqno_i_know;
	//	boost::optional<seqno_t>		m_bigest_seqno_when_join;
	//	int								m_backfetch_msec;
	//};

	inline std::pair<seqno_t, seqno_t> get_seqno_range(int64_t offset, int64_t totalFilmSize)
	{
		seqno_t minSeqno=static_cast<seqno_t>(offset/PIECE_SIZE);
		seqno_t maxSeqno=static_cast<seqno_t>((totalFilmSize-1)/PIECE_SIZE);
		return std::make_pair(minSeqno, maxSeqno);
	}

	struct scheduling_status{
		timestamp_t m_now;
		peer_connection_sptr m_server_connection;
		boost::optional<timestamp_t> m_recvd_first_packet_time;
		timestamp_t m_scheduling_start_time;
		boost::optional<seqno_t> m_smallest_seqno_i_care;
		boost::optional<seqno_t> m_bigest_seqno_i_know;
		seqno_t m_bigest_seqno_in_buffer;
		seqno_t m_smallest_seqno_in_buffer;
		int m_max_memory_cach_size;
		double m_playing_quality;
		double m_global_remote_to_local_lostrate;
		double m_server_to_local_lostrate;
		double m_duplicate_rate;
		double m_buffer_health;
		int	m_buffer_size;
		int m_buffer_duration;
		int m_incoming_packet_rate;
		int m_neighbor_cnt;
		int m_total_online_peer_cnt;
		int m_delay_gurantee;
		int m_backfetch_msec;
		int m_src_packet_rate;
		bool m_b_super_seed;
		bool m_b_seed;
		bool m_b_inscription_state;
		bool m_b_play_start;
		bool m_b_live;
	};

	typedef boost::unordered_map<peer_connection*, std::vector<seqno_t> > scheduling_task_map;

	class basic_stream_scheduling
		: public basic_client_object
		, public basic_engine_object
	{
	protected:
		basic_stream_scheduling(stream_scheduling& scheduling);
		virtual ~basic_stream_scheduling();
	
	public:
		virtual void stop()=0;
		virtual void start()=0;
		virtual void reset()=0;

		//每隔10ms本函数被stream_scheduling调用一次，用来处理一些定时任务。
		//但是否是精确的10ms间隔视操作系统时间精度而不同，在一些arm-linux平台上是20ms。
		//返回cpuload
		virtual int on_timer(timestamp_t now)=0;

	protected:
		stream_scheduling* scheduling_;
	};

	class scheduling_strategy
		:public scheduling_status
	{
	public:
		virtual const scheduling_task_map& get_task_map()=0;
	};

}

#endif //scheduling_typedef_h__
