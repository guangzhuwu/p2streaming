//
// stream_scheduling.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_stream_scheduling_h__
#define peer_stream_scheduling_h__

#include "client/typedef.h"
#include "client/scheduling_base.h"
#include "client/stream/absent_packet_list.h"
#include "client/stream/stream_topology.h"
#include "client/stream/stream_monitor.h"
#include "client/stream/buffer_manager.h"
#include "client/stream/stream_seed.h"
#include "client/stream/media_dispatcher.h"
#include "client/stream/heuristic_scheduling_strategy.h"

namespace p2client{

	class stream_monitor;
	class stream_topology;
	class media_dispatcher;
	class stream_pusher;
	class heuristic_scheduling_strategy;
	class stream_scheduling
		:public scheduling_base
	{
		typedef stream_scheduling this_type;
		SHARED_ACCESS_DECLARE;

		friend class stream_topology;
		typedef stream_topology_sptr topology_sptr;
		typedef stream_topology_wptr topology_wptr;
		typedef rough_timer timer;
		typedef boost::shared_ptr<timer> timer_sptr;

	protected:
		struct  unsubscript_elm
		{
			unsubscript_elm(const peer_connection_sptr& c, int subStreamID, timestamp_t now)
				:conn(c), substream_id(subStreamID), t(now){}
			peer_connection_wptr conn;
			int substream_id;
			timestamp_t t;
		};

	public:
		static const double		URGENT_TIME;
		static const double		LOST_RATE_THRESH;
		static const double		DEFINIT_URGENT_DEGREE;
		static const int		PULL_TIMER_INTERVAL;//msec
		static const double		AGGRESSIVE_THREASH;//丢包超过这个就认为需要和别人抢带宽

	protected:
		stream_scheduling(topology_sptr, bool justBeProvider = false);
		virtual ~stream_scheduling();

	public:
		static shared_ptr create(topology_sptr tpl, bool justBeProvider = false)
		{
			return shared_ptr(new this_type(tpl), shared_access_destroy<this_type>());
		}

	public:
		topology_sptr get_topology()const
		{
			return topology_.lock();
		}

		client_service_sptr get_client_service()const
		{
			stream_topology* topology=topology_.lock().get();
			if (topology)
				return topology->get_client_service();
			return client_service_sptr();
		}

		int get_average_src_packet_rate()const
		{
			if (average_packet_rate_)
				return (int)*average_packet_rate_;
			return (int)src_packet_rate_;
		}

		int get_src_packet_rate()const
		{
			return (int)src_packet_rate_;
		}

		packet_buffer& get_memory_packet_cache() 
		{
			return buffer_manager_->get_memory_packet_cache();
		}
		const packet_buffer& get_memory_packet_cache() const
		{
			return buffer_manager_->get_memory_packet_cache();
		}

		absent_packet_list& get_absent_packet_list() 
		{
			return buffer_manager_->get_absent_packet_list();
		}
		const absent_packet_list& get_absent_packet_list() const
		{
			return buffer_manager_->get_absent_packet_list();
		}

		const stream_monitor& get_stream_monitor()const
		{
			BOOST_ASSERT(stream_monitor_);
			return *stream_monitor_;
		}

		stream_monitor& get_stream_monitor()
		{
			BOOST_ASSERT(stream_monitor_);
			return *stream_monitor_;
		}

		const buffer_manager& get_buffer_manager()const
		{
			BOOST_ASSERT(buffer_manager_);
			return *buffer_manager_;
		}

		buffer_manager& get_buffer_manager()
		{
			BOOST_ASSERT(buffer_manager_);
			return *buffer_manager_;
		}

		const boost::optional<timestamp_t>& get_recvd_first_packet_time()const
		{
			return recvd_first_packet_time_;
		}

		const boost::optional<seqno_t>& get_smallest_seqno_i_care()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_smallest_seqno_i_care();
		}

