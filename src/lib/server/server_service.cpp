#include "server/server_service.h"
#include "server/media_distributor.h"
#include "server/server_service.h"
#include "server/server_service_logic.h"
#include "common/const_define.h"
#include <boost/filesystem.hpp>

using namespace p2server;

server_service::server_service(io_service& ios)
	:basic_engine_object(ios)
	, stoped_(true)
	, client_count_(0)
{
	set_obj_desc("server_service");
}

server_service::~server_service()
{	
	stop();
	DEBUG_SCOPE(
		std::cout<<get_obj_desc()<<" deconstructor"<<std::endl;
		);
}

void server_service::start(const server_param_base& param, error_code& ec, bool embeddedInClient)
{
	BOOST_ASSERT(stoped_);

	if (!stoped_)
		return;
	stoped_=false;

	BOOST_ASSERT(is_live_category(param.type)||param.channel_link!=param.channel_uuid);

	server_param_ =create_server_param_sptr(param);
	std::string md5Uid= md5(param.channel_uuid);

	//设置server_info_
	//这里为了测试临时设置的server_info信息
	server_info_.set_peer_id(&md5Uid[0], md5Uid.size());
	server_info_.set_nat_type(NAT_UNKNOWN);
	server_info_.set_upload_capacity(300);
	server_info_.set_peer_type(SERVER);
	server_info_.set_join_time((ptime_now()-min_time()).total_milliseconds());
	server_info_.set_info_version(0);
	server_info_.set_relative_playing_point(0);
	server_info_.set_user_info(server_param_->channel_uuid);

	endpoint edp=endpoint_from_string<udp::endpoint>(server_param_->internal_ipport);
	server_info_.set_internal_ip(edp.address().to_v4().to_ulong());
	server_info_.set_internal_udp_port(edp.port());
	server_info_.set_internal_tcp_port(edp.port());
	edp=endpoint_from_string<udp::endpoint>(server_param_->external_ipport);
	server_info_.set_external_ip(edp.address().to_v4().to_ulong());
	server_info_.set_external_udp_port(edp.port());
	server_info_.set_external_tcp_port(edp.port());

	for (size_t i=0;i<server_param_->cache_server_ipport.size();++i)
	{
		server_info_.add_cache_server_ipport(server_param_->cache_server_ipport[i]);
	}
	

	//临时设置完毕
	peer_info* serverInfo=NULL;
	if (!embeddedInClient)
	{
		serverInfo=&server_info_;
	}
	else
	{
		TODO_ASSERT("b_embedded_in_client 暂未实现");
	}
	BOOST_ASSERT(serverInfo);

	//启动info_gather_timer_
	if (!info_gather_timer_)
	{
		info_gather_timer_=rough_timer::create(get_io_service());
		info_gather_timer_->set_obj_desc("server::server_service::info_gather_timer_");
		info_gather_timer_->register_time_handler(boost::bind(&this_type::on_info_gather_timer, this));
		info_gather_timer_->async_keep_waiting(seconds(1), seconds(1));
	}
	
	//启动distributor
	if(is_live_category(server_param_->type))
	{
		distributor_=media_distributor::create(get_io_service(), server_param_);
		distributor_->start(*serverInfo, ec);
		if (serverInfo->external_udp_port()==0)
			serverInfo->set_external_udp_port(serverInfo->internal_udp_port());
	}

	if(is_vod_category(server_param_->type))
	{
		if(!sdistributor_scheduling_)
			sdistributor_scheduling_ = p2simple::distributor_scheduling::create(get_io_service());

		//启动simple distributor scheduling
		simple_distributor_ = sdistributor_scheduling_->start(*serverInfo, server_param_->media_directory);

		if (serverInfo->external_udp_port()==0)
			serverInfo->set_external_udp_port(serverInfo->internal_udp_port());

		size_t pos = server_param_->internal_ipport.find(":");
		if(std::string::npos != pos)
			server_param_->internal_ipport = server_param_->internal_ipport.substr(0, pos+1) 
			+ boost::lexical_cast<std::string>(serverInfo->internal_udp_port()); 
		
		pos = server_param_->external_ipport.find(":");
		if(std::string::npos != pos)
			server_param_->external_ipport= server_param_->external_ipport.substr(0, pos+1) 
			+ boost::lexical_cast<std::string>(serverInfo->external_udp_port());
	}

	//启动与tracker的交互
	tracker_session::shared_ptr session=tracker_session::create(get_io_service(), server_param_);
	session->start(SHARED_OBJ_FROM_THIS);
	tracker_sessions_.insert(std::make_pair(server_param_->tracker_ipport, session));
}

void server_service::add_tracker(const std::string& tracker_ipport)
{
	BOOST_ASSERT(!tracker_sessions_.empty());

	if (tracker_sessions_.find(tracker_ipport)!=tracker_sessions_.end())
		return;
	
	server_param_sptr paramSptr = create_server_param_sptr(*server_param_);
	paramSptr->tracker_ipport=tracker_ipport;

	tracker_session::shared_ptr session=tracker_session::create(get_io_service(), paramSptr);
	session->start(SHARED_OBJ_FROM_THIS);
	tracker_sessions_.insert(std::make_pair(paramSptr->tracker_ipport, session));
}

