#include "tracker/member_service.h"
#include "tracker/tracker_service.h"
#include "tracker/tracker_service_logic.h"

using namespace p2tracker;

typedef member_table::peer peer;

static const time_duration CHANNEL_INFO_BROADCAST_INTERVAL=seconds(1);

void member_service::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(tracker_peer_msg::login, on_recvd_login);
}

member_service::member_service(io_service& net_svc, tracker_param_sptr param)
	: basic_engine_object(net_svc)
	, basic_tracker_object(param)
	, channel_change_cnt_(0)
{
	set_obj_desc("member_service");
}

member_service::~member_service()
{
	stop();
}

void member_service::__start(boost::shared_ptr<tracker_service_logic_base> tsLogic, 
	const std::string& channelID
	)
{
	BOOST_ASSERT(!urdp_acceptor_);
	BOOST_ASSERT(!trdp_acceptor_);

	tracker_service_logic_=tsLogic;

	//频道
	channel::shared_ptr this_channel = get_channel(channelID);
	if (!this_channel) 
		return;

	//监听的是这个domain（tracker_and_peer_demain+"/"+channelID），连接到这个域的就都是channelID的对象
	//member_service要接受多个频道的连接，域中不包含特定的channelID.
	const std::string domain=tracker_and_peer_demain/*+"/"+channelID*/;
	endpoint edp = endpoint_from_string<endpoint>(
		get_tracker_param_sptr()->internal_ipport
		);

	error_code ec;
	start_acceptor(edp, domain, ec);
}

//启动直播频道
void member_service::start(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
	const peer_info& serverInfo, 
	const message_socket_sptr server_socket, 
	const live_channel_info& channelInfo
	)
{
	const std::string& channelLink=channelInfo.channel_uuid();//live的link和uuid一样
	start_any_channel(
		tsLogic, 
		channelLink, 
		serverInfo, 
		server_socket, 
		channelInfo
		);
	//有新频道加进来时就对定时器进行一次更新
	//update_channel_broadcast_timer();
}
//启动一个点播频道
void member_service::start(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
	const peer_info& serverInfo, 
	const message_socket_sptr server_socket, 
	const vod_channel_info& channelInfo)
{
	const std::string& channelLink=channelInfo.channel_link();
	start_any_channel(
		tsLogic, 
		channelLink, 
		serverInfo, 
		server_socket, 
		channelInfo
		);
}

template<typename ChannelInfoType>
void member_service::start_any_channel(boost::shared_ptr<tracker_service_logic_base>tsLogic, 
	const std::string& channelID, 
	const peer_info& serverInfo, 
	const message_socket_sptr server_socket, 
	const ChannelInfoType& channelInfo)
{
	if(!create_channel(channelID, server_socket))
		return;

	if (!urdp_acceptor_ && !trdp_acceptor_)
		__start(tsLogic, channelID);

	channel::shared_ptr this_channel = get_channel(channelID);
	if (!this_channel) 
		return;

	peer_info realServerInfo=serverInfo;
	if (server_socket->connection_category()==message_socket::UDP)
	{
		p2engine::error_code ec;
		if (get_tracker_param_sptr()->b_for_shunt)
		{
			BOOST_AUTO(edp, endpoint_from_string<variant_endpoint>(
				get_tracker_param_sptr()->external_ipport));
			realServerInfo.set_external_ip(edp.address().to_v4().to_ulong());
			realServerInfo.set_external_udp_port(edp.port());
			DEBUG_SCOPE(
				std::cout<<"----------"<<edp<<std::endl;
			std::cout<<"server from UDP( "<<edp<<" )"<<std::endl;
			);
		}
		else
		{
			BOOST_AUTO(edp, server_socket->remote_endpoint(ec));
			realServerInfo.set_external_udp_port(edp.port());
			DEBUG_SCOPE(
				std::cout<<"----------"<<edp<<std::endl;
			std::cout<<"server from UDP( "<<edp<<" )"<<std::endl;
			);
		}
	}
	else
	{
		p2engine::error_code ec;
		if (get_tracker_param_sptr()->b_for_shunt)
		{
			BOOST_AUTO(edp, endpoint_from_string<variant_endpoint>(
				get_tracker_param_sptr()->external_ipport));
			realServerInfo.set_external_ip(edp.address().to_v4().to_ulong());
			realServerInfo.set_external_tcp_port(edp.port());
			DEBUG_SCOPE(
				std::cout<<"----------"<<edp<<std::endl;
			std::cout<<"server from TCP( "<<edp<<" )"<<std::endl;
			);
		}
		else
		{
			BOOST_AUTO(edp, server_socket->remote_endpoint(ec));
			realServerInfo.set_external_tcp_port(edp.port());
			DEBUG_SCOPE(
				std::cout<<"----------"<<edp<<std::endl;
			std::cout<<"server from TCP( "<<edp<<" )"<<std::endl;
			);
		}
	}

	DEBUG_SCOPE(
		std::cout<<"server udp org internal: "<<internal_udp_endpoint(serverInfo)<<std::endl;
	std::cout<<"server udp org external: "<<external_udp_endpoint(serverInfo)<<std::endl;
	std::cout<<"server udp real external: "<<external_udp_endpoint(realServerInfo)<<std::endl;
	std::cout<<"server tcp org internal: "<<internal_tcp_endpoint(serverInfo)<<std::endl;
	std::cout<<"server tcp org external: "<<external_tcp_endpoint(serverInfo)<<std::endl;
	std::cout<<"server tcp real external: "<<external_tcp_endpoint(realServerInfo)<<std::endl;
	);
	/*if ("acedf2e2970b34bede00c839f697930e"==channelID)
	{
	p2engine::error_code e;
	BOOST_AUTO(edp, server_socket->remote_endpoint(e));

	FILE* fp=fopen("c:\\tracker.log", "a+");
	if (fp)
	{

	std::string str=std::string("\n\nserver udp org internal: ")+ endpoint_to_string(internal_udp_endpoint(serverInfo))
	+"server udp org external: "+ endpoint_to_string(external_udp_endpoint(serverInfo))
	+"server udp real external: "+ endpoint_to_string(external_udp_endpoint(realServerInfo))
	+"server tcp org internal: "+ endpoint_to_string(internal_tcp_endpoint(serverInfo))
	+"server tcp org external: "+ endpoint_to_string(external_tcp_endpoint(serverInfo))
	+"server tcp real external: "+ endpoint_to_string(external_tcp_endpoint(realServerInfo))
	+"--------?-------------: "+endpoint_to_string(edp)
	+"\n"+boost::lexical_cast<std::string>((int)timestamp_now());
	fwrite(str.c_str(), 1, str.size(), fp);
	fclose(fp);
	}
	}*/

	this_channel->start(realServerInfo, channelInfo);
}

