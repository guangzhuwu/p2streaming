//
// hub_topology.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2008 GuangZhu Wu (WuGuangZhu@gmail.com)
//
// All rights reserved. 
//

#include "client/peer.h"
#include "client/peer_connection.h"
#include "client/client_service.h"
#include "client/hub/hub_scheduling.h"
#include "client/hub/hub_topology.h"

using namespace p2client;

//TODO：以下功能未实现
//////////////////////////////////////////////////////////////////////////
//该client_service并未使用DHT等较高级的网络拓扑。根据聊天室的具体需求，大体设计思路为:
//每个节点在进入聊天室后，逐渐同其它节点建立连接，链接数最多
//MAX_MESSAGE_CONN_CNT=100，ping_interval设置为无穷大(disable ping，依靠tracker
//的掉线公告来知晓节点的在线状态)。链接要求稳定，即一旦链接，除非对方掉线，将
//不会轻易断开连接。链接的节点每隔INFO_EXCHANGE_INTERVAL=20s相互交换信息，信息主要包括
//其链接着的节点（可以使用增量信息方式减少overhead）。这样，节点就能知道其邻居节点
//都和哪些节点相连。
//当一个节点A需要发送p2p消息给另一个节点时D时；A首先查看是否和D相连，如果没有连接
//，则A搜索A的邻居节点，看谁和D相连，如果发现B和D相连，则将请B路由消息给D；如果
//发送未能成功，则A直接请求Tracker路由。这会以很高的概率通过直接发送或者邻居转发
//完成p2p消息；只有少量消息需要通过Tracker路由；也避免了nat穿越等的一些麻烦。
//公聊消息使用flood方式。
//////////////////////////////////////////////////////////////////////////

#define GUARD_OVERLAY \
	client_service_sptr  ovl=client_service_.lock();\
	if (!ovl) {stop();return;}

namespace
{
	time_duration MAINTAINER_INTERVAL=milliseconds(500);
}

//消息处理函数注册
//void hub_topology::register_handler()
//{
//#define REGISTER_HANDLER(msgType, handler)\
//	msg_handler_map_.insert(std::make_pair(msgType, boost::bind(&this_type::handler, SHARED_OBJ_FROM_THIS, _1, _2) ) );
//
//	REGISTER_HANDLER(peer_peer_msg::handshake_msg, on_recvd_handshake);
//}

hub_topology::hub_topology(client_service_sptr ovl)
:overlay(ovl, HUB_TOPOLOGY)
{
	set_obj_desc("hub_topology");

	BOOST_ASSERT(ovl);
}

hub_topology::~hub_topology()
{
}

void hub_topology::start()
{
	const std::string& channelUUID = get_client_param_sptr()->channel_uuid;
	topology_acceptor_domain_base_=std::string("hub_topology")+"/"+channelUUID;
	overlay::start();
	
	//启动调度器
	scheduling_=hub_scheduling::create(SHARED_OBJ_FROM_THIS);
	scheduling_->start();

	//启动URDP监听器，监听连接请求
	DEBUG_SCOPE(
		std::cout<<"hub_topology udp_local_endpoint_:"<<udp_local_endpoint_<<std::endl;
	std::cout<<"hub_topology tcp_local_endpoint_:"<<tcp_local_endpoint_<<std::endl;
	);
}

void hub_topology::stop(bool flush)
{
	overlay::stop(flush);
}

