#include "tracker/cache_table.h"
#include "tracker/cache_service.h"

using namespace p2tracker;

typedef cache_table::peer peer;

void cache_table::register_message_handler(peer_connection* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(tracker_peer_msg::peer_request, 	on_recvd_cache_peer_request);
	REGISTER_HANDLER(tracker_peer_msg::logout, 			on_recvd_logout);
	REGISTER_HANDLER(tracker_peer_msg::failure_report, 	on_recvd_failure_report);
	REGISTER_HANDLER(global_msg::relay, 					on_recvd_relay);
}

cache_table::cache_table(boost::shared_ptr<cache_service>svc, 
						 const std::string& channelID
						 )
						 : cache_service_(svc)
						 , basic_tracker_object(svc->get_tracker_param_sptr())
						 , channel_uuid_(channelID)
{
}

cache_table::~cache_table()
{
	peers_.clear();
	server_set_.clear();
}

//点播过程中，拖放等交互操作导致的更新
const peer* cache_table::updata(peer_set& peers, peer_sptr conn, int health)
{
	peer_id_t id(conn->get_peer_info().peer_id());
	id_index_type::iterator itr=id_index(peers).find(id);

	//same id, diffrent socket
	if (itr!=id_index(peers).end())
	{
		id_index(peers).erase(itr);//一定要删除再插入，否则updata与insert会循环调用
		return insert(peers, conn, health);
	}

	return NULL;
}

//如果插入成功，返回peer*；否则，返回NULL
const peer* cache_table::insert(peer_sptr conn, int healthy)
{
	if (is_assist_server(conn))
		return insert(server_set_, conn, healthy);

	return insert(peers_, conn, healthy);
}

const peer* cache_table::insert(peer_set& peers, peer_sptr conn, int healthy)
{
	error_code ec;
	endpoint edp = conn->remote_endpoint(ec);
	if (ec) 
		return NULL;

	peer_sptr sock = conn->shared_obj_from_this<peer_connection>();
	if (!sock) 
		return NULL;

	peer p;
	set_peer_info(&p, conn, healthy);
	if (socket_index(peers_).find(sock) == socket_index(peers_).end())
	{
		BOOST_AUTO(insertRst, socket_index(peers).insert(p));
		if (insertRst.second==true)
		{
			////插入成功，绑定断开后的操作
			register_message_handler(conn.get());
			conn->register_disconnected_handler(boost::bind(
				&this_type::erase, this, conn.get(), _1
				));

			DEBUG_SCOPE(
				std::cout<<"cache channel "<<channel_id()
				<<" has "
				<<peers.size()<<" peers"
				<<std::endl;
			);

			return &(*(insertRst.first));
		}
	}


	return updata(peers, conn, healthy);
}

void cache_table::find_peer_for(const peer& target, const peer_set& peers, 
								std::vector<const peer*>& returnVec, size_t maxReturnCnt)
{
	if (ip_index(peers).size() <= maxReturnCnt)
	{
		find_peer_for(target, peers, returnVec);
	}
	else
	{
		sets_.clear();
		returnVec.resize(maxReturnCnt);

		//首先返回maxReturnCnt/3 health最高的节点
		size_t i = find_peer_by_max_health(
			target, peers, returnVec, maxReturnCnt
			);

		//搜索maxReturnCnt/3个与节点的IP最近的节点（这会以较大概率返回位于相同子网的节点）
		//当然，如果使用"网络坐标来"选择节点，效果应该更好。
		size_t nearCnt = (maxReturnCnt - i)/2;
		ip_index_type::const_iterator itrRight=ip_index(peers).lower_bound(target.m_external_ipport);
		ip_index_type::const_iterator itrLeft=itrRight;
		for (;itrRight!=ip_index(peers).end()&&i<nearCnt/2;++itrRight)
		{
			if (!is_same_edp(target, (const peer&)*itrRight) && insert(*itrRight))
				returnVec[i++]=&(*itrRight);
		}
		if (itrLeft!=ip_index(peers).begin()&&((--itrLeft)!=ip_index(peers).begin()))
		{
			for (;i<nearCnt;)
			{
				if (!is_same_edp(target, *itrLeft) && insert(*itrLeft))
					returnVec[i++]=&(*itrLeft);
				if (itrLeft==ip_index(peers).begin())
					break;
				--itrLeft;
			}
		}
		//然后搜索maxReturnCnt/2个随机节点，避免信息孤岛的产生
		//TODO：这在节点较多时候操作较费时
		double p = generate_probability(peers, maxReturnCnt, i);
		BOOST_AUTO(itr, ip_index(peers).begin());
		for (;itr!=itrLeft&&i<maxReturnCnt;++itr)
		{
			if (!is_same_edp(target, *itr)
				&&in_probability(p)
				&&insert(*itr)
				)
				returnVec[i++]=&(*itr);
		}
		for (itr=itrRight;ip_index(peers).end()!=itr&&i<maxReturnCnt;++itr)
		{
			if (!is_same_edp(target, *itr)
				&&in_probability(p)
				&&insert(*itr)
				)
				returnVec[i++]=&(*itr);
		}
		returnVec.resize(std::min(i, maxReturnCnt));
	}
}

