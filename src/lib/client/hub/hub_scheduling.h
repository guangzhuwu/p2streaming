//
// hub_scheduling.h
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

namespace p2client{

	class hub_topology;

	class hub_scheduling
		:public scheduling_base
	{
		typedef hub_scheduling this_type;
		SHARED_ACCESS_DECLARE;

		friend class hub_topology;
		typedef rough_timer timer;

	protected:
		hub_scheduling(hub_topology_sptr);
		virtual ~hub_scheduling();

	public:
		static shared_ptr create(hub_topology_sptr tpl)
		{
			return shared_ptr(new this_type(tpl), 
				shared_access_destroy<this_type>()
				);
		}

		virtual void start();
		virtual void stop(bool flush=false);
		virtual void set_play_offset(int64_t);
		virtual void send_handshake_to(peer_connection* conn);
		virtual void send_buffermap_to(peer_connection* conn)
		{}
		virtual void process_recvd_buffermap(const buffermap_info& bufmap, 
			peer_connection* p)
		{}
		virtual void neighbor_erased(const peer_id_t&);
	protected:
		//网络事件处理
		//void on_connected(peer_connection* conn, const error_code& ec);
		void on_disconnected(peer_connection* conn, const error_code& ec);
	protected:
		//网络消息处理
		void register_message_handler(peer_connection* conn);
		void on_recvd_supervize_request(peer_connection* conn, safe_buffer buf);
	protected:
		//定时事件处理
		void on_check_supervize_timer(VC9_BIND_BUG_PARAM_DECLARE);

	protected:
		hub_topology_wptr topology_;

		boost::shared_ptr<timer> supervize_timer_;
		boost::unordered_set<peer_id_t> supervisors_;
		boost::unordered_set<peer_id_t> supervised_;

		boost::unordered_map<peer_connection*, boost::shared_ptr<peer_connection> >connections_;
	};
}

#endif
