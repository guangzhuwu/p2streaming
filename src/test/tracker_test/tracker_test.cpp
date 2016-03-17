
#include "tracker/tracker_service.h"
#include "client/tracker_handler.h"
//#include "boost/signal.hpp"

boost::condition g_close_condition;
boost::mutex g_close_mutex;


int main(int argc, char* argv[])
{
	boost::mutex::scoped_lock lock(g_close_mutex);

	using namespace  p2tracker;

	std::vector<boost::shared_ptr<tracker_service> > tsVec;
	running_service<1> running_service_obj;
	for (int type=INTERACTIVE_LIVE_TYPE;type<=BT_TYPE;++type)
	{
		tracker_param_base param_;
		param_.type = (distribution_type )type;
		param_.external_ipport 
			= param_.internal_ipport
			=std::string("127.0.0.1:")+boost::lexical_cast<std::string>(9080+type);
		param_.aaa_key = "default_channel_key";//这里的私钥与client使用的一致，否则无法login

		boost::shared_ptr<tracker_service> ts = tracker_service::create(
			running_service_obj.get_running_io_service(), create_tracker_param_sptr(param_)
			);
		ts->start();
		tsVec.push_back(ts);
	}

	//等待退出信号
	g_close_condition.wait(lock);

	return 0;
}
int stop_service(void)
{
	g_close_condition.notify_all();
	return 0;
}
