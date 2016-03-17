#include "client/client_service.h"
#include "client/client_service_logic.h"
#include "client/nat.h"
#include "client/peer.h"
#include "client/local_param.h"
#include "client/tracker_manager.h"
#include "client/stream/stream_topology.h"
#include "client/hub/hub_topology.h"
#include "client/cache/cache_service.h"
#include "natpunch/auto_mapping.h"

#if !defined(_DEBUG_SCOPE) && defined(NDEBUG)
#	define  CLIENT_SERVICE_DBG(x) 
#else 
#define  CLIENT_SERVICE_DBG(x) /*x*/
#endif

#define  GUARD_CLIENT_SERVICE_LOGIC(returnValue)\
	client_service_logic_base_sptr svcLogic=client_service_logic_.lock();\
	if(!svcLogic)\
	return;

NAMESPACE_BEGIN(p2client);

namespace{
	static const int NAT_DETECT_DELAY_SECOND=1;//seconds
	static bool _upnp_support=true;
	static bool _natpmp_support=true;
}

//////////////////////////////////////////////////////////////////////////
//client_service
void client_service::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	BOOST_ASSERT(conn->register_message_handler(msgType).size()==0);\
	conn->register_message_handler(msgType).bind(&this_type::handler, this, conn, _1);

	BOOST_ASSERT(conn);

	//REGISTER_HANDLER(tracker_peer_msg::room_info, on_recvd_room_info);
	//REGISTER_HANDLER(global_msg::peer_info, on_recvd_peer_info);
}

client_service::client_service(io_service& iosvc, const client_param_sptr& param)
	:basic_engine_object(iosvc)
	, basic_client_object(param)
	, stoped_(true)
	, online_peer_cnt_(0)
	, local_peer_key_(0)
	, local_nat_type_(NAT_UNKNOWN)
	, extern_udp_port_(0)
	, extern_tcp_port_(0)
	, bind_cache_serice_(false)
{
	set_obj_desc("client_service");
	client_param_->local_info.set_relative_playing_point((int)param->offset);
	CLIENT_SERVICE_DBG(
		std::cout<<"----------------:"<<string_to_hex(param->local_info.peer_id())<<std::endl;
	);

}

client_service:: ~client_service()
{
	client_service_logic_.reset();
	__stop_lowlayer();
	CLIENT_SERVICE_DBG(std::cout<<"~client_service"<<std::endl;);
}

uint64_t client_service::generate_uuid()
{
	uint64_t uid;
	for (int i=0;i<sizeof(uint64_t);++i)
		((char*)(&uid))[i]=random(0, 256);
	return uid;
}

void client_service::start(client_service_logic_base_sptr svcLogic)
{
	if (!stoped_)return;
	BOOST_ASSERT(svcLogic);
	client_service_logic_=svcLogic;
	if (!svcLogic)return;
	stoped_=false;
	__start_client_service();
}

void client_service::restart()
{
	__start_client_service();
}

void client_service::__start_client_service()
{
	error_code ec;
	//点播类应用要注册硬盘缓存索引服务
	bind_cache_serice();

	//生成stream_topology_，但并不start，login成功后才start
	if (stream_topology_)
		stream_topology_->stop();
	stream_topology_=stream_topology::create(SHARED_OBJ_FROM_THIS);

	//生成hub_topology_，但并不start，login成功后才start
	if (hub_topology_)
		hub_topology_->stop();
	hub_topology_=hub_topology::create(SHARED_OBJ_FROM_THIS);

	//生成member_tracker_handler_base_，并立刻启动
	if (member_tracker_handler_)
		member_tracker_handler_->stop();
	member_tracker_handler_= tracker_manager::create(get_io_service(), 
		get_client_param_sptr(), tracker_manager::live_type);
	member_tracker_handler_->ON_KNOWN_NEW_PEER=boost::bind(&this_type::on_known_new_peers, this, _1, _2, true);
	member_tracker_handler_->ON_LOGIN_FINISHED = boost::bind(&this_type::on_login_finished, this, _1, _2);
	member_tracker_handler_->ON_RECVD_USERLEVEL_RELAY = boost::bind(&this_type::on_recvd_userlevel_relay, this, _1, true);
	member_tracker_handler_->start(tracker_and_peer_demain);
}

