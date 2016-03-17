#include "natpunch/upnp_punch.h"
#include "natpunch/auto_mapping.h"

using namespace p2engine;

namespace natpunch {

	static boost::recursive_mutex s_mutex_;
	static std::pair<int, int> s_udp_mapping_=std::make_pair(0, 0);
	static std::pair<int, int> s_tcp_mapping_=std::make_pair(0, 0);

	struct out_scope_print{
		out_scope_print(const std::string& func)
		{
			DEBUG_SCOPE(
				func_=func;
			std::cout<<"------------> IN "<<func<<std::endl;
			)
		}
		~out_scope_print()
		{
			DEBUG_SCOPE(
				std::cout<<"------------> OUT "<<func_<<std::endl;
			)
		}
		DEBUG_SCOPE(
			std::string func_;
		);
	};

#define  OUT_SCOPE_PRINT(func) //out_scope_print out_scope_print__(func);

	std::pair<int, int> get_udp_mapping()
	{
		OUT_SCOPE_PRINT("get_udp_mapping");
		boost::recursive_mutex::scoped_lock lock(s_mutex_);
		return s_udp_mapping_;

	}

	std::pair<int, int> get_tcp_mapping()
	{
		OUT_SCOPE_PRINT("get_tcp_mapping");
		boost::recursive_mutex::scoped_lock lock(s_mutex_);
		return s_tcp_mapping_;

	}

	void __on_port_mapping(const port_mapping& mapping)
	{
		OUT_SCOPE_PRINT("__on_port_mapping");
		boost::recursive_mutex::scoped_lock lock(s_mutex_);
		if (mapping.enabled)
		{
			if (!is_local(mapping.address)&&!is_loopback(mapping.address)&&!is_any(mapping.address))
			{
				if (mapping.protocol==UDP_MAPPING)
				{
					s_udp_mapping_.first=mapping.internal_port;
					s_udp_mapping_.second=mapping.external_port;

					DEBUG_SCOPE(
						std::cout<<"UPNP  UDP_MAPPING: "
						<<mapping.internal_port<<" --> "<<mapping.external_port
						<<std::endl;
					);
				}
				if (mapping.protocol==TCP_MAPPING)
				{
					s_tcp_mapping_.first=mapping.internal_port;
					s_tcp_mapping_.second=mapping.external_port;
					DEBUG_SCOPE(
						std::cout<<"UPNP  TCP_MAPPING: "
						<<mapping.internal_port<<" --> "<<mapping.external_port
						<<std::endl;
					);
				}
			}
		}
	}

	void start_auto_mapping(p2engine::io_service& ios)
	{
		OUT_SCOPE_PRINT("start_auto_mapping");
		boost::recursive_mutex::scoped_lock lock(s_mutex_);
		static boost::shared_ptr<upnp_punch> s_upnp_punch_sptr;
		static io_service* io_service_=NULL;
		if (s_upnp_punch_sptr&&io_service_==&ios)
			return;

		//std::cout<<"start_auto_mapping ..."<<std::endl;

		io_service_=&ios;
		s_upnp_punch_sptr.reset(new upnp_punch(ios));
		s_upnp_punch_sptr->start();
		//std::cout<<"s_upnp_punch_sptr ok"<<std::endl;

		//打开dummy acceptor, 尽量让tcp和udp端口一样
		static boost::shared_ptr<urdp_acceptor> s_dummy_urdp_acceptor_;
		static boost::shared_ptr<trdp_acceptor> s_dummy_trdp_acceptor_;
		endpoint edp;
		error_code ec;
		for(int i=0;i<10;++i)
		{
			edp.port(0);
			if (s_dummy_urdp_acceptor_)
				s_dummy_urdp_acceptor_->close(ec);
			if (s_dummy_trdp_acceptor_)
				s_dummy_trdp_acceptor_->close(ec);
			s_dummy_trdp_acceptor_=trdp_acceptor::create(ios, true);
			s_dummy_trdp_acceptor_->listen(edp, "dymmy_acceptor", ec);
			edp=s_dummy_trdp_acceptor_->local_endpoint(ec);
			int port=edp.port();
			s_dummy_urdp_acceptor_=urdp_acceptor::create(ios, true);
			s_dummy_urdp_acceptor_->listen(edp, "dymmy_acceptor", ec);
			edp=s_dummy_urdp_acceptor_->local_endpoint(ec);
			if (port==edp.port())
			{
				//std::cout<<"local port udp=tcp="<<port<<std::endl;
				s_udp_mapping_.first=s_udp_mapping_.first=port;
				break;
			}
		}
		if(s_udp_mapping_.first==0)
			s_udp_mapping_.first=s_dummy_urdp_acceptor_->local_endpoint(ec).port();
		if(s_tcp_mapping_.first==0)
			s_tcp_mapping_.first=s_dummy_trdp_acceptor_->local_endpoint(ec).port();
		//std::cout<<"local port udp="<<s_udp_mapping_.first<<", tcp="<<s_tcp_mapping_.first<<std::endl;

		///*
		natpunch::port_mapping udpMapping(s_udp_mapping_.first, s_udp_mapping_.first, UDP_MAPPING);
		s_upnp_punch_sptr->add_port_mapping(udpMapping, boost::bind(&__on_port_mapping, _1));
		natpunch::port_mapping tcpMapping(s_tcp_mapping_.first, s_tcp_mapping_.first, TCP_MAPPING);
		s_upnp_punch_sptr->add_port_mapping(tcpMapping, boost::bind(&__on_port_mapping, _1));

		//std::cout<<"start_auto_mapping ok"<<std::endl;
	}
};