		double get_buffer_health()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_buffer_health();
		}

		int get_buffer_duration(double bufferHealth)const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_buffer_duration(bufferHealth);
		}

		const boost::optional<seqno_t>& get_bigest_sqno_i_know() const
		{
			return buffer_manager_->get_bigest_sqno_i_know();
		}

		size_t get_buffer_size()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_buffer_size();
		}

		seqno_t get_min_seqno_in_buffer()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_min_seqno_in_buffer();
		}

		seqno_t get_max_seqno_in_buffer()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_max_seqno_in_buffer();
		}

		timestamp_t get_current_playing_timestamp(timestamp_t now)const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->get_current_playing_timestamp(now);
		}

		bool is_player_started()const
		{
			BOOST_ASSERT(media_dispatcher_);
			return media_dispatcher_->is_player_started();
		}

		bool is_in_incoming_substream(const peer_connection* conn, int subStreamID)const
		{
			return (conn==in_substream_[subStreamID].lock().get());
		}

		peer_connection* get_incoming_substream_conn(int subStreamID)const
		{
			return (in_substream_[subStreamID].lock().get());
		}

		double average_hop()const {return hop_;}
		double average_hop(int substreadID)const {return substream_hop_[substreadID];}

		absent_packet_info* get_packet_info(const seqno_t seqno)
		{
			return get_absent_packet_list().get_packet_info(seqno);
		}

		int backfetch_msec()const
		{
			return get_client_param_sptr()->back_fetch_duration;
		}
		int delay_guarantee()const
		{
			return delay_guarantee_;
		}

		template<typename MsgType>
		void set_buffermap(MsgType& msg, bool using_bitset = false, 
			bool using_longbitset = false, bool pop_erased = true)
		{
			buffer_manager_->get_buffermap(msg, using_bitset, using_longbitset, pop_erased);
		}

		bool get_media(media_packet&pkt, seqno_t seqno, timestamp_t now=timestamp_now())
		{
			return get_memory_packet_cache().get(pkt, seqno, now);
		}

		void prepare_erase_neighbor(peer_connection* conn)
		{
			prepare_erase_conn_=conn;
		}

		timestamp_t get_timestamp_adjusted(const media_packet&pkt)const
		{
			if (is_vod())
				return timestamp_t((pkt.get_seqno()*1000)/(int)src_packet_rate_);
			return pkt.get_time_stamp();
		}

		const peer_connection_sptr get_server_connection() const
		{
			return stream_seed_->get_connection();
		}

		peer_connection* get_prepare_erase_connection()
		{
			return prepare_erase_conn_;
		}

		bool guess_timestamp(timestamp_t now, seqno_t seqno, timestamp_t& timeStamp, 
			int* thePacketRate=NULL)
		{
			(void)(now);
			return stream_monitor_->guess_timestamp(seqno, timeStamp, thePacketRate);
		}

		bool get_piece_elapse(timestamp_t now, seqno_t seqno, int& t, 
			timestamp_t* pieceTimestamp=NULL)const
		{
			return stream_monitor_->piece_elapse(now, seqno, t, pieceTimestamp);
		}

		double urgent_degree(timestamp_t now, seqno_t seqno, int deadlineMsec)const
		{
			return stream_monitor_->urgent_degree(now, seqno, deadlineMsec);
		}

		bool local_is_seed()const
		{
			return stream_seed_->local_is_seed();
		}

		bool local_is_super_seed()const
		{	
			if (local_is_seed())
				return be_super_seed_;
			return false;
		}

		int get_packet_rate_adjusted(const media_packet&pkt)const
		{
			if (is_vod())
				return (int)src_packet_rate_;
			return pkt.get_packet_rate();
		}

		const bool is_pull_start() const
		{
			return scheduling_strategy_->is_pull_start();
		}

	public: //实现父类函数
		virtual void start();
		virtual void stop(bool flush=false);
		virtual void set_play_offset(int64_t offset);
		virtual void send_handshake_to(peer_connection*);
		virtual void neighbor_erased(const peer_id_t& id);
		virtual void register_message_handler(peer_connection* conn);
		virtual void process_recvd_buffermap(const buffermap_info& bufmap, peer_connection* p);

	public:
		void require_update_server_info();
		void restart();
		void check_health(const scheduling_status&localStatus);
		void set_local_status(scheduling_status&localStatus, timestamp_t now);
		void calc_download_speed_coefficient(const scheduling_status&localStatus);
		bool is_in_outgoing_substream(const peer_connection* conn, int substreamID)const;
		bool is_in_outgoing_substream(const peer_connection* conn)const;
		void subscribe(peer_connection* conn, int substreamID, seqno_t seqno);
		void unsubscribe(peer_connection* conn, int substreamID);
		bool has_subscription(peer_connection*);
		void read_media_packet_from_nosocket(const safe_buffer&, const error_code& ec, seqno_t seqno);
		int urgent_time()const
		{ 
			return (int)(
				URGENT_TIME*std::max(get_client_param_sptr()->delay_guarantee, 
				get_client_param_sptr()->back_fetch_duration)
				);
		}
	protected:
		//网络消息处理
		void on_recved_buffermap_request(peer_connection* conn, safe_buffer buf);
		void on_recvd_buffermap_exchange(peer_connection* conn, safe_buffer buf);
		void on_recvd_media_confirm(peer_connection* conn, safe_buffer buf);
		void on_recvd_lite_media_request(peer_connection* conn, safe_buffer buf);
		void on_recvd_chunkmap_exchange(peer_connection* conn, safe_buffer buf);
		void on_recvd_media_packet(peer_connection* conn, safe_buffer buf);
		void on_recvd_be_seed(peer_connection* conn, safe_buffer buf);
		void on_recvd_be_super_seed(peer_connection* conn, safe_buffer buf);
		void on_recvd_piece_notify(peer_connection* conn, safe_buffer buf);
		void on_recvd_recommend_seed(peer_connection* conn, safe_buffer buf);
		void on_recvd_no_piece(peer_connection* conn, safe_buffer buf);
		void on_recvd_media_request(peer_connection* conn, safe_buffer buf);
		void on_recvd_media_subscription(peer_connection* conn, safe_buffer buf);
		void on_recvd_media_unsubscription(peer_connection* conn, safe_buffer buf);
		void on_recvd_pulled_media_packet(peer_connection* conn, safe_buffer buf)
		{
			media_packet pkt(buf);
			pkt.set_is_push(0);
			on_recvd_media_packet(conn, buf);
		}
		void on_recvd_pushed_media_packet(peer_connection* conn, safe_buffer buf)
		{
			media_packet pkt(buf);
			pkt.set_is_push(1);
			on_recvd_media_packet(conn, buf);
		}

		//定时事件处理
		void on_timer();
		void on_pull_timer(timestamp_t now);
		void on_info_report_timer(timestamp_t now);
		void on_quality_report_timer(timestamp_t now);
		void on_unsubscrip_timer(timestamp_t now);
		int on_media_confirm_timer(timestamp_t now);
		int on_exchange_buffermap_timer(timestamp_t now);

		//功能函数
		void __init_seqno_i_care(seqno_t seqnoKnown);
		void __stop(bool flush=false);
		int  average_subscribe_rtt(timestamp_t now);
		void set_play_seqno(seqno_t seqno);
		void process_media_packet_from_nonsocket(media_packet& pkt, timestamp_t now);
		void process_recvd_media_packet(media_packet&pkt, peer_connection* p, timestamp_t now);
		bool local_is_major_super_seed();
		void unsubscription();   //退订所有子流
		void reset_scheduling(uint8_t* serverSession=NULL);
		void request_perhaps_fail(seqno_t seqno, peer_connection* inchargePeer);
		int exchange_buffermap_live(const neighbor_map& conn_map, timestamp_t now);
		int exchange_buffermap_vod(const neighbor_map& conn_map, timestamp_t now);

		void modify_media_packet_header_before_send(media_packet&pkt, peer_connection* conn, bool isPush);

		void send_buffermap_to(peer_connection* conn);
		void send_media_packet(peer_connection_sptr conn, seqno_t seq, bool push, int smoothDelay);
		void do_subscription_push(media_packet&pkt, timestamp_t now);

		boost::optional<seqno_t> remote_urgent_seqno(const peer_sptr& p, timestamp_t now);

	private:
		//基本属性
		boost::optional<timestamp_t>      recvd_first_packet_time_;
		smoother                          smoother_;
		topology_wptr                     topology_;
		boost::scoped_ptr<stream_monitor> stream_monitor_;
		int                               delay_guarantee_;
		int                               backfetch_cnt_;
		seqno_t							  max_seqno_, min_seqno_;
		double							  hop_;
		std::vector<double>				  substream_hop_;
		bool                              be_super_seed_;
		bool                              just_be_provider_;   //只作为服务者，本地并不播放这一频道

		//子流订阅
		typedef std::vector<std::list<peer_connection_wptr> > out_substream_vector;
		typedef std::vector<peer_connection_wptr> in_substream_vector;
		in_substream_vector           in_substream_;               //本地从其他节点那里订阅的子流
		out_substream_vector	      out_substream_;              //其他节点从本地订阅的子流
		std::deque<unsubscript_elm>   unsubscrips_;                //延迟一段时间后再处理取消订阅
		int                           in_subscription_state_confirm_;
		bool                          is_in_subscription_state_; //是否进入了订阅状态

		//timer调度控制相关变量
		timer_sptr  timer_;
		timestamp_t last_media_confirm_time_;
		timestamp_t last_exchange_buffermap_time_;
		timestamp_t last_pull_time_;
		timestamp_t last_quality_report_time_;
		timestamp_t last_info_report_time_;

		//调度过程属性
		timestamp_t                   scheduling_start_time_;
		timestamp_t                   last_subscrib_time_;
		timestamp_t                   last_average_subscrib_rtt_calc_time_;
		int                           average_subscrib_rtt_;
		timestamp_t                   last_get_global_local_to_remote_speed_time_;
		int                           global_local_to_remote_speed_;
		double                        src_packet_rate_;
		boost::optional<double>       average_packet_rate_;
		boost::optional<uint8_t>      server_session_id_;
		boost::optional<timestamp_t>  low_speed_time_;
		boost::optional<int>		  low_speed_delay_play_time_;
		peer_connection*              prepare_erase_conn_;

		//////////////////////////////////////////////////////////////////////////
		//为避免频繁构造而将一些临时变量提升为类的成员变量
		std::vector<seqno_t>                   seqnomap_buffer_;
		boost::scoped_ptr<media_request_msg>   snd_media_request_msg_;
		buffermap_exchange_msg                 buffermap_exchange_msg_;
		media_sent_confirm_msg                 media_sent_confirm_msg_;
		//////////////////////////////////////////////////////////////////////////
	private:
		boost::scoped_ptr<buffer_manager>		           buffer_manager_;
		boost::shared_ptr<media_dispatcher>		           media_dispatcher_;
		boost::shared_ptr<stream_seed>			           stream_seed_;
		boost::shared_ptr<heuristic_scheduling_strategy>   scheduling_strategy_;
	};
}


#endif