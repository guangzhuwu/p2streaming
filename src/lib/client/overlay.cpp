#include "client/overlay.h"
#include "client/peer.h"
#include "client/peer_connection.h"
#include "client/client_service.h"
#include "client/scheduling_base.h"
#include "client/tracker_manager.h"
#include "client/cache/cache_service.h"

int post_active_conn_cnt = 0;

#define DESIABLE_DEBUG_SCOPE 1
#if !defined(_DEBUG_SCOPE) && defined(NDEBUG) || DESIABLE_DEBUG_SCOPE
#	define  OVERLAY_DBG(x) 
#else 
#	define  OVERLAY_DBG(x) x
#endif

NAMESPACE_BEGIN(p2client);

namespace
{
#ifdef POOR_CPU
	time_duration MAINTAINER_INTERVAL = milliseconds(500);
	time_duration NEIGHTBOR_EXCHANGE_INTERVAL = seconds(60);
#else
	time_duration MAINTAINER_INTERVAL = milliseconds(450);
	time_duration NEIGHTBOR_EXCHANGE_INTERVAL = seconds(30);
#endif
}

//#define  PRINT_LINE 
#define  PRINT_LINE printf("--------------line:%d\n", __LINE__);
#define GUARD_CLIENT_SERVICE(returnValue) \
	client_service_sptr clientService=client_service_.lock();\
	if (!clientService) {stop();return returnValue;}


overlay::overlay(client_service_sptr clientService, int topologyID)
	: basic_engine_object(clientService->get_io_service())
	, basic_client_object(clientService->get_client_param_sptr())
	, client_service_(clientService)
	, topology_id_(topologyID)
	, neighbors_conn_((topologyID == STREAM_TOPOLOGY) ? STREAM_NEIGHTBOR_PEER_CNT : HUB_NEIGHTBOR_PEER_CNT)
{
	set_obj_desc("overlay");

	timestamp_t now = timestamp_now();
	last_exchange_neighbor_time_ = now;
	last_member_request_time_ = now;

	if (topology_id_ == STREAM_TOPOLOGY)
	{
		max_neighbor_cnt_ = get_client_param_sptr()->stream_neighbor_peer_cnt;
		neighbor_ping_interval_ = STREAM_PEER_PEER_PING_INTERVAL;
	}
	else if (topology_id_ == HUB_TOPOLOGY)
	{
		max_neighbor_cnt_ = get_client_param_sptr()->hub_neighbor_peer_cnt;
		neighbor_ping_interval_ = HUB_PEER_PEER_PING_INTERVAL;
	}
	else if (topology_id_ == CACHE_TOPOLOGY)
	{
		max_neighbor_cnt_ = get_client_param_sptr()->stream_neighbor_peer_cnt;
		neighbor_ping_interval_ = STREAM_PEER_PEER_PING_INTERVAL;
	}
	else
	{
		BOOST_ASSERT(0 && "必须明确指定拓扑的max_neighbor_cnt_、neighbor_ping_interval_等");
	}
}

overlay::~overlay()
{
	__close();
}

//等待其它节点连接请求
void overlay::start()
{
	//先停止
	stop();

	//启动URDP监听器，监听连接请求
	OVERLAY_DBG(
		std::cout << topology_id_ << "***********overlay start********************:" << (timestamp_t)timestamp_now() << std::endl;
	std::cout << "udp_local_endpoint_:" << udp_local_endpoint_ << std::endl;
	std::cout << "tcp_local_endpoint_:" << tcp_local_endpoint_ << std::endl;
	);

	error_code ec;
	std::string domain = topology_acceptor_domain_base_ + "/" + get_client_param_sptr()->local_info.peer_id();
	start_acceptor(tcp_local_endpoint_, udp_local_endpoint_, domain, ec, true);
	udp_local_endpoint_ = urdp_acceptor_->local_endpoint(ec);
	tcp_local_endpoint_ = trdp_acceptor_->local_endpoint(ec);

	OVERLAY_DBG(
		std::cout << "real udp_local_endpoint_:" << udp_local_endpoint_ << std::endl;
	std::cout << "real tcp_local_endpoint_:" << tcp_local_endpoint_ << std::endl;
	);

	//启动邻居拓扑维护定时器
	neighbor_maintainer_timer_ = timer::create(get_io_service());
	neighbor_maintainer_timer_->set_obj_desc("p2client::overlay::neighbor_maintainer_timer_");
	neighbor_maintainer_timer_->register_time_handler(boost::bind(&this_type::on_neighbor_maintainer_timer, this));
	neighbor_maintainer_timer_->async_keep_waiting(milliseconds(topology_id_ * 200), MAINTAINER_INTERVAL);

	//get_io_service().post(boost::bind(&this_type::connect_server, SHARED_OBJ_FROM_THIS));
}

void overlay::stop(bool flush)
{
	__close(flush);
}

void overlay::__close(bool flush)
{
	close_acceptor();

	if (neighbor_maintainer_timer_)
	{
		neighbor_maintainer_timer_->cancel();
		neighbor_maintainer_timer_.reset();
	}
	if (scheduling_)
	{
		scheduling_->stop(flush);
		scheduling_.reset();
	}

	BOOST_FOREACH(neighbor_map::value_type& v, neighbors_conn_)
	{
		v.second->close();
	}
	neighbors_conn_.clear();

	BOOST_FOREACH(peer_connection_sptr conn, pending_active_sockets_)
	{
		conn->close();
	}
	pending_active_sockets_.clear();

	BOOST_FOREACH(peer_connection_sptr conn, pending_passive_sockets_)
	{
		conn->close();
	}
	pending_passive_sockets_.clear();
}