void client_service::stop(bool flush)
{
	//停止更新当前播放频道的信息
	if(get_cache_service(get_io_service()))
		get_cache_service(get_io_service())->stop_update_cache_timer();

	//reset uplayer
	client_service_logic_.reset();

	//post 销毁底层，否则会引发崩溃。比如：A类a内有一个B类的指针b，b在执行b->f()时候引起
	//a调用a->stop，如果a直接在stop中delete b，那么，会因为还在执行b->f()而崩溃。
	get_io_service().post(
		boost::bind(&client_service::__stop_lowlayer, SHARED_OBJ_FROM_THIS, flush)
		);
}

void client_service::update_server_info()
{
	if(member_tracker_handler_)
		member_tracker_handler_->request_peer(get_client_param_sptr()->channel_uuid);
}

void client_service::bind_cache_serice()
{
	//点播类应用要注册硬盘缓存索引服务
	if  (!bind_cache_serice_&&is_vod()&&get_cache_service(get_io_service()))
	{
		bind_cache_serice_=true;
		boost::shared_ptr<tracker_manager> cacheTrackerHandler=get_cache_tracker_handler(get_io_service());
		BOOST_ASSERT(cacheTrackerHandler);
		cacheTrackerHandler->set_channel_id("");
		cacheTrackerHandler->ON_KNOWN_NEW_PEER=boost::bind(&this_type::on_known_new_peers, this, _1, _2, false);
		cacheTrackerHandler->ON_RECVD_USERLEVEL_RELAY=boost::bind(&this_type::on_recvd_userlevel_relay, this, _1, false);
	}
}

void client_service::__stop_lowlayer(bool flush)
{
	BOOST_ASSERT(client_service_logic_.expired());//uplayer已经关闭

	if(stoped_)return;
	stoped_=true;

	if (member_tracker_handler_)
	{
		member_tracker_handler_->stop();
		member_tracker_handler_.reset();
	}
	if (stream_topology_)
	{
		stream_topology_->stop(flush);
		stream_topology_.reset();
	}
	if (hub_topology_)
	{
		hub_topology_->stop(flush);
		hub_topology_.reset();
	}
}

void client_service::set_play_offset(int64_t offset)
{
	if (stream_topology_)
		stream_topology_->set_play_offset(offset);
	if (hub_topology_)
		hub_topology_->set_play_offset(offset);

	//local_peer_info relative play point update
	get_client_param_sptr()->local_info.set_relative_playing_point((int)offset);
	member_tracker_handler_->report_local_info();
}

boost::shared_ptr<peer> client_service::find_peer(const peer_id_t& id)
{
	peer_map::iterator itr=peers_.find(id);
	if (itr==peers_.end())
		return boost::shared_ptr<peer>();
	return itr->second;
}

peer_nat_type client_service::local_nat_type()
{
	peer_nat_type natType=(peer_nat_type)get_local_nat_type();
	if (external_udp_port()>0||external_tcp_port()>0)//upnp或者natpmp成功，则设置为NAT_OPEN_OR_FULL_CONE
	{
		if(local_nat_type_!=NAT_OPEN_OR_FULL_CONE)
		{
			peer_info& localInfo=get_client_param_sptr()->local_info;
			localInfo.set_nat_type(NAT_OPEN_OR_FULL_CONE);
			localInfo.set_info_version(localInfo.info_version()+1);
			if(member_tracker_handler_)
			{
				get_io_service().post(make_alloc_handler(
					boost::bind(&tracker_manager::report_local_info, 
					member_tracker_handler_)
					));
				if (get_cache_tracker_handler(get_io_service()))
				{
					get_io_service().post(make_alloc_handler(
						boost::bind(&tracker_manager::report_local_info, 
						get_cache_tracker_handler(get_io_service()))
						));
				}
			}
		}
		local_nat_type_=natType=NAT_OPEN_OR_FULL_CONE;
	}
	return natType;
}

