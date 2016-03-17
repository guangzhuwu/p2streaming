//
// cache_service.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef tracker_cache_service_h__
#define tracker_cache_service_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "tracker/cache_table.h"
#include "common/policy.h"
#include "p2engine/acceptor.hpp"

#include <p2engine/push_warning_option.hpp>
#include <boost/unordered_map.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace p2tracker{

	class tracker_service;
	class tracker_service_logic_base;

	class cache_service
		:public basic_engine_object
		, public basic_tracker_object
		, public basic_mix_acceptor<cache_service, catch_peer_acceptor>
		, public ts2p_challenge_checker
	{
		typedef cache_service this_type;
		SHARED_ACCESS_DECLARE;

		typedef catch_peer_connection peer_connection;
		typedef catch_peer_connection::shared_ptr peer_sptr;
		typedef catch_peer_connection::weak_ptr peer_wptr;
		typedef boost::unordered_map<std::string, boost::shared_ptr<cache_table> > cache_table_map;

		friend class basic_mix_acceptor<cache_service, catch_peer_acceptor>;
	public:
		typedef variant_endpoint endpoint;
		typedef cache_table::peer peer;
		typedef rough_timer timer;

	protected:
		cache_service(io_service& net_svc, tracker_param_sptr param);
		virtual ~cache_service();

	public:
		static shared_ptr create(io_service& net_svc, tracker_param_sptr param)
		{
			return shared_ptr(new this_type(net_svc, param), 
				shared_access_destroy<this_type>());
		}
		void start(boost::shared_ptr<tracker_service_logic_base>tsLogic);
		void stop();
		void erase(const std::string&channelID);

	protected:
		//网络事件处理
		void on_accepted(peer_sptr conn, const error_code& ec);
	protected:
		//网络消息处理
		void register_message_handler(peer_connection*);

		void on_recvd_announce_cache(peer_connection*, const safe_buffer&);
		void on_recvd_login(peer_connection*, const safe_buffer&);

	protected:
		//help function
		void set_peer_info(peer_sptr conn, peer_info& info);
		boost::shared_ptr<cache_table> get_channel(const std::string& channelID);
		void recvd_cached_info(const p2ts_cache_announce_msg& msg, peer_sptr conn);
		void recvd_erase_channel(const p2ts_cache_announce_msg& msg, peer_sptr conn);
		void recvd_cached_channel(const p2ts_cache_announce_msg& msg, peer_sptr conn);
		bool challenge_channel_check(peer_sptr conn, const p2ts_login_msg& msg);
		void try_remove_empty_channel(const std::string& channelID);

	protected:
		boost::weak_ptr<tracker_service_logic_base> tracker_service_logic_;
		timed_keeper_set<peer_sptr> pending_sockets_;//刚刚建立的连接，如果一定超时时间内客户端没有做进一步的报文交互，这个链接将被自动关闭
		cache_table_map cache_tables_;
	};
}

#endif//tracker_cache_service_h__
