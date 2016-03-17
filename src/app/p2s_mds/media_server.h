#ifndef __P2S_MDS
#define __P2S_MDS

#include "common/common.h"
#include "p2s_mds/media_relay.h"
#include "server/server_service_logic.h"
#include "tracker/tracker_service_logic.h"
#include "shunt/receiver.h"

using namespace p2engine;
using namespace p2server;
using namespace p2shunt;

//������������������VLC����������������ݺ�ַ�
class p2s_mds
	:public server_service_logic_base
{
	typedef p2s_mds this_type;
	SHARED_ACCESS_DECLARE;

public:
	static shared_ptr create(io_service& ios, 	
		const server_param_base& param, 
		const std::string& fluidistorUrl
		)
	{
		return shared_ptr(new this_type(ios, param, fluidistorUrl), 
			shared_access_destroy<this_type>()
			);
	}

	void start(error_code& ec);
	void reset(error_code& ec);
	void stop(error_code& ec);

protected:
	p2s_mds(io_service& ios,
		const server_param_base& param, 
		const std::string& fluidistorUrl
		);
	virtual ~p2s_mds();

private:
	bool open_distributor(const server_param_base& param);
	void handle_media(const safe_buffer& buf);

private:
	std::string fluidistor_url_;

	//boost::shared_ptr<media_relay> media_relay_;

	boost::shared_ptr<receiver> receiver_;

	udp::endpoint multicast_endpoint_;
	boost::scoped_ptr<udp::socket>  multicast_socket_;

	server_param_base param_;
};

#endif//__P2S_MDS