void client_service::on_login_finished(error_code_enum e, 
	const ts2p_login_reply_msg& rplMsg)
{
	GUARD_CLIENT_SERVICE_LOGIC(;);
	if (e)
	{
		svcLogic->on_login_failed(e, "login failed");
		return;
	}
	if (!member_tracker_handler_)
	{
		svcLogic->on_login_failed(e, "login failed");
		return;
	}

	//频道信息
	if (rplMsg.has_vod_channel_info())
	{
		vod_channel_info_=rplMsg.vod_channel_info();
		//get_vod_channel_info(vod_channel_info_->channel_uuid())=rplMsg.vod_channel_info();
		get_client_param_sptr()->channel_uuid=vod_channel_info_->channel_uuid();

		//offset must less than file length
		if(vod_channel_info_&&
			(get_client_param_sptr()->offset > vod_channel_info_->film_length())
			)
		{
			CLIENT_SERVICE_DBG(
				std::cout<<"offset must less than file length! offset="
				<<get_client_param_sptr()->offset<<", filmLen="<<vod_channel_info_->film_length()
				<<"\n";
			BOOST_ASSERT(0);
			);
			set_play_offset(0);
		}
	}
	else if(rplMsg.has_live_channel_info())
	{
		live_channel_info_=rplMsg.live_channel_info();
	}

	if (rplMsg.has_online_peer_cnt())
		online_peer_cnt_= rplMsg.online_peer_cnt();

	//if(rplMsg.has_cache_tracker_addr()&& 
	//	(get_cache_service(get_io_service()))&&
	//	!(get_cache_service(get_io_service())->is_running()))
	//{
	//	get_cache_tracker_handler(get_io_service())->update_tracker_adder(
	//		rplMsg.cache_tracker_addr()
	//		);

	//	get_cache_service(get_io_service())->start(get_client_param_sptr()->channel_uuid);

	//}

	address localAddr;
	if(member_tracker_handler_->local_endpoint())
		localAddr=member_tracker_handler_->local_endpoint()->address();
	endpoint tcpLocalEndpoint(localAddr, get_tcp_mapping().first);
	endpoint udpLocalEndpoint(localAddr, get_udp_mapping().first);
	//启动stream topology
	BOOST_ASSERT(stream_topology_);
	stream_topology_->set_tcp_local_endpoint(tcpLocalEndpoint);
	stream_topology_->set_udp_local_endpoint(udpLocalEndpoint);
	stream_topology_->start();
	//启动hub topology
	BOOST_ASSERT(hub_topology_);
	hub_topology_->set_tcp_local_endpoint(tcpLocalEndpoint);
	hub_topology_->set_udp_local_endpoint(udpLocalEndpoint);
	hub_topology_->start();

	CLIENT_SERVICE_DBG(
		std::cout<<"----------------------------\n";
	std::cout<<stream_topology_->trdp_acceptor_local_endpoint()<<std::endl;
	std::cout<<stream_topology_->urdp_acceptor_local_endpoint()<<std::endl;
	std::cout<<hub_topology_->trdp_acceptor_local_endpoint()<<std::endl;
	std::cout<<hub_topology_->urdp_acceptor_local_endpoint()<<std::endl;
	std::cout<<"----------------------------\n";
	);

	//local_peer_info
	/*
	没必要再设置了，member_tracker_handler_已经在收到报文时候过
	get_client_param_sptr()->local_info.set_external_tcp_port(
		stream_topology_->trdp_acceptor_local_endpoint().port()
		);
	get_client_param_sptr()->local_info.set_internal_udp_port(
		stream_topology_->urdp_acceptor_local_endpoint().port()
		);
	*/

	member_tracker_handler_->report_local_info();

	//这里，加入cache节点请求。
	if(get_cache_service(get_io_service()))
		get_cache_service(get_io_service())->start_update_cache_timer(get_client_param_sptr()->channel_uuid);

	svcLogic->on_login_success();
}

