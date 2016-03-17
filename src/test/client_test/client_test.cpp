#include "client/client_service.h"
#include "client/local_param.h"
#include "client/cache_service.h"
#include "common/utility.h"

using namespace  p2client;
using namespace  p2common;

io_service& s_running_service(){
	static boost::shared_ptr<running_service<1> >s_running_service_(new running_service<1>());
	return s_running_service_->get_running_io_service();
}

boost::shared_ptr<client_service> create_client(int type)
{
	using namespace p2client;

	p2common::client_param_base param_;
	//这里频道号设置成与cache的一致，正在播放的频道
	param_.channel_uuid = boost::lexical_cast<std::string>(type);
	param_.tracker_ipport = "127.0.0.1:"+boost::lexical_cast<std::string>(9080+type);
	param_.type = (distribution_type)type;

	std::string key = "default_channel_key";
	std::pair<std::string,std::string> key_pair_ = generate_key_pair();
	md5_byte_t digest[16];
	md5_user_init(key, key_pair_, digest);
	
	param_.channel_key.assign((char*)digest,(char*)digest+16);
	param_.public_key=key_pair_.first;
	param_.private_key=key_pair_.second;

	boost::shared_ptr<client_service> th = client_service::create(s_running_service(), create_client_param_sptr(param_));
	th->start();
	return th;
}

boost::condition g_close_condition;
boost::mutex g_close_mutex;
int main(int argc, char* argv[])
{

	boost::mutex::scoped_lock lock(g_close_mutex);

	int client_cnt=4;

	if(2 == argc) 
	{
		client_cnt = std::atoi(argv[1]);
	}

	{
		int type = VOD_TYPE;
		endpoint trackerEdp = endpoint_from_string<endpoint>("127.0.0.1:"+boost::lexical_cast<std::string>(9080+type));
		error_code ec;
		std::string tracker_host_ = trackerEdp.address().to_string(ec);
		int tracker_port_ = trackerEdp.port();
		peer_info tracker_peer_info_;
		tracker_peer_info_.set_nat_type(NAT_UNKNOWN);

		address addr=address::from_string(tracker_host_,ec);
		if(!ec)
		{
			tracker_peer_info_.set_external_ip(addr.to_v4().to_ulong());
		}

		tracker_peer_info_.set_external_tcp_port(tracker_port_);
		tracker_peer_info_.set_external_udp_port(tracker_port_);

		//创建cache_service
		
		running_service<1> svc;
		p2common::client_param_base param_;
		param_.channel_uuid = boost::lexical_cast<std::string>(type);
		param_.tracker_ipport = "127.0.0.1:"+boost::lexical_cast<std::string>(9080+type);
		param_.type = (distribution_type)type;


		std::string key = "default_channel_key";
		std::pair<std::string,std::string> key_pair_ = generate_key_pair();
		md5_byte_t digest[16];
	    md5_user_init(key, key_pair_, digest);

		param_.channel_key.assign((char*)digest,(char*)digest+16);
		param_.public_key=key_pair_.first;
		param_.private_key=key_pair_.second;

		init_cache_service(s_running_service(), tracker_peer_info_, create_client_param_sptr(param_));
	}

	//创建N个
	std::vector<std::pair<std::string,int> > cache_infoVec;
	std::vector<boost::shared_ptr<client_service> >  client_vec;
	for (int i=0; i<client_cnt; ++i)
	{
		//for (int type=BT_TYPE;type<=BT_TYPE;++type)
		int type=VOD_TYPE;
		{
			p2client::init_static_local_peer_info();
			p2client::change_static_local_peer_id();

			client_vec.push_back(create_client(type));
			cache_infoVec.push_back(std::make_pair(boost::lexical_cast<std::string>(type)
				, random(1,100)));

		}
	}//*/
	boost::shared_ptr<tracker_handler> trackerHandlerIns=get_cache_tracker_handler(s_running_service());

	trackerHandlerIns->cache_changed(cache_infoVec);

	g_close_condition.wait(lock);

	return 0;
}
