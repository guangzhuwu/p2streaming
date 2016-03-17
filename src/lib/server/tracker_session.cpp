#include "server/tracker_session.h"
#include "server/server_service.h"
#include <boost/filesystem.hpp>

using namespace p2server;

void tracker_session::register_server_message_handler(message_socket* conn)
{
#define REGISTER_HANDLER(msgType, handler)\
	conn->register_message_handler(msgType, boost::bind(&this_type::handler, this, conn, _1));

	REGISTER_HANDLER(server_tracker_msg::info_reply, on_recvd_channel_info_reply);
	REGISTER_HANDLER(server_tracker_msg::distribute, on_recvd_distribute);

#undef REGISTER_HANDLER
}

tracker_session::tracker_session(io_service& net_svc, server_param_sptr param)
	: basic_engine_object(net_svc)
	, server_param_(param)
	, client_count_(0)
	, playing_quality_(1.0)
	, global_remote_to_local_lost_rate_(0)
{
	set_obj_desc("tracker_session");
}

tracker_session::~tracker_session()
{
	stop();
}

void tracker_session::start(boost::shared_ptr<server_service> serverSvc)
{
	server_service_=serverSvc;
	if (is_live_category(server_param_->type))//直播频道才启动这个定时器
	{
		info_report_timer_=timer::create(get_io_service());
		info_report_timer_->set_obj_desc("server::tracker_session::info_report_timer_");
		info_report_timer_->register_time_handler(boost::bind(&this_type::on_info_report_timer, this));
		info_report_timer_->async_keep_waiting(seconds(5), seconds(1));
	}	

	info_request_timer_=timer::create(get_io_service());
	info_request_timer_->set_obj_desc("server::tracker_session::info_request_timer_");
	info_request_timer_->register_time_handler(boost::bind(&this_type::on_info_request_timer, this));
	info_request_timer_->async_keep_waiting(seconds(5), seconds(5));

	delay_connect_timer_=timer::create(get_io_service());
	delay_connect_timer_->set_obj_desc("server::tracker_session::delay_connect_timer_");
	delay_connect_timer_->register_time_handler(boost::bind(&this_type::connect, this));

	connect();
}

void tracker_session::stop()
{
	if (urdp_socket_)
	{
		urdp_socket_->close(true);
		urdp_socket_.reset();
	}
	if (info_report_timer_)
	{
		info_report_timer_->cancel();
		info_report_timer_.reset();
	}
	if (delay_connect_timer_)
	{
		delay_connect_timer_->cancel();
		delay_connect_timer_.reset();
	}
	if(info_request_timer_)
	{
		info_request_timer_->cancel();
		info_request_timer_.reset();
	}
	DEBUG_SCOPE(
		std::cout<<get_obj_desc()<<" stop channel:"<<server_param_->channel_uuid<<std::endl;
	);
}

void tracker_session::connect()
{
	const std::string domain= tracker_and_server_demain+"/tracker_service";
	endpoint remoteEdp = endpoint_from_string<udp::endpoint>(server_param_->tracker_ipport);
	endpoint localEdp = endpoint_from_string<udp::endpoint>(server_param_->internal_ipport);
	endpoint localWildEdp;
	localWildEdp.port(localEdp.port());
	LogInfo(
		"\n "
		"tracker_session connect: %s; "

		, server_param_->tracker_ipport.c_str()
		);

	error_code ec;
	urdp_socket_=urdp_message_socket::create(get_io_service(), true);
	urdp_socket_->open(localWildEdp, ec);
	urdp_socket_->register_connected_handler(boost::bind(&this_type::on_connected, this, urdp_socket_.get(), _1));
	urdp_socket_->async_connect(remoteEdp, domain);

	LogInfo(
		"\n "
		"tracker_session bind port %d succeed!; "

		, localEdp.port()
		);
}