void client_service::on_known_new_peers(const std::string& channelID, 
	const std::vector<const peer_info*>& peerInfos, bool sameChannel
	)
{
	if (channelID!=get_client_param_sptr()->channel_uuid)
		return;
	for (size_t i=0;i<peerInfos.size();++i)
	{
		on_known_new_peer(*peerInfos[i], sameChannel);
	}
}
//client_service通过tracker得到合作节点
//得到合作节点的同时通知overlay
void client_service::on_known_new_peer(const peer_info& peerInfo, bool sameChannel)
{

	GUARD_CLIENT_SERVICE_LOGIC(;);

	if (peerInfo.peer_id()==get_client_param_sptr()->local_info.peer_id())
		return ;

	//是服务器或者助理服务器
	if (peerInfo.peer_type()==SERVER||peerInfo.peer_type()==ASSIST_SERVER)
	{
		CLIENT_SERVICE_DBG(;
		std::cout<<peerInfo.peer_type()<<"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@SERVER@@@@@@@@@@@:"
			<<external_udp_endpoint(peerInfo)<<std::endl;
		);
		peer_map& servers=server_peers();
		peer_sptr& s=servers[peer_id_t(peerInfo.peer_id())];
		bool same_channel = is_vod_category(get_client_param_sptr()->type)?false : true;
		if (!s)
			s=peer::create(same_channel, true);
		s->get_peer_info()=peerInfo;
		return;
	}

	CLIENT_SERVICE_DBG(
		if (!sameChannel)
			std::cout<<"xxxxxxx"<<std::endl;
	);
	//是普通的节点
	peer_id_t id(peerInfo.peer_id());
	peer_map::iterator itr=peers_.find(id);
	if (itr==peers_.end())
	{
		if ((int)peers_.size()>MAX_PEER_CNT_PER_ROOM+50)
			return;

		//都认为是not thesame吧，反正播放同一个频道而且播放点非常相近的情况很少
		if (is_vod())
			sameChannel=false;

		boost::shared_ptr<peer> p=peer::create(sameChannel, false);
		p->get_peer_info()=peerInfo;
		peers_[id]=p;

		//std::cout<<"add peer::"<<peerInfo.peer_id()<<std::endl;
		//message_topology_也会调用本函数，如果直接调用message_topology_的known_new_peer
		//可能造成message_topology_中的membertable迭代器出现问题，所以，这里用post
		get_io_service().post(
			make_alloc_handler(boost::bind(&overlay::known_new_peer, stream_topology_, id, p))
			);
		get_io_service().post(
			make_alloc_handler(boost::bind(&overlay::known_new_peer, hub_topology_, id, p))
			);

		//CLIENT_SERVICE_DBG(
		//	std::cout<<"xxxxxxx-----know new peer from same channel: "<<sameChannel<<" [0(false)-1(true)]"<<std::endl;			
		//);
		//通知业务逻辑层
		timestamp_t hisJoinTm=static_cast<timestamp_t>(peerInfo.join_time());
		timestamp_t myJoinTm=static_cast<timestamp_t>(get_client_param_sptr()->local_info.join_time());
		if (time_greater(hisJoinTm, myJoinTm)
			//&&login_time_stamp_+boost::posix_time::millisec((boost::uint32_t)(hisJoinTm-myJoinTm))+boost::posix_time::seconds(2)>=ptime_now()
			)
		{
			//chatroom应用中，UI可能显示"欢迎xxx加入聊天室"
			svcLogic->on_join_new_peer(id, peerInfo.user_info());
		}
		else
		{
			//chatroom应用中，UI可能仅仅节点添加到聊天室内, 而不必显示欢迎
			svcLogic->on_known_online_peer(id, peerInfo.user_info());
		}

	}
	else
	{
		//只有信息版本比记录的新才更新
		if (itr->second->get_peer_info().info_version()<peerInfo.info_version())
		{
			peer_info oldInfo=itr->second->get_peer_info();
			itr->second->get_peer_info()=peerInfo;
			if (oldInfo.user_info()!=peerInfo.user_info())
			{
				svcLogic->on_user_info_changed(id, 
					oldInfo.user_info(), peerInfo.user_info());
			}
		}
	}
}