void cache_table::find_assist_server_for(
	const peer& target, std::vector<const peer*>& returnVec, 
	size_t max_return_cnt/* = 2*/)
{
	tick_type now = tick_now();/*(ptime_now()-min_time()).total_milliseconds();*/
	tick_type duration_time = 0; //服务器加入持续时间

	ip_index_type::iterator itr = ip_index(server_set_).begin();
	for (;itr != ip_index(server_set_).end(); ++itr)
	{
		if(is_same_edp(target, *itr))
			continue;

		duration_time = peer_duration_time(*itr, now);
		if (duration_time < channel_info_.film_duration()) 
			continue;

		//完成了整个影片的缓冲, 服务器设置充盈度大于100
		const_cast<peer&>(*itr).m_healthy =static_cast<uint8_t>(100*random(1.05, 1.1));
	}
	find_peer_for(target, server_set_, returnVec, max_return_cnt);
}

void cache_table::find_peer_for(const peer& target, 
								std::vector<const peer*>& returnVec, size_t maxReturnCnt)
{
	find_peer_for(target, peers_, returnVec, maxReturnCnt);
}

const peer* cache_table::find(peer_sptr conn)
{
	peer_sptr sock = conn->shared_obj_from_this<peer_connection>();
	if (!sock) 
		return NULL;

	socket_index_type::iterator itr=socket_index(peers_).find(sock);
	if (itr!=socket_index(peers_).end())
	{
		peer& p = const_cast<peer&>(*itr);
		return &p;
	}
	return NULL;
}

const peer* cache_table::find(const peer_id_t& id)
{
	id_index_type::iterator itr=id_index(peers_).find(id);
	if (itr != id_index(peers_).end())
		return &(*itr);

	return NULL;
}

void cache_table::__erase(peer_set& peers, 
						 peer_connection* conn, 
						 error_code ec)
{
	OBJ_PROTECTOR(holdThis);
	peer_sptr sock = conn->shared_obj_from_this<peer_connection>();
	if (!sock) 
		return;

	BOOST_AUTO(itr, socket_index(peers).find(sock));
	if (itr!=socket_index(peers).end())
	{
		socket_index(peers).erase(itr);
		if ((size()==0) && 
			(NORMAL_PEER == sock->get_peer_info().peer_type()))
		{
			boost::shared_ptr<cache_service> ptr=cache_service_.lock();
			if (ptr)
			{
				DEBUG_SCOPE(
					std::cout<<"channel: "<<channel_uuid_
					<<" has no peer, erase it"
					<<std::endl;
				)
					ptr->erase(channel_uuid_);
			}
		}
		DEBUG_SCOPE(
			std::cout<<" cache table: peer erased, peer remain"<<size()
			<<std::endl;
		)
	}
}

void cache_table::erase(peer_connection* conn, error_code ec)
{
	peer_sptr sock = conn->shared_obj_from_this<peer_connection>();
	if (!sock) 
		return;

	if (ASSIST_SERVER == sock->get_peer_info().peer_type())
	{
		__erase(server_set_, conn, ec);
	}
	else
	{
		__erase(peers_, conn, ec);
	}

}

void cache_table::set_channel_info(const vod_channel_info& channel_info)
{
	channel_info_.CopyFrom(channel_info);
}