//void hub_topology::on_accepted(peer_connection_sptr conn, const error_code& ec)
//{
//	if(!ec)
//	{
//		//被动链接在未收到handshaker前是不放到neighbors中的，也不交给stream_scheduling处理
//		BOOST_ASSERT(!conn->get_peer());//此时还没有绑定具体peer
//		keep_pending_passive_sockets(conn);
//		conn->register_message_handler(peer_peer_msg::handshake_msg).bind(
//			&this_type::on_recvd_handshake, this, conn.get(), _1);
//		conn->disconnected_signal().bind(&this_type::on_disconnected, this, conn.get(), _1);
//		conn->ping_interval(HUB_PEER_PEER_PING_INTERVAL);
//		conn->keep_async_receiving();
//	}
//	else
//	{
//		BOOST_ASSERT(!pending_passive_sockets_.is_keeped(conn));
//	}
//}
//
//void hub_topology::on_connected(peer_connection* connPtr, const error_code& ec)
//{
//	BOOST_ASSERT(connPtr->get_peer());
//
//	peer_connection_sptr conn=connPtr->shared_obj_from_this<peer_connection>();
//	if (!ec)
//	{
//		if(!pending_to_neighbor(conn, HUB_NEIGHTBOR_PEER_CNT))
//		{
//			conn->close();
//			return;
//		}
//
//		//将除handshake外，消息处理权交给scheduling
//		scheduling_->register_message_handler(conn.get());
//		conn->register_message_handler(peer_peer_msg::handshake_msg).bind(
//			&this_type::on_recvd_handshake, this, conn.get(), _1);//handshake消息由SHARED_OBJ_FROM_THIS处理
//		conn->disconnected_signal().bind(&this_type::on_disconnected, this, conn.get(), _1);
//		conn->keep_async_receiving();
//		conn->ping_interval(HUB_PEER_PEER_PING_INTERVAL);
//
//		//本地主动连接对方。一旦链接建立，立刻向对方发送一个handshanke
//		scheduling_->send_handshake_to(conn.get());
//	}
//	else
//	{
//		BOOST_ASSERT(conn->get_peer());
//		conn->keep_async_receiving();
//		peer_sptr p=conn->get_peer();
//		if (p)
//		{
//			peer_id_t id=p->peer_info().peer_id();
//			pending_to_member(p);
//			//此时，并不确信这一节点是否掉线或者不可达；所以，并不删除此节点，
//			//但一段时间内不再次链接这一节点
//			low_capacity_peer_keeper_.try_keep(id, seconds(5));
//		}
//	}
//}
//
//void hub_topology::on_disconnected(peer_connection* conn, const error_code& ec)
//{
//	scheduling_->on_disconnected(conn, ec);
//	peer_sptr p=conn->get_peer();
//	if (p)
//	{
//		neighbor_to_member(conn->shared_obj_from_this<peer_connection>());
//	}
//}
//
//void hub_topology::on_recvd_handshake(peer_connection* connPtr, safe_buffer& buf)
//{
//	GUARD_OVERLAY;
//
//	peer_connection_sptr conn=connPtr->shared_obj_from_this<peer_connection>();
//
//	pending_passive_sockets_.erase(conn);
//
//	//解析handshake_msg，如果不知道这个peer则加入到peer池中
//	p2p_handshake_msg msg;
//	if(!parser(buf, msg))
//	{
//		PEER_LOGGER(warning)<<"can't parser p2p_handshake_msg";
//		conn->close();
//		return;
//	}
//	ovl->known_new_peer(msg.peer_info());
//
//	boost::shared_ptr<peer> p=ovl->find_peer(msg.peer_info().peer_id());
//	if (!p)//由于某原因并未能在client_service中注册本节点
//	{
//		conn->close();
//		return;
//	}
//	//如果还没有设置peer，说明是一个被动链接，只有这种情况下才回送一个handshake
//	bool needSendHandshake=(conn->get_peer().get()==NULL);
//	//将peer绑定在本连接上
//	conn->set_peer(p);
//	//尝试members移动到neighbors
//	if(!member_to_neighbor(conn, HUB_NEIGHTBOR_PEER_CNT))
//	{
//		conn->close();
//		return;
//	}
//
//	//成为邻居节点成功, 处理msg中携带的buffermap和pieceinfo信息
//	//if (msg.has_buffermap())
//	//{
//	//	scheduling_->process_recvd_buffermap(msg.buffermap(), conn.get());
//	//}
//	//if (msg.has_compressed_buffermap())
//	//{
//
//	//}
//	//for (int i=0;i<msg.pieceinfo_size();++i)
//	//{
//	//	stream_scheduling_->process_recvd_pieceinfo(msg.pieceinfo(i), conn.get());
//	//}
//
//	if (needSendHandshake)
//	{
//		//将消息处理权交给stream_scheduling
//		scheduling_->register_message_handler(conn.get());
//
//		//回复一个handshake，使得对方了解本节点的最新状态
//		scheduling_->send_handshake_to(conn.get());
//	}
//}

bool hub_topology::is_black_peer(const peer_id_t& id)
{
	return low_capacity_peer_keeper_.is_keeped(id);
}

void hub_topology::try_shrink_neighbors(bool explicitShrink)
{
}

bool hub_topology::can_be_neighbor(peer_sptr p)
{
	return true;
}
