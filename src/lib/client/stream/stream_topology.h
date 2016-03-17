//
// stream_topology.h
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2009 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//
#ifndef peer_stream_topology_h__
#define peer_stream_topology_h__

#include "client/overlay.h"

namespace p2client{

	class stream_scheduling;

	//数据调度拓扑层
	class stream_topology
		:public overlay
	{
		typedef stream_topology this_type;
		SHARED_ACCESS_DECLARE;
		typedef boost::shared_ptr<stream_scheduling> scheduling_sptr;
		typedef rough_timer timer;
	
	protected:
		stream_topology(client_service_sptr ovl);
		virtual ~stream_topology();

	public:
		static shared_ptr create(client_service_sptr ovl)
		{
			return shared_ptr(new this_type(ovl), 
				shared_access_destroy<this_type>());
		}

		//继承自overlay
		virtual void start();
		virtual void stop(bool flush = false);

		virtual void try_shrink_neighbors(bool explicitShrink );
		virtual bool is_black_peer(const peer_id_t& id);//是否在黑名单中
		virtual bool can_be_neighbor(peer_sptr p);

	protected:
		timestamp_t last_choke_neighbor_time_;//上次choke某peer的时刻
		struct prepare_closing_connection 
		{
			peer_connection* conn;
			boost::weak_ptr<peer_connection> conn_wptr;
			timestamp_t erase_time;
		};
		std::list<prepare_closing_connection>  prepare_closing_connections_;
	};
}

#endif //peer_stream_scheduling_h__