void  client_service::on_known_offline_peer(const peer_id_t& peerID)
{
	GUARD_CLIENT_SERVICE_LOGIC(;);
	//是服务器或者助理服务器
	peer_map& servers=server_peers();
	peer_map::iterator svrItr=servers.find(peerID);
	if(svrItr!=servers.end())
	{
		offline_peers_keeper_.try_keep(peerID, seconds(100));
		servers.erase(svrItr);
		return;
	}

	//是一般的节点

	//message_topology_也会调用本函数，如果直接调用message_topology_的known_new_peer
	//可能造成message_topology_中的membertable迭代器出现问题，所以，这里用post
	get_io_service().post(make_alloc_handler(
		boost::bind(&client_service::__topology_erase_offline_peer, SHARED_OBJ_FROM_THIS, peerID)
		));

	peer_map::iterator itr=peers_.find(peerID);
	if (itr!=peers_.end())
	{
		peers_.erase(itr);
		offline_peers_keeper_.try_keep(peerID, seconds(50));
		svcLogic->on_known_offline_peer(peerID);
	}
}

void  client_service::__topology_erase_offline_peer(const peer_id_t& peerID)
{
	stream_topology_->erase_offline_peer(peerID);
	hub_topology_->erase_offline_peer(peerID);
}

void client_service::on_recvd_userlevel_relay(const relay_msg& msg, bool sameChannel)
{
	GUARD_CLIENT_SERVICE_LOGIC(;);

	(void)(sameChannel);
	if (msg.dst_peer_id()==get_client_param_sptr()->local_info.peer_id())
	{//消息是发给自己的
		if (msg.level()==USER_LEVEL)//应用层的消息
		{
			svcLogic->on_recvd_msg(msg.msg_data(), peer_id_t(msg.src_peer_id()));
		}
		else//nat穿越等的系统层消息
		{
			BOOST_ASSERT(0);
		}
	}
	else
	{
		//不是dst，将消息往下传递，中继由hub层中完成，这里不做中继处理
		BOOST_ASSERT(0);
	}
}

void client_service::on_recvd_room_info(const safe_buffer&buf)
{
	ts2p_room_info_msg msg;
	if(!parser(buf, msg))
	{
		//PEER_LOGGER(warning)<<"can't parse ts2p_room_info_msg";
		return;
	}
	online_peer_cnt_=std::max((int)peers_.size(), (int)msg.online_peer_cnt());
	for (int i=0;i<msg.offline_peer_list_size();++i)
	{
		on_known_offline_peer(peer_id_t(msg.offline_peer_list(i)));
	}
	for(int i=0;i<msg.new_login_peer_list_size();i++)
	{
		const peer_info& peerInfo=msg.new_login_peer_list(i);
		if (!is_offline(peer_id_t(peerInfo.peer_id())))
		{
			on_known_new_peer(peerInfo, true);
		}
		//CLIENT_SERVICE_DBG(
		//	endpoint edp(asio::ip::address(asio::ip::address_v4(peerInfo.external_ip())), peerInfo.external_tcp_port());
		//std::cout<<"new login:"<<edp<<std::endl;
		//);
	}
	for (int i=0;i<msg.online_peer_list_size();++i)
	{
		const peer_info& peerInfo=msg.online_peer_list(i);
		if (!is_offline(peer_id_t(peerInfo.peer_id())))
		{
			on_known_new_peer(peerInfo, true);
		}
		//CLIENT_SERVICE_DBG(
		//	endpoint edp(asio::ip::address(asio::ip::address_v4(peerInfo.external_ip())), peerInfo.external_tcp_port());
		//std::cout<<"online peer:"<<edp<<std::endl;
		//);
	}
}
//原来logic中的函数, 收到媒体流
void client_service::on_recvd_media(const safe_buffer& buf, const peer_id_t& srcPeerID, 
	media_channel_id_t mediaChannelID, coroutine coro)
{

}
void client_service::report_quality(const p2ts_quality_report_msg& msg)
{
	member_tracker_handler_->report_play_quality(msg);
}

NAMESPACE_END(p2client);