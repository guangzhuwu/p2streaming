#ifndef UPNP_UPNPCFG_H__
#define UPNP_UPNPCFG_H__

#include <p2engine/p2engine.hpp>
#include <p2engine/push_warning_option.hpp>
#include <boost/function.hpp>
#include <boost/asio/ip/address.hpp>
#include <string>
#include <map>
#include <p2engine/pop_warning_option.hpp>

namespace natpunch {
	enum protocol_type {
		UDP_MAPPING = 0, 
		TCP_MAPPING = 1
	};

	struct port_mapping {
		port_mapping(int internalPort, protocol_type protocolType)
			: internal_port(internalPort), 
			external_port(internalPort), 
			protocol(protocolType), 
			enabled(false)
		{}

		port_mapping(int internalPort, int externalPort, protocol_type protocol)
			: internal_port(internalPort), 
			external_port(externalPort), 
			protocol(protocol), 
			enabled(false)
		{}

		port_mapping()
			:enabled(false)
		{
		}

		boost::asio::ip::address address;
		int internal_port;
		int external_port;
		protocol_type protocol;
		bool enabled;
	};

	typedef boost::function<void(const port_mapping&)> natpunch_callback;

}  // namespace natpunch

#endif  // UPNP_UPNPCFG_H__
