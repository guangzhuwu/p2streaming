#ifndef __COMMON_AUTH
#define __COMMON_AUTH

#include <string>

#include <p2engine/p2engine.hpp>

namespace p2control{
	class interprocess_client;
}
namespace p2shunt{
	class p2sshunt;
}

using namespace p2engine;
using namespace p2shunt;
using namespace p2control;

class progress_alive_alarm
	:public boost::enable_shared_from_this < progress_alive_alarm >
{
	typedef progress_alive_alarm this_type;
	typedef http::basic_http_connection<http::http_connection_base> http_connection;
	SHARED_ACCESS_DECLARE;

protected:
	progress_alive_alarm(const std::string&id, p2sshunt& shunt, int guardPort);
	virtual ~progress_alive_alarm();

public:
	static shared_ptr create(const std::string&id, p2sshunt& shunt, int guardPort)
	{
		return shared_ptr(new this_type(id, shunt, guardPort),
			shared_access_destroy<this_type>()
			);
	}
	void start(boost::shared_ptr<interprocess_client>);

protected:
	void on_timer();

private:
	p2sshunt&	 shunt_;
	boost::shared_ptr<rough_timer> timer_;
	boost::weak_ptr<interprocess_client> interprocess_client_;
};

#endif
