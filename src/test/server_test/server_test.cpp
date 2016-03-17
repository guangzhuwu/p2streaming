#include "ppc/main_interface.h"
#include "server/server_service.h"
#include "client/tracker_handler.h"

using namespace  p2common;
using namespace  p2server;

boost::condition g_close_condition;
boost::mutex g_close_mutex;

int main(int argc, char* argv[])
{
    boost::mutex::scoped_lock lock(g_close_mutex);

	typedef boost::shared_ptr<server_interface>   server_service_sptr;

	std::vector<server_service_sptr> ssVec;
	ssVec.reserve(4);

	running_service<1> running_service_obj;
	for (int type=INTERACTIVE_LIVE_TYPE;type<=BT_TYPE;++type)
	{

		server_param_base param_;

		param_.type=(distribution_type)type;
		param_.channel_uuid =boost::lexical_cast<std::string>(type);
		param_.external_ipport 
			= param_.internal_ipport
			=std::string("127.0.0.1:")+boost::lexical_cast<std::string>(8090+type);;
		param_.tracker_ipport  
			=std::string("127.0.0.1:")+boost::lexical_cast<std::string>(9080+type);
		param_.channel_key = generate_key_pair().first;

		//param_.film_duration = 60*30*1000; //30min
		//param_.film_length   = 1024*1024*100; //100M

		server_service_sptr ss = p2server::server_service::create(running_service_obj.get_running_io_service(),
			create_server_param_sptr(param_));
		ss->start();
		ssVec.push_back(ss);
	}

	//µÈ´ýÍË³öÐÅºÅ
	g_close_condition.wait(lock);

	return 0;
}