void overlay::restart()
{
	start();
}

void overlay::set_play_offset(int64_t offset)
{
	if (scheduling_)
		scheduling_->set_play_offset(offset);
}

endpoint overlay::urdp_acceptor_local_endpoint()const
{
	error_code ec;
	if (urdp_acceptor_)
		return urdp_acceptor_->local_endpoint(ec);
	return endpoint();
}

endpoint overlay::trdp_acceptor_local_endpoint()const
{
	error_code ec;
	if (trdp_acceptor_)
		return trdp_acceptor_->local_endpoint(ec);
	return endpoint();
}

void overlay::on_accepted(peer_connection_sptr conn, const error_code& ec)
{
	if (!ec)
	{
		BOOST_ASSERT(scheduling_);
		//被动链接在未收到handshaker前是不放到neighbors中的，也不交给stream_scheduling处理
		BOOST_ASSERT(!conn->get_peer());//此时还没有绑定具体peer
		keep_pending_passive_sockets(conn);
		conn->register_message_handler(peer_peer_msg::handshake_msg, 
			boost::bind(&this_type::on_recvd_handshake, this, conn.get(), _1)
			);
		conn->register_message_handler(peer_peer_msg::neighbor_table_exchange, 
			boost::bind(&this_type::on_recvd_neighbor_table_exchange, this, conn.get(), _1)
			);
		conn->register_disconnected_handler(
			boost::bind(&this_type::on_disconnected, this, conn.get(), _1)
			);
		conn->ping_interval(neighbor_ping_interval_);
		conn->keep_async_receiving();
	}
	else
	{
		BOOST_ASSERT(!pending_passive_sockets_.is_keeped(conn));
	}
}

void overlay::on_connected(peer_connection* connPtr, const error_code& ec)
{
	BOOST_ASSERT(connPtr);
	if (!ec)
	{
		peer_connection_sptr conn = connPtr->shared_obj_from_this<peer_connection>();
		BOOST_ASSERT(connPtr->get_peer());
		BOOST_ASSERT(scheduling_);

		pending_active_sockets_.erase(conn);
		if (!to_neighbor(conn, max_neighbor_cnt_))
			return;

		peer* p = conn->get_peer().get();
		if (!p->playing_the_same_channel())
		{
			join_channel_msg msg;
			msg.set_channel_id(get_client_param_sptr()->channel_uuid);
			conn->async_send_reliable(serialize(msg), global_msg::join_channel);
		}

		conn->register_message_handler(peer_peer_msg::handshake_msg, 
			boost::bind(&this_type::on_recvd_handshake, this, conn.get(), _1));
		conn->register_message_handler(peer_peer_msg::neighbor_table_exchange, 
			boost::bind(&this_type::on_recvd_neighbor_table_exchange, this, conn.get(), _1)
			);
		conn->register_disconnected_handler(
			boost::bind(&this_type::on_disconnected, this, conn.get(), _1)
			);
		conn->keep_async_receiving();
		conn->ping_interval(neighbor_ping_interval_);

		//将除handshake外的消息处理权交给scheduling
		scheduling_->register_message_handler(conn.get());
		//本地主动连接对方，一旦链接建立，立刻向对方发送一个handshanke
		scheduling_->send_handshake_to(conn.get());
	}
	else
	{
		peer_sptr p = connPtr->get_peer();
		BOOST_ASSERT(p);
		if (p)
		{
			pending_to_member(p);
			peer_id_t id(p->get_peer_info().peer_id());
			//此时，并不确信这一节点是否掉线或者不可达；所以，并不删除此节点，
			//但一段时间内不再次链接这一节点
			low_capacity_peer_keeper_.try_keep(id, seconds(10));

			OVERLAY_DBG(;
			error_code temperr;
			std::cout << "XXXXXXXX----connect member "
				<< string_to_hex(p->get_peer_info().peer_id())
				<< " ip:" << connPtr->remote_endpoint(temperr)
				<< " error:" << ec.message()
				<< " same channel:" << p->playing_the_same_channel()
				<< std::endl;
			);
		}
	}
}