void server_service::del_tracker(const std::string& tracker_ipport)
{
	tracker_sessions_.erase(tracker_ipport);
}

void server_service::change_tracker(const std::string& tracker_ipport)
{
	BOOST_ASSERT(!tracker_sessions_.empty());

	//清除所有的tracker
	tracker_sessions_.clear();
	server_param_->tracker_ipport=tracker_ipport;

	//启动与tracker的交互
	tracker_session::shared_ptr session=tracker_session::create(get_io_service(), server_param_);
	session->start(SHARED_OBJ_FROM_THIS);
	tracker_sessions_.insert(std::make_pair(server_param_->tracker_ipport, session));
}

void server_service::stop()
{
	if (stoped_)
		return;
	stoped_=true;
	if(distributor_)
		distributor_->stop();
	distributor_.reset();

	if(simple_distributor_)
		simple_distributor_->stop();
	simple_distributor_.reset();

	tracker_sessions_.clear();
}

//重置欢迎词
void server_service::reset_welcome(const  std::string& welcome)
{
	BOOST_ASSERT(0);
}
//重置描述
void server_service::reset_discription(const std::string& discription)
{
	BOOST_ASSERT(0);
}

void server_service::distribute(safe_buffer data, const std::string& srcPeerID, 
	int mediaChannelID, int level)
{
	if (stoped_)
		return;
	if (distributor_)
	{
		//生成一个media_packet，设置其media_type
		media_packet pkt;
		pkt.set_channel_id(mediaChannelID);
		pkt.set_level(level);
		//填充数据
		safe_buffer_io io(&pkt.buffer());
		io.write(buffer_cast<char*>(data), data.length());
		//if (b_embedded_in_client_)
		distributor_->distribute(pkt);
	}
}
void server_service::smooth_distribute(safe_buffer data, const std::string& srcPeerID, 
	int mediaChannelID, int level)
{
	if (stoped_)
		return;
	if (distributor_)
	{
		//生成一个media_packet，设置其media_type
		media_packet pkt;
		pkt.set_channel_id(mediaChannelID);
		pkt.set_level(level);
		//填充数据
		safe_buffer_io io(&pkt.buffer());
		io.write(buffer_cast<char*>(data), data.length());
		//if (b_embedded_in_client_)
		distributor_->smooth_distribute(pkt);
	}
}

seqno_t server_service::current_media_seqno()
{
	if (distributor_)
		return distributor_->current_media_seqno();
	return 0;
}

void server_service::distribute(media_packet& pkt)
{
	if (stoped_)
		return;
	if (distributor_)
		distributor_->distribute(pkt);
}
void server_service::smooth_distribute(media_packet& pkt)
{
	if (stoped_)
		return;
	if (distributor_)
		distributor_->smooth_distribute(pkt);
}

int server_service::packet_rate()const
{
	if (distributor_)
		return distributor_->packet_rate();
	if (simple_distributor_)
		return simple_distributor_->packet_rate();

	return 0;
}
double server_service::out_multiple()const
{
	if (distributor_)
		return distributor_->out_multiple();
	
	return 0;
}
int server_service::bitrate()const
{
	if (distributor_)
		return distributor_->bitrate();
	if(simple_distributor_)
		return simple_distributor_->bit_rate();

	return 0;
}
int server_service::out_kbps()const
{
	if (distributor_)
		return distributor_->out_kbps();
	if(sdistributor_scheduling_)
		return sdistributor_scheduling_->out_kbps();

	return 0;
}

double server_service::p2p_efficient()const
{
	double bitRate=bitrate();
	double outRate=out_kbps();
	double cnt=client_count();

	if(cnt)
	{
		double rst=1.0-outRate/(bitRate*cnt+FLT_MIN);
		return ((int)(rst*100+0.5))/100.0;
	}

	return 0;
}

const std::deque<seqno_t>& server_service::iframe_list()const
{
	const static std::deque<seqno_t> null_list;
	if (distributor_)
		return distributor_->iframe_list();
	return null_list;
}

void server_service::on_info_gather_timer()
{
	int clientCount=0;
	double playingQuality=0.0;
	double lostRate=0.0;

	BOOST_FOREACH(const tracker_session_map::value_type& v, tracker_sessions_)
	{
		const tracker_session::shared_ptr& session=v.second;
		clientCount+=session->client_count();
	}
	if (distributor_&&distributor_->total_seed_count()>clientCount)
		clientCount=distributor_->total_seed_count();
	if (clientCount>0)
	{
		BOOST_FOREACH(const tracker_session_map::value_type& v, tracker_sessions_)
		{
			const tracker_session::shared_ptr& session=v.second;
			double a=(double)session->client_count()/double(clientCount);
			playingQuality+=session->playing_quality()*a;
			lostRate+=session->global_remote_to_local_lost_rate()*a;
		}
	}

	client_count_=clientCount;
	server_info_.set_playing_quality((float)playingQuality);
	server_info_.set_global_remote_to_local_lost_rate((float)lostRate);
}
