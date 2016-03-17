#include "shunt/media_sender.h"
#include "tracker/tracker_service_logic.h"

#include <p2engine/push_warning_option.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace p2tracker{
	class tracker_for_sender
		:public tracker_service_logic_base
	{
		typedef tracker_for_sender this_type;
		SHARED_ACCESS_DECLARE;

	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>()
				);
		}

	protected:
		tracker_for_sender(io_service& svc)
			:tracker_service_logic_base(svc){}
		virtual ~tracker_for_sender(){}

	public:
		//网络消息处理
		virtual void register_message_handler(message_socket*){};
		virtual void known_offline(peer*){};
		virtual bool permit_relay(peer*, const relay_msg&){return true;};
		virtual void recvd_peer_info_report(peer* , const p2ts_quality_report_msg&){};
	};
}

namespace p2shunt{

	const std::string default_channel_key="default_channel_key";
	const std::string default_channel_uuid="default_channel_uuid";

	class server_for_sender
		:public server_service_logic_base
	{
		typedef server_for_sender this_type;
		SHARED_ACCESS_DECLARE;
	public:
		static shared_ptr create(io_service& ios)
		{
			return shared_ptr(new this_type(ios), 
				shared_access_destroy<this_type>()
				);
		}
		protected:
		server_for_sender(io_service& ios)
			:server_service_logic_base(ios){}
	};

	media_sender::media_sender(io_service& ios)
		:sender(ios)
	{
	}

	media_sender::~media_sender()
	{
		if (server_interface_)
			server_interface_->stop();
		if (tracker_interface_)
			tracker_interface_->stop();
	}

	bool media_sender::updata(const std::string& url, error_code& ec)
	{
		uri u(url, ec);
		if (ec)
			return false;

		if("shunt"!=u.protocol())
			return false;

		bool restart=false;

		server_param_base serverParam;
		serverParam.channel_uuid=default_channel_uuid;
		serverParam.channel_link=default_channel_uuid;
		serverParam.type=LIVE_TYPE;
		{
			std::string ip=u.host();
			if (ip.empty())
				ip="0.0.0.0";
			int port=u.port();
			variant_endpoint edp(asio::ip::address::from_string(ip, ec), port);

			bool changed=false;

			if (server_interface_&&the_edp_==edp)
			{
			}
			else
			{
				the_edp_=edp;
				restart=true;
			}
			serverParam.internal_ipport=endpoint_to_string(edp);
			serverParam.channel_key=u.query_map()["key"];
		}
		{
			std::string external_edp=u.query_map()["external_address"];
			if(!external_edp.empty())
			{
				variant_endpoint edp=endpoint_from_string<endpoint>(external_edp);
				bool changed=false;
				if (server_interface_&&the_extrenal_edp_==edp)
				{
				}
				else
				{
					the_extrenal_edp_=edp;
					restart=true;
				}
				serverParam.external_ipport=external_edp;
			}
			else
			{
				serverParam.external_ipport=serverParam.internal_ipport;
			}
			
		}
		if (restart)
		{
			tracker_param_base trackerParam;
			trackerParam.type=serverParam.type;
			trackerParam.aaa_key=serverParam.channel_key;
			trackerParam.internal_ipport=serverParam.internal_ipport;
			trackerParam.external_ipport=serverParam.external_ipport;
			trackerParam.b_for_shunt=true;

			serverParam.tracker_ipport=trackerParam.internal_ipport;

			if (tracker_interface_)
				tracker_interface_->stop();
			tracker_interface_=tracker_for_sender::create(get_io_service());
			tracker_interface_->start(trackerParam);

			if (server_interface_)
				server_interface_->stop();
			server_interface_=server_for_sender::create(get_io_service());
			server_interface_->start(serverParam, ec);
		}
		return false;
	}

	void media_sender::shunt(const safe_buffer& pkt)
	{
		server_interface_->distribute(pkt, "", 0, USER_LEVEL);
	}
}