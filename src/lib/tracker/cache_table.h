//
// cache_table.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_cache_table_h__
#define tracker_cache_table_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <string>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "tracker/config.h"

namespace p2tracker{
	namespace multi_index=boost::multi_index;
	class cache_service;

	class catch_peer_connection
		:public message_socket
	{
		typedef catch_peer_connection this_type;
		SHARED_ACCESS_DECLARE;

	public:
		typedef catch_peer_connection connection_base_type;

	public:
		catch_peer_connection(io_service& ios, bool realTimeUsage, bool isPassive)
			:message_socket(ios, realTimeUsage, isPassive)
		{}
		virtual ~catch_peer_connection(){}

	public:
		peer_info& get_peer_info()
		{
			return info_;
		}

	protected:
		peer_info info_;
	};
	RDP_DECLARE(catch_peer_connection, catch_peer_acceptor);


	class cache_table
		: public basic_object
		, public basic_tracker_object
	{
		typedef catch_peer_connection peer_connection;
		typedef catch_peer_connection::shared_ptr peer_sptr;
		typedef catch_peer_connection::weak_ptr peer_wptr;
		typedef variant_endpoint endpoint;

	public:
		typedef cache_table this_type;

		struct peer:object_allocator{
			catch_peer_connection_sptr m_socket;
			peer_id_t	m_id;
			ip_port_t	m_external_ipport;
			peer_key_t	m_key;
			timed_keeper_set<uint64_t> m_msg_relay_keeper;//该节点在一定时间内的msg relay条数记录
			int8_t		m_healthy;//充盈度, server的充盈度是100，为了区分其它普通节点，设置充盈度是大于100
		};

		typedef multi_index::multi_index_container<
			peer, 
			multi_index::indexed_by<
			multi_index::ordered_unique<multi_index::member<peer, peer_id_t, &peer::m_id>, std::less<peer_id_t> >, 
			multi_index::ordered_non_unique<multi_index::member<peer, ip_port_t, &peer::m_external_ipport> >, 
			multi_index::ordered_unique<multi_index::member<peer, peer_sptr, &peer::m_socket> >, 
			multi_index::ordered_non_unique<multi_index::member<peer, int8_t, &peer::m_healthy> >
			> 
		> peer_set;

		typedef peer_set::nth_index<0>::type id_index_type;
		typedef peer_set::nth_index<1>::type ip_index_type;
		typedef peer_set::nth_index<2>::type socket_index_type;
		typedef peer_set::nth_index<3>::type healthy_index_type;

	protected:
		//服务器信息
		typedef peer_set server_set;
		id_index_type& id_index(peer_set& peers){return multi_index::get<0>(peers);}
		ip_index_type& ip_index(peer_set& peers){return multi_index::get<1>(peers);}
		const id_index_type& id_index(const peer_set& peers){return multi_index::get<0>(peers);}
		const ip_index_type& ip_index(const peer_set& peers){return multi_index::get<1>(peers);}
		socket_index_type& socket_index(peer_set& peers){return multi_index::get<2>(peers);}
		healthy_index_type& healthy_index(peer_set& peers){return multi_index::get<3>(peers);}
		const socket_index_type& socket_index(const peer_set& peers){return multi_index::get<2>(peers);}
		const healthy_index_type& healthy_index(const peer_set& peers){return multi_index::get<3>(peers);}

	public:
		cache_table(boost::shared_ptr<cache_service>svc, const std::string& channelID);
		virtual ~cache_table();

	public:
		//如果插入成功，返回peer*；否则，返回NULL
		const peer* updata(peer_set& peers, peer_sptr conn, int health);
		const peer* insert(peer_sptr conn, int healthy);
		void erase(peer_connection* conn, error_code=error_code());
		int size(){return (int)ip_index(peers_).size();}
		vod_channel_info& channel_info(){return channel_info_;}
		void set_channel_info(const vod_channel_info& channel_info);

		void set_server_socket(message_socket_sptr conn){server_socket_ = conn;} //主服务器
		message_socket_sptr server_socket(){return server_socket_;}
		void set_server_info(const peer_info& info){server_info_ = info;}
		const peer_info& server_info(){return server_info_;}
		const std::string& channel_id(){return channel_uuid_;}
		void reply_peers(peer_connection* sock, p2ts_peer_request_msg& msg);

		void login_reply(peer_sptr conn, p2ts_login_msg& msg, 
			const cache_table::peer* new_peer, bool findMember);
		//网络消息处理
	public:
		void on_recvd_cache_peer_request(peer_connection*, const safe_buffer&);
		void on_recvd_failure_report(peer_connection*, const safe_buffer&);
		void on_recvd_relay(peer_connection*, const safe_buffer&);
		void on_recvd_logout(peer_connection*, const safe_buffer&);

	protected:
		void register_message_handler(peer_connection* conn);
		
	
	protected:
		void find_peer_for(const peer& target, std::vector<const peer*>& returnVec, 
			size_t maxReturnCnt=20);
		void find_assist_server_for(const peer& target, std::vector<const peer*>& returnVec, 
			size_t max_return_cnt = 2);
		const peer* find(peer_sptr conn);
		const peer* find(const peer_id_t& id); 

		template<typename msg_type>
		void find_member_for(const cache_table::peer& target, msg_type& msg);
		template<typename msg_type>
		void add_peer_info(msg_type& msg, std::vector<const peer*>& returnVec);

		void set_socket_info_to(peer* p, peer_connection* sock);
		const peer* insert(peer_set& peers, peer_sptr conn, int healthy);
		void __erase(peer_set& peers, peer_connection* conn, error_code ec = error_code());
		void find_peer_for(const peer& target, const peer_set& peers, 
			std::vector<const peer*>& returnVec, size_t maxReturnCnt);
	
	protected:
		//help function
		bool keep_relay_msg(uint64_t msg_id, peer* p);
		void __relay_msg(relay_msg& msg, const peer* p);

		void find_peer_for(const peer& target, const peer_set& peers, std::vector<const peer*>& returnVec);
		int find_peer_by_max_health(const peer& target, const peer_set& peers, 
			std::vector<const peer*>& returnVec, size_t maxReturnCnt);
		bool is_assist_server(peer_sptr conn);
		bool is_server(const peer& p);
		void set_peer_info(peer* p, peer_sptr conn, const int healthy);
		bool is_same_edp(const peer& target, const peer& candidate);
		bool insert(const peer& p);
		double generate_probability(peer_set peers, size_t max_return_cnt, size_t already_pick_cnt);
		boost::uint64_t peer_duration_time(const peer& target, const boost::uint64_t& now);

	private:
		peer_set peers_;
		boost::weak_ptr<cache_service> cache_service_;
		std::string channel_uuid_;
		vod_channel_info channel_info_;

		//频道服务连接, 包括server和assistant server
		server_set server_set_;	
		message_socket_sptr server_socket_;
		peer_info server_info_;

		//使用到的临时变量，升级到成员变量
		boost::unordered_set<uint64_t> sets_;
		timed_keeper_set<uint64_t> relay_msg_keeper_;
	};
}

#endif // tracker_cache_table_h__