void cache_table::find_peer_for(const peer& target, 
								const peer_set& peers, std::vector<const peer*>& returnVec)
{
	returnVec.reserve(ip_index(peers).size());
	ip_index_type::iterator itr = ip_index(peers).begin();
	for (; itr != ip_index(peers).end(); ++itr)
	{
		if (!is_same_edp(target, *itr))
			returnVec.push_back(&(*itr));
	}
}

int cache_table::find_peer_by_max_health(const peer& target, 
										 const peer_set& peers, 
										 std::vector<const peer*>& returnVec, 
										 size_t maxReturnCnt)
{
	double p=(double)(maxReturnCnt)/std::min((double)(ip_index(peers).size()), 100.0);
	BOOST_AUTO(itr, healthy_index(peers).rbegin());
	BOOST_AUTO(rend, healthy_index(peers).rend());

	size_t i = 0;
	for (;itr!=rend && i <maxReturnCnt/3;++itr)
	{
		if (is_same_edp(target, *itr))
			continue;

		if (is_server(target) //服务器
			||in_probability(p)
			)
		{
			returnVec[i++]=&(*itr);
			insert(*itr);
		}
	}
	return i;
}

bool cache_table::is_assist_server(peer_sptr conn)
{
	if (ASSIST_SERVER == conn->get_peer_info().peer_type())
		return true;

	return false;
}

bool cache_table::is_server(const peer& p)
{
	return p.m_healthy > 100;
}

void cache_table::set_peer_info(peer* p, 
								peer_sptr conn, 
								const int healthy)
{
	error_code ec;
	endpoint edp = conn->remote_endpoint(ec);
	if (ec) 
		return;

	p->m_id = conn->get_peer_info().peer_id();
	p->m_external_ipport.ip = edp.address().to_v4().to_ulong();
	p->m_external_ipport.port = edp.port();
	p->m_socket = conn;
	p->m_healthy = healthy;

	p->m_socket->get_peer_info().set_external_ip(
		p->m_external_ipport.ip
		);
	if(conn->connection_category()==message_socket::UDP)
		p->m_socket->get_peer_info().set_external_udp_port(
		p->m_external_ipport.port
		);
	else
		p->m_socket->get_peer_info().set_external_tcp_port(
		p->m_external_ipport.port
		);

	tick_type now = tick_now();
	p->m_socket->get_peer_info().set_join_time(now);
}

bool cache_table::is_same_edp(const peer& target, 
							 const peer& candidate)
{
#ifndef _DEBUG
	if(candidate.m_external_ipport == target.m_external_ipport)
		return true;
#endif

	return false;
}

bool cache_table::insert(const peer& p)
{
	return sets_.insert(p.m_external_ipport.to_uint64()).second;
}

double cache_table::generate_probability(peer_set peers, 
										 size_t max_return_cnt, size_t already_pick_cnt)
{
	double p = (double)(max_return_cnt - already_pick_cnt + 1)/
		std::min((double)(ip_index(peers).size()-1), 50.0);
	return p < 0.02 ? 0.02 : p;
}

boost::uint64_t cache_table::peer_duration_time(
	const peer& target, const boost::uint64_t& now)
{
	boost::uint64_t duration_time = 0; //服务器加入持续时间
	return now - target.m_socket->get_peer_info().join_time() 
		+ target.m_socket->get_peer_info().relative_playing_point();
}

void cache_table::on_recvd_cache_peer_request(
	peer_connection*sock, const safe_buffer& buf)
{
	p2ts_peer_request_msg msg;
	if (!parser(buf, msg)) 
		return;

	reply_peers(sock, msg);
}

void cache_table::on_recvd_failure_report(
	peer_connection* conn, const safe_buffer& buf)
{
	p2ts_failure_report_msg msg;
	if (!parser(buf, msg)) 
		return;

	//掉线的节点的ID	
	const peer* p = find(peer_id_t(msg.peer_id()));
	if(!p) 
		return;

	error_code ec;
	p->m_socket->ping(ec);//向其发送一个ping，以探测器在线情况
}

