#ifndef UPNP_MINIUPNPCLIENTIMPL_H__
#define UPNP_MINIUPNPCLIENTIMPL_H__

#include <p2engine/p2engine.hpp>

#include <p2engine/push_warning_option.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <string>
#include <list>
#include <map>
#include <p2engine/pop_warning_option.hpp>

#include "natpunch/port_mapping.h"

namespace  libupnp{
	class upnp;
};
namespace natpunch {

	class upnp_punch 
		:public p2engine::basic_engine_object
	{
		typedef upnp_punch this_type;

	public:
		upnp_punch(io_service& svc);
		virtual ~upnp_punch();
		// starts up the UPnP control point, including device discovery
		void start();

		// schedules a port mapping for registration with known and future services
		void add_port_mapping(const port_mapping&, natpunch_callback callBack);
		// removes the mapping from the internal list and all known services
		void delete_port_mapping(const port_mapping&);
		void delete_all_port_mappings();

	protected:
		// returns true if suitable services have been found
		bool has_services();
		void discover_device_feedback(bool _has_service);



		// schedules a port mapping for registration with known and future services
		void __add_port_mapping(const port_mapping&, 
			natpunch_callback callBack=natpunch_callback(), bool insertList=false);
		// removes the mapping from the internal list and all known services
		void __delete_port_mapping(const port_mapping&);
		// removes all mappings
		void __delete_all_port_mappings();

		// retrieves the external IP address from the device
		//std::string get_external_ip();

	private:
		upnp_punch(const upnp_punch&);
		upnp_punch& operator=(const upnp_punch&);

		void __start();
		void __discover_devices();
		void __refresh_callback();

		bool has_services_;

		struct port_mapping_less
		: std::binary_function<port_mapping, port_mapping, bool>
		{
			bool operator()(const port_mapping& pm1, const port_mapping&pm2)const
			{
				if (pm1.protocol!=pm2.protocol)
					return pm1.protocol<pm2.protocol;
				else if(pm1.internal_port!=pm2.internal_port)
					return pm1.internal_port<pm2.internal_port;
				else if(pm1.external_port!=pm2.external_port)
					return pm1.external_port<pm2.external_port;
				else if(pm1.address!=pm2.address)
					return pm1.address<pm2.address;
				return false;
			}
		};
		std::set<port_mapping, port_mapping_less> port_mappings_;

		boost::shared_ptr<p2engine::rough_timer> timer_;
		boost::intrusive_ptr<libupnp::upnp> upnp_handler_;
		std::vector<p2engine::ip_interface> interfaces_;
	};
}  // namespace natpunch

#endif  // UPNP_MINIUPNPCLIENTIMPL_H__