void overlay::on_recvd_handshake(peer_connection* connPtr, safe_buffer& buf)
{
	GUARD_CLIENT_SERVICE(;);

	peer_connection_sptr conn = connPtr->shared_obj_from_this<peer_connection>();

	pending_passive_sockets_.erase(conn);
	pending_active_sockets_.erase(conn);

	//解析handshake_msg，如果不知道这个peer则加入到peer池中
	p2p_handshake_msg msg;
	if (!parser(buf, msg))
	{
		//PEER_LOGGER(warning)<<"can't parser p2p_handshake_msg";
		{PRINT_LINE; conn->close(); }
		return;
	}
	clientService->on_known_new_peer(msg.peer_info(), clientService->is_same_channel(msg.playing_channel_id()));
	peer_sptr p = clientService->find_peer(peer_id_t(msg.peer_info().peer_id()));
	if (!p)//由于某原因并未能在client_service中注册本节点
	{
		{PRINT_LINE; conn->close(); }
		return;
	}
	//如果还没有设置peer，说明是一个被动链接，只有这种情况下才回送一个handshake, 
	//否则，送回一个buffermap.
	bool needSendHandshake = !conn->get_peer();
	if (needSendHandshake)
	{
		conn->set_peer(p);//将peer绑定在本连接上
		//尝试members移动到neighbors
		if (!to_neighbor(conn, max_neighbor_cnt_))
			return;
	}

	//成为邻居节点成功, 处理msg中携带的buffermap和pieceinfo信息
	if (msg.has_buffermap())
	{
		scheduling_->process_recvd_buffermap(msg.buffermap(), conn.get());
	}
	if (msg.has_compressed_buffermap())
	{
	}

	if (msg.has_chunkmap())
	{
		memcpy(p->chunk_map(), msg.chunkmap().c_str(), 
			std::min<size_t>(p->chunk_map_size(), msg.chunkmap().length()));
	}

	if (needSendHandshake)
	{
		//将消息处理权交给stream_scheduling
		scheduling_->register_message_handler(conn.get());
		//回复一个handshake，使得对方了解本节点的最新状态
		scheduling_->send_handshake_to(conn.get());
	}
}

void overlay::on_disconnected(peer_connection* conn, const error_code& ec)
{
	OVERLAY_DBG(
		error_code err;
	std::cout << "***********overlay::on_disconnected****************************:"
		<< conn->remote_endpoint(err)
		<< std::endl;
	);
	if (conn&&conn->get_peer())
	{
		neighbor_to_member(conn->shared_obj_from_this<peer_connection>());
	}
	else
	{
		//被动链接是有可能到达这里的
	}
}

void overlay::on_neighbor_maintainer_timer()
{
	GUARD_CLIENT_SERVICE(;);

	//删除那些掉线的
	neighbor_map::iterator itr = neighbors_conn_.begin();
	for (; itr != neighbors_conn_.end();)
	{
		BOOST_AUTO(&conn, itr->second);
		if (!conn || !conn->is_connected() || conn->alive_probability() == 0)
		{
			DEBUG_SCOPE(
				std::cout << "on_neighbor_maintainer_timer erase conn:"
				<< "conn?=" << !!conn
				<< ", is_connected?=" << conn->is_connected()
				<< ", alive_probability?=" << conn->alive_probability()
				<< "\n";
			);
			{PRINT_LINE; conn->close(); }
			neighbor_to_member(conn);//迭代器失效, 不再继续检查了(每次查出一个也不错)
			break;
		}
		else
		{
			++itr;
		}
	}

	on_neighbor_exchange_timer();

	timestamp_t now = timestamp_now();
	//补足连接数
	int neighborCnt = (int)neighbors_conn_.size();
	int pendingCnt = (int)pending_peers_.size();
	int CNT = max_neighbor_cnt_ - (int)(neighborCnt + pendingCnt / 4);
	if (CNT > 0
		&& (CNT > max_neighbor_cnt_ * 2 / 3 + 1 || global_local_to_remote_lost_rate() < 0.15)//有时候链接节点过多会发生上行丢包率极高，所以进行控制
		)
	{
		//如果member过少，释放几个unreachable_members_
		int memberCnt = (int)members_.size();
		if (members_.empty())
		{
			int i = 0;
			while (unreachable_members_.size() && i++ < 3)
			{
				BOOST_AUTO(randitr, random_select(unreachable_members_.begin(), unreachable_members_.size()));
				if (in_probability(0.3))
					members_.insert(*randitr);
				unreachable_members_.erase(randitr);
			}
			if (members_.empty()
				&& CNT > max_neighbor_cnt_ / 2
				&& is_time_passed(50 * 1000, last_member_request_time_, now)
				)
			{
				last_member_request_time_ = now;
				clientService->update_server_info();
			}
		}

		//如果链接已经足够多，则每个定时周期新建连接的个数设置为1, 
		//否则，最多新建3个
		if ((int)neighbors_conn_.size() * 4 > max_neighbor_cnt_)
			CNT = 1;
		else
			CNT = std::min(2, CNT);
		CNT = std::min(CNT, (int)members_.size());
		int cnt = CNT;
		for (int i = 0; i < 3 * CNT; i++)//多次循环随机选择
		{
			peer_map::iterator randItr = random_select(members_.begin(), members_.size());
			peer_sptr p = randItr->second;
			BOOST_ASSERT(p);
			if (neighbors_conn_.find(randItr->first) != neighbors_conn_.end())
			{
				BOOST_ASSERT(0);
			}
			else if (can_be_neighbor(p)//能成为邻居（一般来说需要p上要有locale需要的数据才链接）
				&& is_time_passed(random(10000, 20000), p->last_connect_time(topology_id_), now)
				)
			{
				//std::cout<<neighbors_conn_.size()<<"-------------"<<members_.size()<<std::endl;
				bool nat = false;
				bool sameSubnet = false;
				error_code ec;
				const p2message::peer_info& hisInfo = p->get_peer_info();
				const p2message::peer_info& myInfo = get_client_param_sptr()->local_info;

				if (myInfo.external_ip() == hisInfo.external_ip()
					//&&(hisInfo.internal_ip()&0xffff0000)==(myInfo.internal_ip()&0xffff0000)
					)
				{
					sameSubnet = true;
				}
				else if (hisInfo.nat_type() == NAT_OPEN_OR_FULL_CONE
					//|| (!is_local(address_v4(myInfo.external_ip())) && hisInfo.nat_type() <= NAT_OPEN_OR_FULL_CONE)
					)
				{
					nat = false;
				}
				else
				{
					nat = true;
				}

				if (nat)
				{
					//当对方在NAT后并在防火墙后时，不予链接
					//但有时nat类型探测有误，所以，还是尝试把
					//if (p->get_peer_info().nat_type()==NAT_UDP_BLOCKED)
					//	continue;

					//当邻居节点较少时，不倾向链接NAT后的节点
					if ((int)neighbors_conn_.size() < max_neighbor_cnt_ / 3
						&& i <= 2 * CNT
						)
					{
						continue;
					}
				}
				OVERLAY_DBG(;
				//	std::cout<<"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n";
				//std::cout<<"__async_connect_peer: "<<string_to_hex(p->get_peer_info().peer_id());
				//std::cout<<"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n";
				);
				__async_connect_peer(p.get(), nat, sameSubnet);
				--cnt;
			}
			if (members_.empty() || cnt <= 0)
				break;
		}
	}
	else if (neighborCnt >= max_neighbor_cnt_ && (int)members_.size() >= max_neighbor_cnt_)
	{
		try_shrink_neighbors(false);
	}
}