void member_service::stop()
{
	//TODO:do what?
	//关闭所有频道的定时器
	BOOST_FOREACH(channel_pair_type& chn_pair, channel_set_)
	{
		chn_pair.second->stop();
	}
	close_acceptor();
	tracker_service_logic_.reset();
	//这里仿照cache_service对各个频道的member_table进行清理
	channel_set_.clear();
}
/*!
* \brief 检查频道广播是否要更新.
*
*/
bool member_service::broad_cast_condition(uint32_t min_channel_cnt/* = 100*/, 
	float change_thresh/* = 0.1*/)
{
	//频道数不到100个，不用更新，累积基础节点
	if(channel_set_.size() >= min_channel_cnt) 
		return false;

	//记录增加的频道数
	++channel_change_cnt_;

	//频道数到100以上，且增加的个数达到阈值，更新
	if(float(channel_change_cnt_) / (channel_set_.size()+1) >= change_thresh )
	{
		channel_change_cnt_ = 0;
		return true;
	}
	return false;
}

void member_service::known_offline(const peer& p)
{
	//获取频道
	boost::shared_ptr<tracker_service_logic_base> tslogic 
		= tracker_service_logic_.lock();
	if (tslogic)
		tslogic->known_offline(const_cast<peer*>(&p));
}

void member_service::remove_channel(const std::string& channelID)
{
	channel::shared_ptr this_channel = get_channel(channelID);
	if (!this_channel) 
		return;

	//判断server是否断开
	error_code ec;
	//this_channel->m_server_socket->ping(ec);
	if (this_channel->server_socket() && 
		this_channel->server_socket()->is_connected())
	{
		boost::shared_ptr<empty_channel>& an_channel = empty_channels_[channelID];
		if(!an_channel)
		{
			an_channel.reset(new empty_channel(*this_channel));

			DEBUG_SCOPE(
				std::cout<<get_obj_desc()
				<<" channel id: "<<this_channel->channel_id()<<" cleared"<<std::endl;
			std::cout<<get_obj_desc()
				<<" before erase channel count: "<<channel_set_.size()<<std::endl;
			)
		}
	}
	this_channel->stop();
	channel_set_.erase(channelID);
	DEBUG_SCOPE(
		std::cout<<get_obj_desc()
		<<" after erase channel count: "<<channel_set_.size()<<std::endl;
	)
}

void member_service::recvd_peer_info_report(peer* p, const p2ts_quality_report_msg& msg)
{
	BOOST_AUTO(svc, tracker_service_logic_.lock());
	if(svc)
		svc->recvd_peer_info_report(p, msg);
}

void member_service::on_accepted(message_socket_sptr conn, const error_code& ec)
{
	if (ec) return;

	//将这一链接暂存起来，如果暂存超时还没有收到合法报文，则关闭连接。
	//pending.challenge=boost::lexical_cast<std::string>(random<boost::uint64_t>(0ULL, 0xffffffffffffffffULL));
	pending_sockets_.try_keep(conn, seconds(20));
	register_message_handler(conn.get());
	conn->keep_async_receiving();

	//发送challenge
	send_challenge_msg(conn);
}

