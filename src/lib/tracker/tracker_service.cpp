#include "tracker/tracker_service.h"
#include "tracker/tracker_service_logic.h"
#include "tracker/member_service.h"
#include "common/policy.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <string>
#include <p2engine/pop_warning_option.hpp>

using namespace p2tracker;

void tracker_service::register_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(server_tracker_msg::create_channel, on_recvd_create_channel);
	//REGISTER_HANDLER(server_tracker_msg::info_report, on_recvd_info_report);
	//	tracker_service_logic_->register_message_handler(conn);
}

tracker_service::tracker_service(io_service& net_svc, tracker_param_sptr param)
:basic_engine_object(net_svc)
, basic_tracker_object(param)
{
	set_obj_desc("tracker_service");
}

tracker_service::~tracker_service()
{
	__stop();
}
void tracker_service::__start()
{
	BOOST_ASSERT(!urdp_acceptor_);
	BOOST_ASSERT(!trdp_acceptor_);

	const std::string domain= tracker_and_server_demain+"/tracker_service";
	endpoint edp = endpoint_from_string<endpoint>(tracker_param_->internal_ipport);

	error_code ec;
	start_acceptor(edp, domain, ec);
	register_create_handler();

	pending_sockets_clean_timer_=timer::create(get_io_service());
	pending_sockets_clean_timer_->register_time_handler(boost::bind(
		&this_type::on_check_pending_sockets_timer, this));
	pending_sockets_clean_timer_->async_keep_waiting(seconds(10), seconds(10));
}
//等待来自tracker_session的链接请求
void tracker_service::start(boost::shared_ptr<tracker_service_logic_base>tsLogic)
{
	tracker_service_logic_=tsLogic;

	if(GLOBAL_CACHE_TYPE == tracker_param_->type)
		global_cache_start();

	__start();
}
void tracker_service::stop()
{
	__stop();
}
void tracker_service::__stop()
{
	close_acceptor();
	if (member_service_)
	{
		member_service_->stop();
		member_service_.reset();
	}
	if (cache_service_)
	{
		cache_service_->stop();
		cache_service_.reset();
	}
	if(global_cache_service_)
	{
		global_cache_service_->stop();
		global_cache_service_.reset();
	}
	if (pending_sockets_clean_timer_)
	{
		pending_sockets_clean_timer_->cancel();
		pending_sockets_clean_timer_.reset();
	}
}

void tracker_service::on_check_pending_sockets_timer()
{
	pending_sockets_.clear_timeout();
}

void tracker_service::register_create_handler()
{
	channel_creators_.insert(std::make_pair(INTERACTIVE_LIVE_TYPE, boost::bind(
		&this_type::live_start, this, _1, _2)));
	channel_creators_.insert(std::make_pair(LIVE_TYPE, boost::bind(
		&this_type::live_start, this, _1, _2)));
	channel_creators_.insert(std::make_pair(VOD_TYPE, boost::bind(
		&this_type::vod_start, this, _1, _2)));
	channel_creators_.insert(std::make_pair(BT_TYPE, boost::bind(
		&this_type::cache_start, this, _1, _2)));
}

void tracker_service::on_accepted(message_socket_sptr conn, const error_code& ec)
{
	if (!ec)
	{
		//将这一链接暂存起来，如果暂存超时还没有收到合法报文，则关闭连接。
		pending_sockets_.try_keep(conn, seconds(10));
		register_message_handler(conn.get());
		conn->ping_interval(LIVE_TRACKER_SERVER_PING_INTERVAL);
		conn->keep_async_receiving();
	}
}

void tracker_service::on_recvd_create_channel(message_socket*sock, safe_buffer buf)
{
	//接收到tracker_session发送过来的创建频道请求.
	//有了频道后频道启动等待peer的连接
	s2ts_create_channel_msg msg;
	if (!parser(buf, msg))
	{
		sock->close(true);
		return;
	}

	distribution_type id = (distribution_type)msg.distribute_type();
	channel_creator_map::iterator it = channel_creators_.find(id);
	if(it != channel_creators_.end()) 
		(it->second)(msg, sock->shared_obj_from_this<message_socket>());
	else
		sock->close(true);
}


void tracker_service::create_member_service()
{
	if (member_service_) 
		return;
	member_service_ = member_service::create(get_io_service(), tracker_param_);
}

void tracker_service::create_cache_service()
{
	if(cache_service_) 
		return;
	cache_service_ = cache_service::create(get_io_service(), tracker_param_);
}

void tracker_service::live_start(const s2ts_create_channel_msg& msg, 
								 message_socket_sptr conn_sptr)
{
	if (!tracker_service_logic_.lock())
		return;
	
	if(!msg.has_live_channel_info())
		return;

	create_member_service();

	//启动直播
	member_service_->start(tracker_service_logic_.lock(), 
		msg.server_info(), conn_sptr, msg.live_channel_info()
		);

	DEBUG_SCOPE(
		const std::string& channelID = msg.live_channel_info().channel_uuid();
	std::cout<<"start live channel: "<<channelID<<std::endl;
	);
}

void tracker_service::vod_start(const s2ts_create_channel_msg& msg, 
								message_socket_sptr conn_sptr)
{
	if (!tracker_service_logic_.lock())
		return;

	if(!msg.has_vod_channel_info()||msg.vod_channel_info().film_length()<0)
		return;

	create_member_service();

	//启动点播
	member_service_->start(tracker_service_logic_.lock(), 
		msg.server_info(), conn_sptr, msg.vod_channel_info()
		);

	cache_start(msg, conn_sptr);

	DEBUG_SCOPE(
		const std::string& channelID = msg.vod_channel_info().channel_uuid();
	std::cout<<"start vod channel uuid: "<<channelID<<std::endl;
	);
}

void tracker_service::cache_start(const s2ts_create_channel_msg& msg, 
								 message_socket_sptr )
{
	if (!tracker_service_logic_.lock())
		return;

	if(!msg.has_vod_channel_info())
		return;

	create_cache_service();
	//启动cache
	cache_service_->start(tracker_service_logic_.lock());
}

void tracker_service::global_cache_start()
{
	if (!tracker_service_logic_.lock())
		return;

	if(global_cache_service_) 
		return;

	global_cache_service_ = cache_service::create(get_io_service(), tracker_param_);
	//启动 全局cache
	global_cache_service_->start(tracker_service_logic_.lock());
}