void overlay::on_neighbor_exchange_timer()
{
	if (neighbors_conn_.empty())
		return;

	timestamp_t now = timestamp_now();
	int t = 1000 * int(members_.size() + neighbors_conn_.size());
	if (!is_time_passed(t, last_exchange_neighbor_time_, now))
		return;

	const tick_type kInterval = NEIGHTBOR_EXCHANGE_INTERVAL.total_milliseconds();
	for (int i = 0; i < 3; i++)
	{
		BOOST_AUTO(itr, random_select(neighbors_conn_.begin(), neighbors_conn_.size()));
		peer_sptr p = itr->second->get_peer();
		if (is_time_passed(kInterval, p->last_neighbor_exchange_time(), now))
		{
			__send_neighbor_exchange_to(itr->second.get());
			last_exchange_neighbor_time_ = now;
			break;
		}
	}
}

void overlay::__send_neighbor_exchange_to(peer_connection* conn)
{
	peer_sptr p = conn->get_peer();
	if (!p)return;

	int m = 25;
	int n = 10;
	int msg_size = 0;
	p2p_neighbor_table_exchange msg;
	for (BOOST_AUTO(itr, members_.begin()); itr != members_.end(); ++itr)
	{
		BOOST_AUTO(thePeer, itr->second);
		const peer_id_t& theID = itr->first;
		if (p->is_known_me_known(theID) == false)
		{
			if (m > 0)
			{
				p->set_known_me_known(theID);
				msg.add_known_neighbor_list(&theID[0], theID.size());
				++msg_size;
			}
		}
		if (p->is_known_peer(theID) == false &&
			theID != peer_id_t(p->get_peer_info().peer_id())
			)
		{
			if (n <= 0)
			{
				continue;
			}
			p->set_known_peer(theID);
			if (p->playing_the_same_channel())
			{
				*(msg.add_same_channel_peer_info_list()) = thePeer->get_peer_info();
				++msg_size;
			}
			else
			{
				*(msg.add_cache_peer_info_list()) = thePeer->get_peer_info();
				++msg_size;
			}
		}
		n--; m--;
		if (n < 0)
			break;
	}

	for (BOOST_AUTO(itr, neighbors_conn_.begin()); itr != neighbors_conn_.end(); ++itr)
	{
		if (!itr->second)
			continue;
		BOOST_AUTO(thePeer, itr->second->get_peer());
		const peer_id_t& theID = itr->first;

		if (p->is_known_me_known(theID) == false)
		{
			if (m > 0)
			{
				p->set_known_me_known(theID);
				msg.add_known_neighbor_list(&theID[0], theID.size());
				++msg_size;
			}
		}
		if (p->is_known_peer(theID) == false &&
			theID != peer_id_t(p->get_peer_info().peer_id())
			)
		{
			if (n <= 0)
			{
				continue;
			}
			p->set_known_peer(theID);
			if (p->playing_the_same_channel())
			{
				*(msg.add_same_channel_peer_info_list()) = thePeer->get_peer_info();
				++msg_size;
			}
			else
			{
				*(msg.add_cache_peer_info_list()) = thePeer->get_peer_info();
				++msg_size;
			}
		}
		n--; m--;
		if (n < 0)
			break;
	}

	if (!msg_size)
		return;

	conn->async_send_reliable(serialize(msg), peer_peer_msg::neighbor_table_exchange);
	p->last_neighbor_exchange_time() = (timestamp_t)(timestamp_now() +
		NEIGHTBOR_EXCHANGE_INTERVAL.total_milliseconds() + random(0, 4000));
}