//std::string member_service::shared_key_signature(const std::string& pubkey)
//{
//	std::string theStr=generate_shared_key(key_pair_.second, pubkey);
//
//	md5_byte_t digest[16];
//	md5_state_t pms;
//	md5_init(&pms);
//	md5_append(&pms, (const md5_byte_t *)theStr.c_str(), theStr.length());
//	md5_finish(&pms, digest);
//
//	return std::string((char*)digest, 16);
//}

//////////////////////////////////////////////////////////////////////////
//消息处理，不同的频道节点在这里分派到不同的member_table
//节点连接到member_service
void member_service::on_recvd_login(message_socket* sockPtr, safe_buffer buf)
{
	p2ts_login_msg msg;
	if(!parser(buf, msg))
		return;

	const int session = (int)msg.session_id();
	const peer_info& peerInfo = msg.peer_info();
	const std::string& channelID = msg.channel_id();

	message_socket_sptr conn = sockPtr->shared_obj_from_this<message_socket>();
	pending_sockets_.erase(conn);

	if(!challenge_channel_check(conn, msg)) 
		return;

	channel::shared_ptr this_channel = get_channel(channelID);
	if (!this_channel) 
	{
		challenge_failed(conn, session, e_not_found);
		return;
	}

	const peer* p = this_channel->insert(conn, peerInfo);
	if (!p)
	{
		challenge_failed(conn, session, e_already_login);
	}
	else
	{	
		//登录成功，发送合作节点表
		this_channel->login_reply(conn, msg, p);	
		conn->ping_interval(get_tracker_param_sptr()->tracker_peer_ping_interval);
	}
}

bool member_service::challenge_channel_check(message_socket_sptr conn, 
	p2ts_login_msg& msg)
{
	TODO("防止频繁登录的攻击");
	//error_code ec;
	//endpoint edp = conn->remote_endpoint(ec);
	//if (ec) 
	//	return false;

	//验证certificate 没有发出过challenge或不通过认证
	if (!challenge_check(msg, get_tracker_param_sptr()->aaa_key))
	{
		challenge_failed(conn, msg.session_id(), e_unauthorized);
		return false;
	}//测试，暂时把认证去掉*/

	return true;
}

void member_service::play_point_translate(const std::string& channelID, 
	peer_info& peerInfo)
{
	channel::shared_ptr this_channel = get_channel(channelID);
	if(!this_channel) 
		return;

	int pt = -1;
	if(VOD_TYPE == get_tracker_param_sptr()->type)
	{
		pt = this_channel->dynamic_play_point(peerInfo.relative_playing_point());

		peerInfo.set_relative_playing_point(pt);
	}
}

void member_service::kickout(const std::string& channelID, const peer_id_t& id)
{
	channel_map_type::iterator itr = channel_set_.find(channelID);
	if (itr!=channel_set_.end())
		itr->second->kickout(id);
}

channel::shared_ptr member_service::get_channel(
	const std::string& channelID)
{
	channel_map_type::iterator itr = channel_set_.find(channelID);
	if (itr != channel_set_.end())
		return (itr->second);

	//检查没有节点的频道
	empty_channel_map::iterator it = empty_channels_.find(channelID);
	if(it != empty_channels_.end())
	{
		channel::shared_ptr newChannel=create_channel(channelID, it->second->server_sockt_);
		if(newChannel)
		{
			DEBUG_SCOPE(
				std::cout<<get_obj_desc()
				<<" channel id:"<<newChannel->channel_id()<<"restored"
				<<std::endl;
			);

			boost::shared_ptr<empty_channel> ept_channel = it->second;
			newChannel->set_server_info(ept_channel->server_info_);
			empty_channels_.erase(it);
			if(restore_channel(*newChannel, ept_channel))
				return newChannel;
			else
				channel_set_.erase(channelID);
		}
	}
	return channel::shared_ptr();
}

bool member_service::restore_channel(channel& des_channel, 
	boost::shared_ptr<empty_channel>& ept_channel)
{
	if(is_live_category(get_tracker_param_sptr()->type)
		&&ept_channel->live_channel_info_
		)
	{
		des_channel.set_channel_info(*(ept_channel->live_channel_info_));
		return true;
	}
	else if(ept_channel->vod_channel_info_)
	{
		des_channel.set_channel_info(*(ept_channel->vod_channel_info_));
		return true;
	}
	return false;
}
channel::shared_ptr member_service::create_channel(const std::string& channelID, message_socket_sptr conn)
{
	//在频道列表中查找该频道是不是已经创建了.
	channel_map_type::iterator itr = channel_set_.find(channelID);
	if (itr !=channel_set_.end())
	{
		//更新server
		itr->second->set_server_socket(conn);
		return itr->second;
	}

	////加入列表
	std::pair<channel_map_type::iterator, bool> ret=channel_set_.insert(
		std::make_pair(channelID, channel::create(SHARED_OBJ_FROM_THIS, conn, channelID))
		);
	if(ret.second)
		return ret.first->second;
	return channel::shared_ptr();
}

bool member_service::pending_socket_check(message_socket_sptr conn)
{
	BOOST_AUTO(itr, pending_sockets_.find(conn));
	if (itr == pending_sockets_.end()) 
		return false;
	return true;
}

