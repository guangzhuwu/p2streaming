#ifndef MDS_PROGRESS_ALIVE_ALARM_H__
#define MDS_PROGRESS_ALIVE_ALARM_H__

#include "p2s_mds/media_server.h"
#include <p2engine/push_warning_option.hpp>
#include <string>
#include <p2engine/pop_warning_option.hpp>

namespace p2control{
	class interprocess_client;
}

using namespace p2engine;
using namespace p2control;

class progress_alive_alarm
	:public basic_engine_object
{
	typedef progress_alive_alarm this_type;
	typedef http::http_connection http_connection;
	typedef boost::function<void(const safe_buffer&)>  on_alarm_handler;
	SHARED_ACCESS_DECLARE;

protected:
	progress_alive_alarm(boost::shared_ptr<server_service_logic_base> s, 
		const time_duration& interval, const std::string& id, int type, int guardPort
		);
	~progress_alive_alarm();

public:
	static shared_ptr create(boost::shared_ptr<server_service_logic_base> s
		, const time_duration& interval, const std::string& id, int type, int guardPort
		)
	{
		return shared_ptr(new this_type(s, interval, id, type, guardPort), 
			shared_access_destroy<this_type>()
			);
	}
	void reset_alarm_port(int port);
	void start(boost::shared_ptr<interprocess_client>, error_code& ec);
	void stop();
	void reset(error_code& ec);
	bool is_stoped()const{return !timer_;}

protected:
	void on_timer();
	void on_quality_reported(const safe_buffer& buf);

protected:
	boost::shared_ptr<rough_timer> timer_;

	//���ؽ��̵�http����
	udp::socket guard_socket_;
	udp::endpoint guard_remote_edp_;

	std::string id_;
	int         type_;
	boost::shared_ptr<server_service_logic_base> server_;
	boost::weak_ptr<interprocess_client> interprocess_client_;

	int interval_;
	ptime last_alarm_time_;
};

#endif//MDS_PROGRESS_ALIVE_ALARM_H__

