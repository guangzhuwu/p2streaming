//
// stream_topology.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (GuangZhuWu@gmail.com)
//
// All rights reserved. 
//

#include "client/stream/stream_topology.h"
#include "client/stream/stream_scheduling.h"
#include "client/peer.h"
#include "client/peer_connection.h"
#include "client/client_service.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#define  STREAM_TOPO_DBG(x) 
#else 
#define  STREAM_TOPO_DBG(x) /*x*/
#endif

using namespace p2client;

#define GUARD_OVERLAY \
	client_service_sptr  ovl=client_service_.lock();\
	if (!ovl) {stop();return;}

#define BOOL_GUARD_OVERLAY \
	client_service_sptr  ovl=client_service_.lock();\
	if (!ovl) {stop();return false;} \
	return true;

namespace
{
	time_duration NEIGHTBOR_CHOKE_INTERVAL=seconds(30);
}

//消息处理函数注册
//void stream_topology::register_handler()
//{
//#define REGISTER_HANDLER(msgType, handler)\
//	msg_handler_map_.insert(std::make_pair(msgType, boost::bind(&this_type::handler, SHARED_OBJ_FROM_THIS, _1, _2) ) );
//
//	REGISTER_HANDLER(peer_peer_msg::handshake_msg, on_recvd_handshake);
//}

stream_topology::stream_topology(client_service_sptr ovl)
:overlay(ovl, STREAM_TOPOLOGY) //这里的topology_id指的是拓扑类型ID，而不是节点ID
{
	set_obj_desc("stream_topology");

	timestamp_t now=timestamp_now();
	last_exchange_neighbor_time_=last_choke_neighbor_time_=now;

	BOOST_ASSERT(ovl);
}

stream_topology::~stream_topology()
{
}

//client_service启动stream_topology和hub_topology
//stream_topology启动stream_scheduling 
void stream_topology::start()
{
	GUARD_OVERLAY;

	const std::string& channelUUID = get_client_param_sptr()->channel_uuid;
	topology_acceptor_domain_base_=std::string("stream_topology")+"/"+channelUUID;
	
	//base start
	overlay::start();

	scheduling_=stream_scheduling::create(SHARED_OBJ_FROM_THIS);
	scheduling_->start();
}

void stream_topology::stop(bool flush)
{
	overlay::stop(flush);
}

bool stream_topology::is_black_peer(const peer_id_t& id)
{
	return low_capacity_peer_keeper_.is_keeped(id);
}

void stream_topology::try_shrink_neighbors(bool explicitShrink)
{
	BOOST_ASSERT(boost::dynamic_pointer_cast<stream_scheduling>(scheduling_) 
		== boost::static_pointer_cast<stream_scheduling>(scheduling_));
	boost::shared_ptr<stream_scheduling> scheduling
		=boost::static_pointer_cast<stream_scheduling>(scheduling_);	

	time_duration interval=explicitShrink?(NEIGHTBOR_CHOKE_INTERVAL/5):NEIGHTBOR_CHOKE_INTERVAL;
	timestamp_t now=timestamp_now();
	if(prepare_closing_connections_.empty()
		&&is_time_passed(interval.total_milliseconds(), last_choke_neighbor_time_, now)
		)
	{
		//执行choket类似算法，踢出低性能节点
		double lowestSpeed=(std::numeric_limits<int>::max)();
		peer_connection*	lowestConn=NULL;
		peer*	lowestPeer=NULL;
		neighbor_map::iterator itr=neighbors_conn_.begin();
		for (;itr!=neighbors_conn_.end();++itr)
		{
			peer_connection* conn=itr->second.get();
			BOOST_ASSERT(conn);
			peer* p=conn->get_peer().get();
			if (is_time_passed(20*1000, p->last_connect_time(topology_id_), now))
			{
				double toRemoteSpeed=std::max(100, p->download_from_local_speed());
				double toLocalSpeed=std::max(100, p->upload_to_local_speed());
				double toRemoteLostRate=conn->local_to_remote_lost_rate();
				double toLocalLostRate=conn->remote_to_local_lost_rate();
				double speed=0.4*toRemoteSpeed*toRemoteSpeed*(1.0-toRemoteLostRate*toRemoteLostRate)
					+0.6*toLocalSpeed*toLocalSpeed*(1.0-toLocalLostRate*toLocalLostRate);
				if (speed<lowestSpeed&&!scheduling->has_subscription(conn))
				{
					lowestSpeed=speed;
					lowestConn=conn;
					lowestPeer=p;
				}
			}
		}
		if (lowestConn
			//&&last_choke_neighbor_time_+interval<now
			//&&(double)lowestSpeed/((double)totalSpeed+FLT_MIN)<1.0/NEIGHTBOR_PEER_CNT
			)
		{
			prepare_closing_connection which;
			which.conn=lowestConn;
			which.conn_wptr=lowestConn->shared_obj_from_this<peer_connection>();
			which.erase_time=now+3000;
			prepare_closing_connections_.push_back(which);
			BOOST_ASSERT(prepare_closing_connections_.size()==1);

			scheduling->prepare_erase_neighbor(lowestConn);
		}
	}
	else if (!prepare_closing_connections_.empty())
	{
		BOOST_AUTO(begin, prepare_closing_connections_.begin());
		if (time_less(begin->erase_time, now))
		{
			if (begin->conn_wptr.expired()||!begin->conn->is_connected())
			{
				prepare_closing_connections_.erase(begin);
				return;
			}

			peer_connection* lowestConn=begin->conn;
			peer* lowestPeer=lowestConn->get_peer().get();
			STREAM_TOPO_DBG(error_code ec;
			std::cout<<"----------------------------------------haha============erase:"
				<<lowestConn->remote_endpoint(ec)<<"\n";
			);
			lowestPeer->set_udp_restricte(!lowestPeer->is_udp_restricte());
			lowestConn->close();
			neighbor_to_member(lowestConn->shared_obj_from_this<peer_connection>());
			last_choke_neighbor_time_=now;
			if (members_.size()>50)
			{
				peer_id_t lowestPeerID(lowestPeer->get_peer_info().peer_id());
				erase_low_capacity_peer(lowestPeerID);
				low_capacity_peer_keeper_.try_keep(lowestPeerID, seconds(20));
			}

			prepare_closing_connections_.erase(begin);
		}
	}

	if(prepare_closing_connections_.empty())
		scheduling->prepare_erase_neighbor(NULL);

}

bool stream_topology::can_be_neighbor(peer_sptr p)
{
	switch (get_client_param_sptr()->type)
	{
	case INTERACTIVE_LIVE_TYPE:
	case LIVE_TYPE:
		return true;

	case VOD_TYPE:
		{
			int chunkid=get_client_param_sptr()->smallest_seqno_absenct*PIECE_SIZE/PIECE_SIZE;
			if (chunkid>=p->chunk_map_size()*8)
				return false;
			if (is_bit(p->chunk_map(), chunkid)
				||is_bit(p->chunk_map(), chunkid+1)
				||is_bit(p->chunk_map(), chunkid-1)
				)
				return true;
		}


	case  BT_TYPE:
		return true;
	default:
		BOOST_ASSERT(0);
		return true;
	}
}