void cache_table::on_recvd_relay(
								 peer_connection* sock, const safe_buffer& buf)
{
	relay_msg msg;
	if (!parser(buf, msg)
		|| !relay_msg_keeper_.try_keep(msg.msg_id(), seconds(60))
		|| msg.ttl() < 1
		)
	{
		return;
	}
	//目标节点与源节点是否存在
	const peer* dstp = find(peer_id_t(msg.dst_peer_id()));
	if (!dstp) 
		return;

	peer* srcp = const_cast<peer*>(
		find(sock->shared_obj_from_this<peer_connection>())
		);
	if(!srcp) 
		return;

	//频繁的要求中继报文是不允许的
	if(!keep_relay_msg(msg.msg_id(), srcp)) 
		return;

	__relay_msg(msg, dstp);
}

void cache_table::on_recvd_logout(peer_connection* conn, 
								 const safe_buffer& buf)
{
	p2ts_logout_msg msg;
	if (!parser(buf, msg))
		return;

	erase(conn);
}

void cache_table::reply_peers(peer_connection* sock, 
							 p2ts_peer_request_msg& msg)
{	
	if(!sock || !sock->is_connected())
		return;

	ts2p_peer_reply_msg repmsg;
	repmsg.set_session_id(msg.session_id());
	repmsg.set_error_code(e_no_error);
	repmsg.set_channel_id(channel_id());
	repmsg.set_cache_peer_cnt(size());

	//首先放入server信息, 然后增加其它节点的信息
	//peer_info* pinfo = repmsg.add_peer_info_list();
	//*pinfo = server_info();

	peer p;
	set_socket_info_to(&p, sock);

	find_member_for(p, repmsg);
	//发送消息
	sock->async_send_reliable(
		serialize(repmsg), 
		tracker_peer_msg::peer_reply
		);
}

void cache_table::set_socket_info_to(peer* p, 
									 peer_connection* sock)
{
	error_code ec;
	endpoint edp = sock->remote_endpoint(ec);

	p->m_external_ipport.ip = edp.address().to_v4().to_ulong();
	p->m_external_ipport.port = edp.port();
	p->m_socket = sock->shared_obj_from_this<peer_connection>();
}

template<typename msg_type>
void cache_table::find_member_for(const cache_table::peer& target, 
								 msg_type& msg)
{
	std::vector<const peer*> returnVec;
	//从助服务器中选择一些返回给登录的节点
	find_assist_server_for(target, returnVec);
	add_peer_info(msg, returnVec);

	//从节点列表中选择一些节点准备返回给登录节点
	returnVec.clear();
	find_peer_for(target, returnVec);
	add_peer_info(msg, returnVec);
}

template<typename msg_type>
void cache_table::add_peer_info(msg_type& msg, 
								std::vector<const peer*>& returnVec)
{
	for (size_t i=0; i<returnVec.size(); ++i)
	{
		peer_info* pinfo = msg.add_peer_info_list();
		const peer_info& peerInfo=returnVec[i]->m_socket->get_peer_info();
		*(pinfo) =peerInfo;
	}
}

void cache_table::login_reply(peer_sptr conn, p2ts_login_msg& msg, 
	const cache_table::peer* new_peer, bool findMember)
{
	ts2p_login_reply_msg repmsg;
	repmsg.set_error_code(e_no_error);
	repmsg.set_session_id(msg.session_id());
	repmsg.set_join_time(static_cast<tick_type>(tick_now()));
	repmsg.set_online_peer_cnt((int)size());
	repmsg.set_channel_id(channel_id());
	repmsg.set_external_ip(new_peer->m_external_ipport.ip);
	repmsg.set_external_port(new_peer->m_external_ipport.port);
	if (findMember)//只有要求查找合作节点时候才查找
		find_member_for(*new_peer, repmsg);

	conn->async_send_reliable(serialize(repmsg), tracker_peer_msg::login_reply);

	conn->ping_interval(get_tracker_param_sptr()->tracker_peer_ping_interval);
}

bool cache_table::keep_relay_msg(uint64_t msg_id, peer* p)
{
	p->m_msg_relay_keeper.try_keep(msg_id, seconds(20));
	if(p->m_msg_relay_keeper.size()>40) 
		return false;

	return true;
}

void cache_table::__relay_msg(relay_msg& msg, const peer* p)
{
	//调整消息的ttl
	msg.set_ttl(msg.ttl()-1);

	//发送消息
	p->m_socket->async_send_reliable(serialize(msg), global_msg::relay);
}