void tracker_session::on_connected(message_socket* conn, const error_code& ec)
{
	if (conn!=urdp_socket_.get())
		return;
	boost::shared_ptr<server_service> serverSvc=server_service_.lock();
	if (!serverSvc)
		return;
	if (ec)
	{
		DEBUG_SCOPE(
			std::cout<<"tracker_session on_connected"<<ec.message().c_str()<<std::endl;
		);
		LOG(
			LogInfo(
			"\n "
			"tracker_session connected error: %s; "

			, ec.message().c_str()
			);
		);

		//稍等片刻后重连
		delay_connect_timer_->async_wait(seconds(1));
	}
	else
	{
		delay_connect_timer_->cancel();
		register_server_message_handler(conn);

		//注册网络事件
		conn->register_disconnected_handler(boost::bind(&this_type::on_disconnected, this, conn, _1));
		conn->ping_interval(is_live_category(server_param_->type)
			?LIVE_TRACKER_SERVER_PING_INTERVAL*4/5:VOD_TRACKER_SERVER_PING_INTERVAL*4/5
			);

		//发送频道注册信息
		s2ts_create_channel_msg msg;
		*msg.mutable_server_info() = serverSvc->server_info();
		msg.set_distribute_type(server_param_->type); //tracker类型
		if (is_live_category(server_param_->type))
		{
			boost::shared_ptr<media_distributor> distributor=serverSvc->distributor();
			live_channel_info* liveInfo = msg.mutable_live_channel_info();
			liveInfo->set_channel_uuid(server_param_->channel_uuid);
			liveInfo->set_server_packet_rate(distributor->packet_rate());
			liveInfo->set_server_seqno(distributor->current_media_seqno());
			liveInfo->set_server_time(distributor->current_time());
			DEBUG_SCOPE(
				std::cout<<"create live channel: "<<server_param_->channel_link
				<<std::endl;
			);
			LogInfo("create live channel: %s; ", server_param_->channel_link.c_str());
		}
		else
		{
			simple_distributor_sptr distributor = serverSvc->get_simple_distributor();

			vod_channel_info* vodInfo = msg.mutable_vod_channel_info();
			vodInfo->set_channel_link(server_param_->channel_link);
			vodInfo->set_channel_uuid(server_param_->channel_uuid);

			vodInfo->set_film_duration((int)distributor->get_duration());
			vodInfo->set_film_length((int)distributor->get_length());
			DEBUG_SCOPE(
				std::cout<<"create channel: "<<server_param_->channel_link
				<<" duration: "<<vodInfo->film_duration()
				<<" size: "<<vodInfo->film_length()
				<<std::endl;
			);
			LogInfo("create vod channel: %s; ", server_param_->channel_link.c_str());
		}

		conn->async_send_reliable(serialize(msg), server_tracker_msg::create_channel);
	}
}

void tracker_session::on_disconnected(message_socket* conn, const error_code& ec)
{
	DEBUG_SCOPE(
		std::cout<<"tracker_session on_disconnected"<<ec.message().c_str()<<std::endl;
	);
	LogInfo(
		"\n "
		"tracker_session disconnected : %s; "

		, ec.message().c_str()
		);
	delay_connect_timer_->async_wait(seconds(1));
}
void tracker_session::on_info_request_timer()
{
	boost::shared_ptr<server_service> serverSvc=server_service_.lock();
	if(serverSvc&&(0==serverSvc->out_kbps()))
	{
		client_count_=0;
		playing_quality_=0.0;
		global_remote_to_local_lost_rate_=0.0;
	}

	if(!urdp_socket_)
		return;

	s2ts_channel_status_req msg;
	msg.set_channel_id(server_param_->channel_uuid);

	if(urdp_socket_&&urdp_socket_->is_connected())
		urdp_socket_->async_send_reliable(serialize(msg), server_tracker_msg::info_request);
}
void tracker_session::on_info_report_timer()
{
	BOOST_ASSERT(is_live_category(server_param_->type));

	if (urdp_socket_&&urdp_socket_->is_connected())
	{
		boost::shared_ptr<server_service> serverSvc=server_service_.lock();
		if (!serverSvc)
			return;
		boost::shared_ptr<media_distributor> distributor=serverSvc->distributor();
		if (!distributor)
			return;

		s2ts_channel_report_msg msg;
		live_channel_info* infoMsg=msg.mutable_channel_info();
		infoMsg->set_server_packet_rate(distributor->packet_rate());
		infoMsg->set_server_seqno(distributor->current_media_seqno());
		infoMsg->set_server_time(distributor->current_time());
		infoMsg->set_channel_uuid(server_param_->channel_uuid);
		BOOST_FOREACH(seqno_t seqno, serverSvc->iframe_list())
		{
			msg.add_iframe_seqno(seqno);
		}

		urdp_socket_->async_send_semireliable(serialize(msg), 
			server_tracker_msg::info_report);
	}
}

void tracker_session::on_recvd_channel_info_reply(message_socket* conn, safe_buffer buf)
{
	ts2s_channel_status msg;
	if(!parser(buf, msg))
		return;

	BOOST_AUTO(svr, server_service_.lock());
	if(!svr)
		return;

	client_count_=msg.live_cnt();
	playing_quality_=msg.playing_quality();
	global_remote_to_local_lost_rate_=msg.rtol_lost_rate();
	DEBUG_SCOPE(
		if(client_count_==0)
			BOOST_ASSERT(svr->out_kbps()==0);
		);
	//if(msg.has_quality_info())
		//svr->change_info(serialize(msg.quality_info()));
}

void tracker_session::on_recvd_distribute(message_socket* conn, safe_buffer buf)
{
	BOOST_AUTO(svr, server_service_.lock());
	if(!svr)
		return;
	svr->smooth_distribute(buf, "", 0, SYSTEM_LEVEL);
	////DEBUG_SCOPE(
	////	std::cout<<"XXXXXXXXXXX--recvd broad cast: "
	////	<<std::string(buffer_cast<char*>(buf), buffer_size(buf))<<std::endl;
	////);
}
