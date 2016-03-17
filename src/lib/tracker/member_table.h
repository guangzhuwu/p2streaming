//
// member_table.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_member_table_h__
#define tracker_member_table_h__

#include "tracker/config.h"
#include <p2engine/push_warning_option.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/scope_exit.hpp>
#include <vector>
#include <string>
#include "p2engine/shared_access.hpp"
#include <p2engine/pop_warning_option.hpp>

namespace p2tracker{
	namespace multi_index=boost::multi_index;
	class member_service;

	class member_table 
		: public object_allocator
		, public basic_tracker_object
	{
		typedef member_table this_type;
		typedef message_socket::shared_ptr socket_sptr;
		typedef message_socket::weak_ptr socket_wptr;
		typedef variant_endpoint endpoint;

	public:
		struct peer:basic_object{
			peer_info					m_peer_info;
			peer_key_t					m_key;
			timed_keeper_set<uint64_t>	m_msg_relay_keeper;//该节点在一定时间内的msg relay条数记录
		private://排序用数据成员，设为私有，小心操作
			socket_sptr	m_socket;
			peer_id_t	m_id;
			ip_port_t	m_external_ipport;
			int			m_relative_playing_point_slot;

		public:
			peer(const peer_info& info, const socket_sptr& conn, int relative_playing_point)
				:m_peer_info(info), m_key(random<peer_key_t>(100, 0xffffffff))
				, m_socket(conn), m_id(info.peer_id())
				, m_relative_playing_point_slot(relative_playing_point)
			{
				error_code ec;
				endpoint edp = conn->remote_endpoint(ec);
				set_ipport(ip_port_t(edp.address().to_v4().to_ulong(), edp.port()));
				m_peer_info.set_external_ip(m_external_ipport.ip);
				if(conn->connection_category()==message_socket::UDP)
					m_peer_info.set_external_udp_port(m_external_ipport.port);
				else
					m_peer_info.set_external_tcp_port(m_external_ipport.port);
				m_peer_info.set_join_time((tick_type)tick_now());
			}
			void set_socket(const socket_sptr& conn){m_socket=conn;}
			void set_id(const peer_id_t& id){m_id=id;}
			void set_ipport(const ip_port_t& ipport){m_external_ipport=ipport;}
			void set_relative_playing_point_slot(int point){m_relative_playing_point_slot=point;}

			const socket_sptr& get_socket()const {return m_socket;}
			const peer_id_t& get_id()const{return m_id;}
			const ip_port_t& get_ipport()const{return m_external_ipport;}
			int get_relative_playing_point_slot()const{return m_relative_playing_point_slot;}

			struct socket_extractor
			{
				typedef socket_sptr result_type;
				const result_type& operator()(const boost::shared_ptr<peer>& p)const
				{return p->m_socket;}
			};
			struct id_extractor
			{
				typedef peer_id_t result_type;
				const result_type& operator()(const boost::shared_ptr<peer>& p)const
				{return p->m_id;}
			};
			struct ip_port_extractor
			{
				typedef ip_port_t result_type;
				const result_type& operator()(const boost::shared_ptr<peer>& p)const
				{return p->m_external_ipport;}
			};
			struct relative_playing_point_extractor
			{
				typedef int result_type;
				const result_type& operator()(const boost::shared_ptr<peer>& p)const
				{return p->m_relative_playing_point_slot;}
			};
		};

		typedef multi_index::multi_index_container<
			boost::shared_ptr<peer>, 
			multi_index::indexed_by<
			multi_index::ordered_unique<peer::id_extractor>, 
			multi_index::ordered_unique<peer::socket_extractor>, 
			multi_index::ordered_non_unique<peer::ip_port_extractor>, 
			multi_index::ordered_non_unique<peer::relative_playing_point_extractor>
			> 
		> peer_set;

		typedef peer_set::nth_index<0>::type id_index_type;
		typedef peer_set::nth_index<1>::type socket_index_type;
		typedef peer_set::nth_index<2>::type ip_index_type;
		typedef peer_set::nth_index<3>::type playing_point_index_type;

	protected:
		id_index_type& id_index(peer_set& sets){return multi_index::get<0>(sets);}
		socket_index_type& socket_index(peer_set& sets){return multi_index::get<1>(sets);}
		ip_index_type& ip_index(peer_set& sets){return multi_index::get<2>(sets);}
		playing_point_index_type& playing_point_slot_index(peer_set& sets){return multi_index::get<3>(sets);}
		const id_index_type& id_index(const peer_set& sets)const{return multi_index::get<0>(sets);}
		const socket_index_type& socket_index(const peer_set& sets)const{return multi_index::get<1>(sets);}
		const ip_index_type& ip_index(const peer_set& sets)const{return multi_index::get<2>(sets);}
		const playing_point_index_type& playing_point_slot_index(const peer_set& sets)const{return multi_index::get<3>(sets);}

	public:
		member_table(boost::shared_ptr<member_service> svc, int totalVideoTimeMmsec);//影片持续时间为负时说明是直播，否则为点播类
		virtual ~member_table();

	public:
		const peer* updata(socket_sptr conn, const peer_info& info, int dynamic_pt);
		void set_privilege(const peer_id_t& peerID, int privilegeID, bool privilege);

		//如果插入成功，返回peer*；否则，返回NULL
		const peer* insert(socket_sptr conn, const peer_info& infoIn, int dynamic_pt);
		void find_member_for(const peer& target, std::vector<const peer*>& returnVec, 
			size_t maxReturnCnt=12)//maxReturnCnt太大容易造成udp包的frag，15个就有些偏大了
		{
			find_member_for(target, peers_, returnVec, maxReturnCnt);
		}
		void find_assist_server_for(const peer& target, std::vector<const peer*>& returnVec, 
			size_t maxReturnCnt = 2);
		void get_all_peers(std::vector<const peer*>& returnVec);
		const peer* find(const peer_id_t& peerID);
		const peer* find(const socket_sptr& conn);
		const peer* find(message_socket* conn)
		{
			if (conn)
				return find(conn->shared_obj_from_this<message_socket>());
			return NULL;
		}

		void erase(const peer_id_t& peerID)
		{
			id_index(peers_).erase(peerID);
		}
		void erase(const socket_sptr& conn);
		size_t size()const{return id_index(peers_).size();}
		bool empty()const{return id_index(peers_).empty();}
		void update_video_time(int64_t video_time_msec, int64_t film_length);
		int translate_relative_playing_point_slot(int dynamic_pt);

	protected:
		const peer* insert(peer_set& peers, socket_sptr conn, const peer_info& info, int dynamic_pt);
		const peer* updata(peer_set& peers, socket_sptr conn, const peer_info& info, int dynamic_pt);
		void find_member_for(const peer& target, const peer_set& peers, 
			std::vector<const peer*>& returnVec, size_t maxReturnCnt);

	protected:
		//help function
		void set_peer_info(peer* p, const peer_info& info, socket_sptr conn, int dynamic_pt);
		bool play_point_is_valide(const peer& target, const peer& candidate);
		void find_vod_member(const peer& target, const peer_set& peers, 
			std::vector<const peer*>& returnVec, size_t maxReturnCnt);
		void find_live_member(const peer& target, const peer_set& peers, 
			std::vector<const peer*>& returnVec, size_t maxReturnCnt);
		double generate_probability(const peer_set& peers, size_t max_return_cnt, 
			size_t already_pick_cnt);
		bool is_vod_type()
		{
			return total_video_time_msec_>0 ? true : false;
		}
		static bool is_same_id(const peer& target, const peer& candidate)
		{
			return candidate.get_id() == target.get_id();
		}

	private:
		peer_set peers_;
		int32_t total_video_time_msec_;
		peer_set server_set_;//辅助服务器
		double play_point_adjust_coeff_;
	};

