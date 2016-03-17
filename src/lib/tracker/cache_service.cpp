#include "tracker/cache_service.h"
#include "tracker/tracker_service.h"
#include "tracker/tracker_service_logic.h"
#include "common/policy.h"

using namespace p2tracker;
using namespace p2common;

//TODO("消息处理放到cachetable中, 避免在cache_service中查找table");

//网络消息处理函数注册
void cache_service::register_message_handler(peer_connection* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(tracker_peer_msg::cache_announce, on_recvd_announce_cache);
	REGISTER_HANDLER(tracker_peer_msg::login,   on_recvd_login);
}

cache_service::cache_service(io_service& net_svc, tracker_param_sptr param)
	:basic_engine_object(net_svc)
	, basic_tracker_object(param)
{
	set_obj_desc("cache_service");
}

cache_service::~cache_service()
{
	stop();
}

//等待来自peer的连接
void cache_service::start(boost::shared_ptr<tracker_service_logic_base>tsLogic)
{
	tracker_service_logic_ = tsLogic;

	endpoint edp = endpoint_from_string<endpoint>(
		get_tracker_param_sptr()->internal_ipport
		);
	error_code ec;
	start_acceptor(edp, cache_tracker_demain, ec);
}

void cache_service::stop()
{
	close_acceptor();
	cache_tables_.clear();
}

void cache_service::erase(const std::string&channelID)
{
	cache_tables_.erase(channelID);
	DEBUG_SCOPE(
		std::cout<<get_obj_desc()<<" channel id: "<<channelID
		<<" after erase channel count: "<<cache_tables_.size()
		<<std::endl;
	)
}

//网络事件处理
void cache_service::on_accepted(peer_sptr conn, const error_code& ec)
{
	if (ec)
		return;

	//将这一链接暂存起来，如果暂存超时还没有收到合法报文，则关闭连接。
	pending_sockets_.try_keep(conn, seconds(10));

	register_message_handler(conn.get());
	conn->ping_interval(get_tracker_param_sptr()->tracker_peer_ping_interval);
	conn->keep_async_receiving();

	//发送challenge
	send_challenge_msg(conn);
}

void cache_service::on_recvd_announce_cache(peer_connection*sock, const safe_buffer& buf)
{
	p2ts_cache_announce_msg msg;
	if (!parser(buf, msg))
		return;

	DEBUG_SCOPE(
		std::cout<<"cached channel recvd from: "
		<<string_to_hex(msg.peer_info().peer_id())
		<<std::endl;
	);
	peer_sptr conn(sock->shared_obj_from_this<peer_connection>());
	peer_info& peerInfo = conn->get_peer_info();
	peerInfo = msg.peer_info();//更新peerinfo

	set_peer_info(conn, peerInfo);
	recvd_cached_info(msg, conn);
}
void cache_service::set_peer_info(peer_sptr conn, peer_info& info)
{
	error_code ec;
	endpoint edp = conn->remote_endpoint(ec);

	info.set_join_time(static_cast<tick_type>(tick_now()));
	info.set_external_ip(edp.address().to_v4().to_ulong());
	if(conn->connection_category() == peer_connection::UDP)
		info.set_external_udp_port(edp.port());
	else
		info.set_external_tcp_port(edp.port());
}
void cache_service::recvd_cached_info(const p2ts_cache_announce_msg& msg, peer_sptr conn)
{
	recvd_cached_channel(msg, conn);
	recvd_erase_channel(msg, conn);
}

void cache_service::recvd_cached_channel(const p2ts_cache_announce_msg& msg, peer_sptr conn)
{
	for (int i=0;i<msg.cached_channels_size();++i)
	{
		const cached_channel_info& info = msg.cached_channels(i);
		const std::string& channelID=info.channel_id();
		boost::shared_ptr<cache_table> tbl = get_channel(channelID);
		if (!tbl) //频道没有找到
		{
			tbl.reset(new cache_table(SHARED_OBJ_FROM_THIS, channelID));
			cache_tables_.insert(cache_table_map::value_type(channelID, tbl));
		}
		if(!tbl)
		{
			BOOST_ASSERT(0);
			continue;
		}

		const peer* p = tbl->insert(conn, info.healthy());//每个频道都把这个peer加进去
		//try_remove_empty_channel(channelID);

		DEBUG_SCOPE(
			if(p)
			{
				std::cout<<"peer: "
					<<string_to_hex(p->m_socket->get_peer_info().peer_id())
					<<" cached cache channel: "
					<<channelID<<std::endl;
			};
		);
	}
}
void cache_service::recvd_erase_channel(const p2ts_cache_announce_msg& msg, peer_sptr conn)
{
	for (int i=0;i<msg.erased_channels_size();++i)
	{
		BOOST_AUTO(itr, cache_tables_.find(msg.erased_channels(i)));
		if (itr != cache_tables_.end())
			itr->second->erase(conn.get());
	}
}

void cache_service::try_remove_empty_channel(const std::string& channelID)
{
	BOOST_AUTO(itr , cache_tables_.find(channelID));
	if(cache_tables_.end() == itr)
		return;
	else if (itr->second->size()==0)
		cache_tables_.erase(itr);
}

void cache_service::on_recvd_login(peer_connection* conn, const safe_buffer& buf)
{
	p2ts_login_msg msg;
	if(!parser(buf, msg))
		return;

	peer_sptr sockt = conn->shared_obj_from_this<peer_connection>();

	//认证并检查频道是否存在
	if (!challenge_channel_check(sockt, msg)) 
		return;	

	peer_info& conn_info = sockt->get_peer_info();
	conn_info =msg.peer_info();
	conn_info.set_join_time(static_cast<tick_type>(tick_now()));

	boost::shared_ptr<cache_table> tbl = get_channel(msg.channel_id());
	if(!tbl) 
		return;

	peer* new_peer = const_cast<peer*>(
		tbl->insert(sockt, msg.peer_info().relative_playing_point())
		);
	if (!new_peer)
	{
		//登录失败
		const int session=(int)msg.session_id();
		challenge_failed(sockt, session, e_already_login);
	}
	else
	{
		//登录成功
		tbl->login_reply(sockt, msg, new_peer, false);

		/*DEBUG_SCOPE(
		std::cout<<"peer: "
		<<string_to_hex(new_peer->m_socket->get_peer_info().peer_id())
		<<" login cache channel: "
		<<msg.channel_id()<<std::endl;
		)//*/
	}
	conn->ping_interval(
		get_tracker_param_sptr()->tracker_peer_ping_interval
		);
}

bool cache_service::challenge_channel_check(
	peer_sptr conn, const p2ts_login_msg& msg)
{
	//验证
	if(!challenge_check(msg, get_tracker_param_sptr()->aaa_key))
	{
		//发送认证失败消息
		challenge_failed(conn, msg.session_id(), e_unauthorized);
		return false; //没有发出过challenge或不通过认证
	}

	//要登录的频道, 如果没有创建过，返回
	const std::string& channelID = msg.channel_id();
	boost::shared_ptr<cache_table> tbl = get_channel(channelID);
	if (!tbl)
	{
		tbl.reset(new cache_table(SHARED_OBJ_FROM_THIS, channelID));
		cache_tables_.insert(cache_table_map::value_type(channelID, tbl));
	}
	return true;
}

boost::shared_ptr<cache_table> cache_service::get_channel(
	const std::string& channelID)
{
	cache_table_map::iterator itr = cache_tables_.find(channelID);
	if(cache_tables_.end() == itr) 
		return boost::shared_ptr<cache_table>();

	return itr->second;
}