void overlay::on_recvd_neighbor_table_exchange(peer_connection* conn, 
	safe_buffer buf)
{
	GUARD_CLIENT_SERVICE(;);

	peer_sptr p = conn->get_peer();
	if (!p)return;

	p2p_neighbor_table_exchange msg;
	if (!parser(buf, msg))
		return;
	for (int i = 0; i < msg.known_neighbor_list_size(); ++i)
	{
		peer_id_t peer_id = peer_id_t(msg.known_neighbor_list(i));
		p->set_known_peer(peer_id);
	}
	for (int i = 0; i < msg.same_channel_peer_info_list_size(); ++i)
	{
		clientService->on_known_new_peer(msg.same_channel_peer_info_list(i), true);
	}
	for (int i = 0; i < msg.cache_peer_info_list_size(); ++i)
	{
		clientService->on_known_new_peer(msg.cache_peer_info_list(i), false);
	}

	timestamp_t now = timestamp_now();
	if (is_time_passed(5000, p->last_buffermap_exchange_time(), now))
	{
		__send_neighbor_exchange_to(conn);
	}
}

void overlay::__async_connect_peer(peer* p, bool peerIsNat, bool sameSubnet, 
	peer_connection* conn, int slotOfInternalIP, error_code ec, coroutine coro
	)
{
	struct get_domain {
		std::string operator ()(const std::string& topology_acceptor_domain_base, 
			peer* p, int topology_id)const
		{
			if (p->playing_the_same_channel() || HUB_TOPOLOGY == topology_id)
				return topology_acceptor_domain_base + "/" + p->get_peer_info().peer_id();
			else
				return cache_server_demain + "/" + p->get_peer_info().peer_id();
		}
	};

	GUARD_CLIENT_SERVICE(;);

	endpoint edp;
	std::string domain;
	peer_connection_sptr pendingConn;
	peer_info& info = p->get_peer_info();
	bool fastConn = true;

	//对方可能因主动连接本节点连接上, 总之，只要neighbors_conn_有了就返回
	if (neighbors_conn_.find(peer_id_t(info.peer_id())) != neighbors_conn_.end())
	{
		OVERLAY_DBG(if (conn)post_active_conn_cnt--;);
		return;
	}
	OVERLAY_DBG(BOOST_ASSERT(post_active_conn_cnt >= 0 && post_active_conn_cnt<50));

	CORO_REENTER(coro)
	{
		BOOST_ASSERT(0 == (int)coroutine_ref(coro));
		edp = external_udp_endpoint(info);
		if (g_b_streaming_using_rudp
			&& (!p->is_udp_restricte() || in_probability(0.1))
			&& (is_any(edp.address()) || edp.port() == 0 || p->is_rtt_accurate() || in_probability(0.8))
			)
		{
			if (!peerIsNat || sameSubnet)
			{
				//如果有other_internal_ip，则首先尝试other_internal_ip，
				//因为这些IP中有可能有公网地址。(other_internal_ip并非只存内网地址)
				if (info.other_internal_ip_size() > 0)
				{
					slotOfInternalIP = 0;
					edp.port(info.internal_udp_port());
				}
				else if (info.internal_ip())//没有other_internal_ip，则使用internal_ip
				{
					slotOfInternalIP = info.other_internal_ip_size() + 1;
					edp.port(info.internal_udp_port());
				}

				if (edp.port() == 0)//只能尝试公网地址了
				{
					slotOfInternalIP = info.other_internal_ip_size() + 2;
					edp.port(info.external_udp_port());
					fastConn = false;
				}

				if (edp.port() != 0)
				{
					for (; slotOfInternalIP <= info.other_internal_ip_size() + 3; ++slotOfInternalIP)
					{
						fastConn = true;
						if (slotOfInternalIP < info.other_internal_ip_size())
						{
							edp.port(info.internal_udp_port());
							edp.address(address_v4(info.other_internal_ip(slotOfInternalIP)));
						}
						else if (slotOfInternalIP == info.other_internal_ip_size() + 1)
						{
							edp = internal_udp_endpoint(info);
						}
						else if (slotOfInternalIP == info.other_internal_ip_size() + 2)
						{
							edp = external_udp_endpoint(info);
						}
						else //(slotOfInternalIP==info.other_internal_ip_size()+3)
						{
							if (info.external_udp_port() != info.internal_udp_port()
								&& info.external_ip() != info.internal_ip()
								)
							{
								edp.port(info.internal_udp_port());
								edp.address(address_v4(info.external_ip()));
							}
							else
							{
								continue;
							}
						}
						if (!is_global(edp.address()))
						{
							continue;
						}

						if (edp.port() == 0)//只能尝试公网地址了
						{
							edp.port(info.external_udp_port());
							fastConn = false;
						}

						pendingConn = urdp_peer_connection::create(get_io_service(), true);
						pendingConn->open(udp_local_endpoint_, ec);//尽量共用一个udp
						pendingConn->set_peer(p->shared_obj_from_this<peer>());
						pendingConn->ping_interval(STREAM_PEER_PEER_PING_INTERVAL);
						member_to_pending(pendingConn);
						OVERLAY_DBG(
						{
							std::cout << "-----DIRECT---外网节点(" << string_to_hex(info.peer_id()) << ") : " << edp
							<< ", (" << info.internal_udp_port() << ", " << info.external_udp_port() << ") "
							<< slotOfInternalIP << std::endl;
						}
						);
						OVERLAY_DBG(++post_active_conn_cnt;)
						CORO_YIELD(
							pendingConn->register_connected_handler(boost::bind(
							&this_type::__async_connect_peer, this, 
							p, peerIsNat, sameSubnet, pendingConn.get(), 
							slotOfInternalIP, 
							_1, coro
							));
						pendingConn->async_connect(edp, 
							get_domain()(topology_acceptor_domain_base_, p, topology_id_), 
							seconds(fastConn ? 5 : 10)
							);  
						);
						OVERLAY_DBG(--post_active_conn_cnt;)
						if (!ec)
						{
							on_connected(conn, ec);
							return;
						}
						else
						{

						}
					}
				}
			}

			//////////////////////////////////////////////////////////////////////////
			//尝试NAT穿越
			//向节点发送链接请求
			if (conn)
			{
				{conn->close(); }
			}
			edp = external_udp_endpoint(info);
			if (is_any(edp.address()))
				edp = internal_udp_endpoint(info);
			if (!edp.port())
				edp.port(info.internal_udp_port());
			if (!edp.port() && is_global(edp.address()))
				edp.port(info.internal_tcp_port());
			if (edp.port())
			{
				//boost::shared_ptr<tracker_manager>  tracker=clientService->get_tracker_handler();
				pendingConn = urdp_peer_connection::create(get_io_service(), true);
				pendingConn->open(udp_local_endpoint_, ec);//尽量共用一个udp
				pendingConn->set_peer(p->shared_obj_from_this<peer>());
				safe_buffer punchData = pendingConn->make_punch_packet(ec, external_udp_endpoint(get_client_param_sptr()->local_info));
				pendingConn->ping_interval(STREAM_PEER_PEER_PING_INTERVAL);

				//同时，要向tracker请求中继穿越包
				punch_request_msg punchRequestMsg;
				punchRequestMsg.set_msg_data(std::string(buffer_cast<char*>(punchData), punchData.length()));
				punchRequestMsg.set_ip(get_client_param_sptr()->local_info.external_ip());
				punchRequestMsg.set_port(get_client_param_sptr()->local_info.external_udp_port());
				safe_buffer punchRequest = serialize(peer_peer_msg::punch_request, punchRequestMsg);
				relay_msg punchRelayMsg;
				punchRelayMsg.set_dst_peer_id(info.peer_id());
				punchRelayMsg.set_level(SYSTEM_LEVEL);
				punchRelayMsg.set_msg_data(std::string(buffer_cast<char*>(punchRequest), punchRequest.length()));
				punchRelayMsg.set_msg_id(clientService->generate_uuid());
				punchRelayMsg.set_src_peer_id(get_client_param_sptr()->local_info.peer_id());
				punchRelayMsg.set_ttl(4);
				clientService->get_tracker_handler()->async_send_reliable(serialize(punchRelayMsg), global_msg::relay);
			}

			if (pendingConn)
			{
				OVERLAY_DBG(
					std::cout << "------NAT------外网节点(" << string_to_hex(info.peer_id()) << "):" << edp << std::endl;
				);
				member_to_pending(pendingConn);
				OVERLAY_DBG(++post_active_conn_cnt;)
				CORO_YIELD(
					pendingConn->register_connected_handler(boost::bind(
					&this_type::__async_connect_peer, this, 
					p, peerIsNat, sameSubnet, pendingConn.get(), 
					slotOfInternalIP, 
					_1, coro));
				pendingConn->async_connect(edp, get_domain()(topology_acceptor_domain_base_, p, topology_id_), seconds(10));
				);
				OVERLAY_DBG(--post_active_conn_cnt;)
				if (!ec)
				{
					on_connected(conn, ec);
					return;
				}
				else
				{
					BOOST_ASSERT(conn);
					OVERLAY_DBG(
						if (conn)
						{
						error_code temperr;
						std::cout << "topybase __async_connect_peer ip:" << conn->remote_endpoint(temperr) << "error" << ec.message() << std::endl;
						}
					);
				}
			}
		}

		//////////////////////////////////////////////////////////////////////////
		//只好尝试tcp
		if (conn)
		{
			conn->close();
		}
		edp = external_tcp_endpoint(info);
		if (edp.port() == 0)
			edp.port(info.external_udp_port());
		if (/*!peerIsNat&&*/edp.port() != 0 && !is_any(edp.address()))
		{
			OVERLAY_DBG(
				std::cout << "------TCP------外网节点(" << string_to_hex(info.peer_id()) << "):" << edp << std::endl;
			);
			pendingConn = trdp_peer_connection::create(get_io_service(), true);
			//pendingConn->open(local_endpoint_, ec);//tcp不绑定某一固定的edp
			pendingConn->set_peer(p->shared_obj_from_this<peer>());
			pendingConn->ping_interval(STREAM_PEER_PEER_PING_INTERVAL);
			member_to_pending(pendingConn);
			OVERLAY_DBG(++post_active_conn_cnt;)
			CORO_YIELD(
				pendingConn->register_connected_handler(boost::bind(&this_type::__async_connect_peer, this, 
				p, peerIsNat, sameSubnet, pendingConn.get(), slotOfInternalIP, _1, coro));
			pendingConn->async_connect(edp, get_domain()(topology_acceptor_domain_base_, p, topology_id_), seconds(5));
			);
			OVERLAY_DBG(--post_active_conn_cnt;)
			if (ec)
			{
				p->set_udp_restricte(false);
				low_capacity_peer_keeper_.try_keep(peer_id_t(info.peer_id()), seconds(5));
			}

			on_connected(conn, ec);
		}
		else
		{
			pending_to_member(p->shared_obj_from_this<peer>());
			low_capacity_peer_keeper_.try_keep(peer_id_t(info.peer_id()), seconds(5));
		}
	}
}

