#include <p2engine/push_warning_option.hpp>
#include <p2engine/config.hpp>
#include <boost/bind.hpp>
#include <boost/assert.hpp>
#include <boost/lexical_cast.hpp>

#include "natpunch/upnp_punch.h"
#include "libupnp/upnp.hpp"

#include <p2engine/pop_warning_option.hpp>

using namespace p2engine;

namespace natpunch {

	const int Refresh_Threshold = 30;
	const char Client_Name[] = "{C7E8209C-4BF9-47CD-A875-C842D2EFD7E8}";

	upnp_punch::upnp_punch(io_service& svc)
		:basic_engine_object(svc)
		, has_services_(false)
	{
		//post_in_constructor(get_io_service())
		//	.post(fssignal::signal<void(void)>(&upnp_punch::__init, this));
		////get_io_service().post(boost::bind(&upnp_punch::__init, this));
	}

	upnp_punch::~upnp_punch()
	{
		if (timer_)
			timer_->cancel();
		if(upnp_handler_)
		{
			upnp_handler_->close();
			upnp_handler_.reset();
		}
	}

	void upnp_punch::add_port_mapping(const port_mapping&mapping, natpunch_callback callBack)
	{
		get_io_service().post(
			make_alloc_handler(boost::bind(&upnp_punch::__add_port_mapping, SHARED_OBJ_FROM_THIS, mapping, callBack, true))
			);
	}

	void upnp_punch::delete_port_mapping(const port_mapping&mapping)
	{
		get_io_service().post(
			make_alloc_handler(boost::bind(&upnp_punch::__delete_port_mapping, SHARED_OBJ_FROM_THIS, mapping))
			);
	}

	void upnp_punch::delete_all_port_mappings()
	{
		get_io_service().post(
			make_alloc_handler(boost::bind(&upnp_punch::__delete_all_port_mappings, SHARED_OBJ_FROM_THIS))
			);
	}

	bool upnp_punch::has_services() {
		return has_services_;
	}

	void upnp_punch::start() 
	{
		get_io_service().post(
			make_alloc_handler(boost::bind(&upnp_punch::__start, SHARED_OBJ_FROM_THIS))
			);
	}

	void upnp_punch::__start() 
	{
		OBJ_PROTECTOR(holder);
		if (timer_)
			return;

		__discover_devices();

		timer_=p2engine::rough_timer::create(get_io_service());
		timer_->set_obj_desc("natpunch::upnp_punch::timer_");
		timer_->register_time_handler(boost::bind(&upnp_punch::__refresh_callback, this));
		timer_->async_keep_waiting(p2engine::seconds(Refresh_Threshold), 
			p2engine::seconds(Refresh_Threshold));
	}

	void upnp_punch::__add_port_mapping(const port_mapping&portMapping, 
		natpunch_callback callBack, bool insertList) 
	{
		port_mapping mapping=portMapping;
		if (mapping.external_port==0)
			mapping.external_port=mapping.internal_port;

		int res = -1;
		if(upnp_handler_)
			res = upnp_handler_->add_mapping(
			((mapping.protocol == TCP_MAPPING)? libupnp::upnp::tcp : libupnp::upnp::udp)
			, mapping.external_port
			, mapping.internal_port
			);
		if (-1!=res) 
			mapping.enabled = true;
		else 
			mapping.enabled = false;
		if (insertList)
			port_mappings_.insert(mapping);
		if(callBack)
			callBack(mapping);
	}

	void upnp_punch::__delete_port_mapping(const port_mapping&portMapping) 
	{
		typedef std::set<port_mapping, port_mapping_less>::iterator iterator;
		iterator it_pm=port_mappings_.find(portMapping);

		if (it_pm!=port_mappings_.end()) 
		{
			bool ok = true;
			if ((*it_pm).enabled) 
			{
				if(upnp_handler_)
					upnp_handler_->delete_mapping(portMapping.external_port, 
					((portMapping.protocol == TCP_MAPPING)? libupnp::upnp::tcp : libupnp::upnp::udp)
					);
			}
			port_mappings_.erase(it_pm);
		}
	}

	void upnp_punch::__delete_all_port_mappings() 
	{
		if(upnp_handler_ && !port_mappings_.empty())
			upnp_handler_->delete_all_mapping();
		port_mappings_.clear();
	}

	//std::string upnp_punch::get_external_ip() {
	//	char ip[16];
	//	ip[0] = '\0';
	//	if (has_services_) {
	//		int res = UPNP_GetExternalIPAddress(upnp_urls_->controlURL, 
	//			igd_data_->servicetype, ip);
	//		if (res == UPNPCOMMAND_SUCCESS) {
	//			return std::string(ip);
	//		}
	//	}
	//	return "";
	//}
	struct callback_info
	{
		int mapping;
		int port;
		error_code ec;
		bool operator==(callback_info const& e)
		{ return mapping == e.mapping && port == e.port && !ec == !e.ec; }
	};
	void log_callback(char const* err)
	{
#ifdef UPNP_DEBUG
		std::cerr << "UPnP: " << err << std::endl;
#endif
	}
	void on_port_mapping(int mapping, address const& ip, int port, error_code const& err)
	{
		std::cout << "UPNP mapping: " << mapping << ", port: " << port << ", IP: " << ip;
		if (err)
			std::cout << ", error: \"" << err.message() << "\"\n";
		else
			std::cout << ", OK!\n";

		TODO("store the callbacks and verify that the ports were successful.");
		//{
		//	std::list<callback_info> callbacks;
		//	callback_info info = {mapping, port, err};
		//	callbacks.push_back(info);
		//}
	}

	void upnp_punch::__discover_devices() 
	{
		struct interface_not_change 
		{
			bool operator()(const std::vector<ip_interface>&f1, const std::vector<ip_interface>&f2)
			{
				if (f1.size()!=f2.size())
					return false;
				for (size_t i=0;i<f1.size();++i)
				{
					if (f1[i].interface_address!=f2[i].interface_address||
						f1[i].netmask!=f2[i].netmask
						)
					{
						return false;
					}
				}
				return true;
			}
		};

		error_code ec;
		std::vector<ip_interface> interfaces=enum_net_interfaces(get_io_service() , ec);
		if (upnp_handler_&&interface_not_change()(interfaces_, interfaces))
			return;
		interfaces_=interfaces;

		if (upnp_handler_)
		{
			upnp_handler_->delete_all_mapping();
			upnp_handler_->close();
		}
		upnp_handler_.reset(new libupnp::upnp(
			get_io_service(), Client_Name, &on_port_mapping, &log_callback
			));
		upnp_handler_->discover_device(
			boost::bind(&this_type::discover_device_feedback, this, _1)
			);
	}
	void upnp_punch::discover_device_feedback(bool _has_service)
	{	
		has_services_ = _has_service;
	}
	bool __is_igd_connected(libupnp::upnp* upnp_handler) {
		upnp_handler->get_status_info();
		return true;
	}

	void upnp_punch::__refresh_callback() {
		if (!has_services_ || !__is_igd_connected(upnp_handler_.get())) {
			__discover_devices();
		}

		typedef std::set<port_mapping, port_mapping_less>::iterator iterator;
		iterator it_pm=port_mappings_.begin();
		for (;it_pm!=port_mappings_.end();++it_pm) 
		{
			__add_port_mapping(*it_pm);
		}
	}

}  // namespace upnp