	/*! struct channel
	* \brief 频道tracker_service.
		*
	* \modify: 2011.04.22 created.
	*/
	class channel 
		: public object_allocator
		, public basic_tracker_object
	{
		typedef channel       this_type;
		SHARED_ACCESS_DECLARE;
		enum{QUALITY_REPORT_INTERVAL=10000};
		enum{MAX_CNT=20};
		typedef member_table::peer	peer;
		typedef rough_timer			timer;
	public:
		static boost::shared_ptr<channel> create(boost::shared_ptr<member_service> svc, 
			message_socket_sptr sock, 
			const std::string& channelID, 
			int64_t totalVideoDurationMsec = -1)
		{
			return boost::shared_ptr<channel>(new this_type(svc, sock, channelID, totalVideoDurationMsec), 
				shared_access_destroy<this_type>());
		}
	protected:
		channel(boost::shared_ptr<member_service> svc, 
			message_socket_sptr sock, 
			const std::string& channelID, 
			int64_t totalVideoDurationMsec);
		~channel(){}

	protected:
		/*channel::channel_broadcast*/
		class channel_broadcast 
		{
		public:
			typedef std::pair<peer_id_t, int>	pair;
			typedef std::list<pair>				pair_list;

			channel_broadcast();
			void operator ()(channel* this_channel, bool must_broad_cast);

		protected:
			void recent_offline(ts2p_room_info_msg& msg);
			void recent_login(ts2p_room_info_msg& msg);
			void online_peers(ts2p_room_info_msg& msg);
			void broadcast_info();

		protected:
			//help function
			void get_peers_waiting_for_broadcast();
			void broad_cast_msg(const ts2p_room_info_msg& msg);

		private:
			bool has_written(const peer* p)const
			{
				return (hasWritten_.find(p) != hasWritten_.end()); 
			}
			bool pair_need_erase(const pair& idPair);

		private:
			int    new_peer_n_;
			channel*  channel_;
			bool   must_broad_cast_;
			boost::unordered_set<const peer*> hasWritten_;
		}; //broad_cast define end

		//记录server创建的但是没有节点了的频道
	public:
		struct suspend_channel
		{
			suspend_channel(const channel& channel_to_removel)
				: server_sockt_(channel_to_removel.m_server_socket)
				, server_info_(channel_to_removel.server_info())
				, vod_channel_info_(channel_to_removel.vod_channel_info_)
				, live_channel_info_(channel_to_removel.live_channel_info_)
			{}

			message_socket_sptr  server_sockt_;
			peer_info    server_info_;
			boost::optional<vod_channel_info> vod_channel_info_;
			boost::optional<live_channel_info> live_channel_info_;
		};

	public:
		void start(const peer_info& serv_info, const live_channel_info& info);
		void start(const peer_info& serv_info, const vod_channel_info& info);
		void stop();
		void kickout(const peer_id_t& id); 
		const peer* insert(message_socket_sptr conn, const peer_info& infoIn);
		void erase(message_socket_sptr conn, error_code = error_code());
		void login_reply(message_socket_sptr conn, p2ts_login_msg& msg, const peer* p);

		const peer_info& server_info()const{return server_info_;}
		void set_server_info(peer_info& info){server_info_ = info;}
		message_socket_sptr& server_socket(){return m_server_socket;}
		void set_server_socket(message_socket_sptr sock);
		const std::string& channel_id(){return m_channel_id;}
		void set_channel_info(const vod_channel_info& info);
		void set_channel_info(const live_channel_info& info){live_channel_info_.reset(info);}

		void set_last_update_time(timestamp_t now){last_updata_live_channel_info_time_ = now;}
		void reset_broadcaster_timer(const time_duration& periodical_duration);
		void add_recent_login_peers(const peer*p);
		int dynamic_play_point(const int play_point);
		//网络消息处理
	protected:
		void register_message_handler(message_socket*);
		void register_server_message_handler(message_socket* conn);
		void on_server_disconnected(message_socket* conn, error_code ec); //频道服务器断开
		void on_peer_disconnected(message_socket* conn, error_code ec); //节点断开

		void on_recvd_logout(message_socket*, const safe_buffer&);
		void on_recvd_peer_request(message_socket*, const safe_buffer&);
		void on_recvd_local_info_report(message_socket*, const safe_buffer&);
		void on_recvd_quality_report(message_socket*, const safe_buffer&);
		void on_recvd_failure_report(message_socket*, const safe_buffer&buf);
		void on_recvd_server_info_report(message_socket*, const safe_buffer&);
		void on_recvd_channel_info_request(message_socket*, const safe_buffer&);
		void on_recvd_relay(message_socket*, const safe_buffer&);

	protected:
		//help function
		template<typename msg_type>
		void find_member_for(const peer& target, msg_type& msg);
		template<typename msg_type>
		void add_peer_info(msg_type& msg, const std::vector<const peer*>& returnVec);

		timestamp_t server_now()const;
		seqno_t server_seqno()const;
		void set_reply_msg(const peer* p, ts2p_peer_reply_msg& msg, 
			const p2ts_peer_request_msg& reqMsg);

		void on_broadcast_room_info();
		void start_broadcast_timer(io_service& io_service_in);
		bool broadcast_condition();
		void recent_offline_peers_increase(const peer* p);

		bool keep_relay_msg(uint64_t msg_id, peer* p);

	public:
		const std::string					 m_channel_id;
		boost::shared_ptr<message_socket>	 m_server_socket;
		size_t								 m_peer_count;
		std::list<std::pair<peer_id_t, int> > m_recent_offline_peers;
		std::list<std::pair<boost::weak_ptr<peer>, int> > m_recent_login_peers;
		boost::shared_ptr<member_table>		 m_member_table;
		boost::weak_ptr<member_service>		 member_service_;

	protected:
		peer_info server_info_;
		boost::optional<live_channel_info> live_channel_info_;
		boost::optional<vod_channel_info>  vod_channel_info_;
		timestamp_t last_updata_live_channel_info_time_;
		int recent_offline_peers_not_broadcast_cnt_;
		timer::shared_ptr channel_info_broadcast_timer_; 

		timestamp_t last_broadcast_channel_info_time_;
		uint32_t relay_or_broadcast_msgid_seed_;//用来生成flood或relay消息的seq
		timed_keeper_set<uint64_t> relay_msg_keeper_;
		timestamp_t last_write_quality_time_;

		double playing_quality_;
		double global_rtol_lost_rate_;

		std::set<seqno_t, wrappable_less<seqno_t> > iframes_;

	};//channel define end
};
using namespace p2tracker;

#endif // tracker_member_table_h__