//
// server_tracker_session.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef server_tracker_session_h__
#define server_tracker_session_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "server/config.h"
#include "simple_server/simple_distributor.h"

namespace p2server{

	class server_service;

	class tracker_session
		:public basic_engine_object
	{
		typedef tracker_session this_type;
		SHARED_ACCESS_DECLARE;

		typedef p2engine::rough_timer timer;
		typedef variant_endpoint endpoint;
		typedef p2simple::simple_distributor_interface simple_distributor_interface;
		typedef boost::shared_ptr<simple_distributor_interface> simple_distributor_sptr;

	protected:
		tracker_session(io_service& net_svc, server_param_sptr param);
		virtual ~tracker_session();

	public:
		static shared_ptr create(io_service& net_svc, server_param_sptr param)
		{
			shared_ptr obj(new this_type(net_svc, param), 
				shared_access_destroy<this_type>());
			return obj;
		}

		void start(boost::shared_ptr<server_service> serverSvc);
		void stop();

		int client_count()const
		{
			return client_count_;
		}
		double playing_quality()const
		{
			return playing_quality_;
		}
		double global_remote_to_local_lost_rate()const
		{
			return global_remote_to_local_lost_rate_;
		}

	protected:
		void connect();

	protected:
		void register_server_message_handler(message_socket* conn);
		void on_recvd_channel_info_reply(message_socket*, safe_buffer);
		void on_recvd_distribute(message_socket*, safe_buffer);
	protected:
		//网络事件处理
		void on_connected(message_socket* conn, const error_code& ec);
		void on_disconnected(message_socket* conn, const error_code& ec);

	protected:
		//定时事件处理
		void on_info_report_timer();
		void on_info_request_timer();

	protected:
		boost::weak_ptr<server_service> server_service_;
		message_socket_sptr urdp_socket_;
		server_param_sptr server_param_;

		boost::shared_ptr<timer> info_request_timer_;//向tracker请求频道状态
		boost::shared_ptr<timer> info_report_timer_;
		boost::shared_ptr<timer> delay_connect_timer_;

		int				client_count_;
		double			playing_quality_;
		double			global_remote_to_local_lost_rate_;
	};

}

#endif//server_tracker_session_h__