void overlay::print_neighbors()
{
	error_code ec;
	if (members_.size() < 10)
	{
		std::cout << "--------*--------members-------*---------[\n";
		peer_map::iterator itr = members_.begin();
		for (; itr != members_.end(); ++itr)
		{
			std::cout
				//<<"|"<<itr->second->get_peer_info().peer_id()
				<< ": " << internal_udp_endpoint(itr->second->get_peer_info())
				<< ": " << external_udp_endpoint(itr->second->get_peer_info())
				<< "\n";
		}
		std::cout << "--------*--------members-------*---------]\n";

	}
	if (unreachable_members_.size())
	{
		std::cout << "--------*--------unreachable_members-------*---------[\n";
		BOOST_AUTO(itr, unreachable_members_.begin());
		for (; itr != unreachable_members_.end(); ++itr)
		{
			std::cout
				//<<"|"<<itr->second->get_peer_info().peer_id()
				<< ": " << internal_udp_endpoint(itr->second->get_peer_info())
				<< "\n";
		}
		std::cout << "--------*--------unreachable_members-------*---------]\n";

	}
	if (pending_peers_.size())
	{
		std::cout << "--------*--------pending_peers count:" << pending_peers_.size() << "\n";
		/*
		std::cout<<"--------*--------pending_peers-------*---------[\n";
		BOOST_AUTO(itr, pending_peers_.begin());
		for (;itr!=pending_peers_.end();++itr)
		{
		std::cout
		<<"|"<<itr->second->get_peer_info().peer_id()
		<<": "<<internal_udp_endpoint(itr->second->get_peer_info())
		<<"\n";
		}
		std::cout<<"--------*--------pending_peers-------*---------]\n";
		*/
	}

	std::cout << "--------*--------neighbors-------*---------[\n";
	for (BOOST_AUTO(itr, neighbors_conn_.begin()); itr != neighbors_conn_.end(); ++itr)
	{
		struct to_precision{
			to_precision(int x)
			{
				precision_ = x * 10;
			}
			double operator()(double d)const
			{
				return double((int)(d*precision_ + 0.5)) / precision_;
			}
			int precision_;
		};
		std::cout
			<< itr->second->remote_endpoint(ec)
			<< ", alive:" << to_precision(2)(itr->second->alive_probability())
			<< ", dwnLstR:" << to_precision(2)(itr->second->remote_to_local_lost_rate())
			<< ", upLstR:" << to_precision(2)(itr->second->local_to_remote_lost_rate())
			<< ", dwn:" << to_precision(1)(itr->second->get_peer()->upload_to_local_speed())
			<< ", up:" << to_precision(1)(itr->second->get_peer()->download_from_local_speed())
			<< ", wnd:" << to_precision(1)(itr->second->get_peer()->tast_count() + itr->second->get_peer()->residual_tast_count())
			<< (itr->second->connection_category() == message_socket::TCP ? " TCP" : " UDP")
			<< "\n";
	}
	std::cout << "--------*--------neighbors-------*---------]\n";
}

bool overlay::to_neighbor(const peer_connection_sptr& conn, int maxNbrCnt)
{
	BOOST_ASSERT(conn);
	BOOST_ASSERT(conn->get_peer());
	BOOST_ASSERT(conn->is_connected());

	GUARD_CLIENT_SERVICE(false);

	peer_sptr p = conn->get_peer();
	peer_id_t id(p->get_peer_info().peer_id());

	//假如节点已经掉线，删除。
	//if (clientService->is_offline(id))
	if (!conn->is_connected())
	{
		//WARNING：假如节点已经掉线，则不应该链接成功，所以，这段代码不会被执行
		BOOST_ASSERT(0 && "有个neighbor可能掉线了");
		erase_offline_peer(id);
		return false;
	}

	//成功链接，所以，不是unreachable
	p->mark_reachable(topology_id_);
	unreachable_members_.erase(id);
	members_.erase(id);
	pending_peers_.erase(id);

	neighbor_map::iterator itr(neighbors_conn_.find(id));
	if (itr != neighbors_conn_.end())
	{
		peer_connection_sptr oldConn = itr->second;
		if (oldConn == conn)
		{
			BOOST_ASSERT(0);
			return false;
		}

		if (!oldConn || !oldConn->is_connected() || oldConn->alive_probability() == 0)
		{
			{PRINT_LINE; oldConn->close(); }
			neighbors_conn_.erase(itr);
			return __insert_to_neighbors(id, conn);
		}
		if (!conn || !conn->is_connected() || conn->alive_probability() == 0)
		{
			BOOST_ASSERT(0);
			{PRINT_LINE; conn->close(); }
			return false;
		}

		//在产生链接冲突时，关闭session小的连接
		if (conn->session_id() < oldConn->session_id()
			&& oldConn&&oldConn->is_connected() && oldConn->alive_probability() > 0
			)
		{
			{PRINT_LINE; conn->close(); }
			return false;
		}
		else
		{
			{PRINT_LINE; oldConn->close(); }
			neighbors_conn_.erase(itr);
			return __insert_to_neighbors(id, conn);
		}
	}
	else if ((int)neighbors_conn_.size() >= maxNbrCnt)//超出了连接允许量，尝试缩减
	{
		try_shrink_neighbors(false);
	}

	BOOST_ASSERT(neighbors_conn_.find(id) == neighbors_conn_.end());
	BOOST_ASSERT(members_.find(id) == members_.end());

	if ((int)neighbors_conn_.size() >= maxNbrCnt)//超出了连接允许量，还原为member
	{
		{PRINT_LINE; conn->close(); }
		__insert_to_members(id, p);
		return false;
	}
	else//由member转为neighbor
	{
		__insert_to_neighbors(id, conn);
		return true;
	}
}

bool overlay::member_to_pending(const peer_connection_sptr& conn)
{
	GUARD_CLIENT_SERVICE(false);

	BOOST_ASSERT(conn);
	peer_sptr p = conn->get_peer();
	BOOST_ASSERT(p);
	peer_id_t id(p->get_peer_info().peer_id());
	members_.erase(id);
	if (neighbors_conn_.find(id) == neighbors_conn_.end())
	{
		BOOST_ASSERT(pending_active_sockets_.find(conn) == pending_active_sockets_.end());
		keep_pending_active_sockets(conn);
		p->last_connect_time(topology_id_) = timestamp_now();
		pending_peers_.erase(id);
		bool rst = pending_peers_.try_keep(std::make_pair(id, p), seconds(60));
		BOOST_ASSERT(rst);
		return true;
	}
	else
	{
		BOOST_ASSERT(0);
		return false;
	}
}

bool overlay::pending_to_member(const peer_sptr& p)
{
	BOOST_ASSERT(p);

	GUARD_CLIENT_SERVICE(false);

	peer_id_t id(p->get_peer_info().peer_id());
	//BOOST_ASSERT(members_.find(id) == members_.end());
	pending_peers_.erase(id);
	if (clientService->is_offline(id))
	{//假如节点已经掉线，删除
		erase_offline_peer(id);
		return false;
	}
	if (neighbors_conn_.find(id) == neighbors_conn_.end())
	{
		if (p->mark_unreachable(topology_id_) >= 3)//未能链接成功累计超过2次，插入到unreachable中
		{
			__insert_to_unreachable_members(id, p);
			members_.erase(id);
			//在非交互式直播中认为此节点掉线
			if (!is_interactive_category(get_client_param_sptr()->type))
				clientService->on_known_offline_peer(id);
		}
		else
		{
			if (unreachable_members_.find(id) == unreachable_members_.end())
				return __insert_to_members(id, p);
			else
				BOOST_ASSERT(0);
		}
	}
	else//如果neighbor中有这个节点，则还是应该按neighbor处理
	{
		members_.erase(id);
		unreachable_members_.erase(id);
		p->mark_reachable(topology_id_);
	}
	return false;
}

bool overlay::neighbor_to_member(const peer_connection_sptr& conn)
{
	GUARD_CLIENT_SERVICE(false);

	BOOST_ASSERT(conn);
	peer_sptr p = conn->get_peer();
	BOOST_ASSERT(p);
	peer_id_t id(p->get_peer_info().peer_id());

	//曾经是neighbor，所以是可达的
	p->mark_reachable(topology_id_);

	//假如节点已经掉线，删除
	if (clientService->is_offline(id))
	{
		scheduling_->neighbor_erased(id);
		erase_offline_peer(id);
		return false;
	}

	neighbor_map::iterator itr = neighbors_conn_.find(id);
	if (itr != neighbors_conn_.end())
	{
		neighbors_conn_.erase(id);
		scheduling_->neighbor_erased(id);
	}
	unreachable_members_.erase(id);
	pending_peers_.erase(id);
	return __insert_to_members(id, p);
}

void overlay::erase_offline_peer(const peer_id_t& id)
{
	neighbor_map::iterator itr = neighbors_conn_.find(id);
	if (itr != neighbors_conn_.end())
	{
		neighbors_conn_.erase(id);
		scheduling_->neighbor_erased(id);
	}
	members_.erase(id);
	pending_peers_.erase(id);
	unreachable_members_.erase(id);
}

void overlay::erase_low_capacity_peer(const peer_id_t& id)
{
	members_.erase(id);
	pending_peers_.erase(id);
	unreachable_members_.erase(id);
	neighbor_map::iterator itr = neighbors_conn_.find(id);
	if (itr != neighbors_conn_.end())
	{
		neighbors_conn_.erase(id);
		scheduling_->neighbor_erased(id);
	}
}

void overlay::erase_route(const peer_id_t& id, const peer_id_t& routeID)
{
	router_table::iterator itr = router_table_.find(id);
	if (itr != router_table_.end())
	{
		itr->second.erase(routeID);
		if (itr->second.empty())
			router_table_.erase(itr);
	}
}

//tracker告诉topology有新的合作节点
void overlay::known_new_peer(const peer_id_t& id, peer_sptr p)
{
	if (is_black_peer(id))
		return;
	BOOST_ASSERT(p);
	if (pending_peers_.find(id) == pending_peers_.end()
		&& unreachable_members_.find(id) == unreachable_members_.end()
		&& neighbors_conn_.find(id) == neighbors_conn_.end()
		)
	{
		__insert_to_members(id, p);
	}
}


NAMESPACE_END(p2client);
