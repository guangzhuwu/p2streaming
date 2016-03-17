#include "client/nat.h"

#include <p2engine/config.hpp>
#include <p2engine/push_warning_option.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/spinlock.hpp>

#include "client/typedef.h"
#include "client/stun/stun.h"

namespace p2client
{
	static int local_nat_type=StunTypeUnknown;
	static bool detect_running=false;
	static p2engine::spinlock detect_mutex;
	static boost::shared_ptr<boost::thread> detect_thread;
#ifdef WIN32
	static const char * nat_file="nat_type";
#else
	static const char * nat_file="/tmp/nat_type";
#endif
	static const char* stunservers[]={
		"stunserver.org", 
		"stun.ekiga.net", 
		"stun.ideasip.com", 
		"stun.softjoys.com", 
		"stun.voipbuster.com", 
		"stun.voxgratia.org", 
		//"stun.xten.com", 
		"stun.sipgate.net:10000", 
		"stun.ipshka.com"
	};

	void detect_nat_type()
	{
		try{

			StunAddress4 stunServerAddr;

			for (size_t i=0;i<sizeof(stunservers)/sizeof(stunservers[0]);++i)
			{
				stunParseServerName((char*)stunservers[i], &stunServerAddr);

				bool verbose = false;
				StunAddress4 sAddr;
				sAddr.port = 0;
				sAddr.addr = 0;
				bool_t preservePort;
				bool_t hairpin;
				int port=0;

				NatType stype = stunNatType( &stunServerAddr, &preservePort, &hairpin, port, &sAddr );
				switch (stype)
				{
				case StunTypeFailure:
					break;
				case StunTypeOpen:
				case StunTypeRestrictedNat:
				case StunTypePortRestrictedNat:
				case StunTypeSymNat:
				case StunTypeSymFirewall:
				case StunTypeBlocked:
					try{
						local_nat_type=stype;
						using boost::property_tree::ptree;
						std::string type=boost::lexical_cast<std::string>(stype);
						std::string timestamp=boost::lexical_cast<std::string>(::time(NULL));
						ptree pt;
						pt.put("type", type);  
						pt.put("timestamp", timestamp);  
						write_ini(nat_file, pt); 
					} 
					catch (...){
					}
					return;
				default:
					break;
				}
			}
		}
		catch(...) 
		{
		}
	}

	static void __set_local_nat_type(int t)
	{
		local_nat_type=t;
	}

	static int __get_local_nat_type()
	{
		try{
			using boost::property_tree::ptree;
			if (boost::filesystem::exists(nat_file))
			{
				ptree pt;
				read_ini(nat_file, pt); 
				int type = pt.get<int>("type");
				time_t timestamp = pt.get<time_t>("timestamp");
				if ((timestamp+3600)>=time(NULL))
					__set_local_nat_type(type);
			}
		}
		catch(...) 
		{
		}
		try{
			if (local_nat_type==StunTypeUnknown)
			{
				p2engine::spinlock::scoped_lock lock(detect_mutex);
				if (detect_running==false)
				{
					detect_running=true;
					if (!detect_thread)
					{
						detect_thread.reset(new boost::thread(boost::bind(&detect_nat_type)));
						detect_thread->detach();
					}
				}
			}
		}
		catch(...) 
		{
		}
		return local_nat_type;
	}

	int get_local_nat_type()
	{
		int t=__get_local_nat_type();
		switch(t)
		{
		case StunTypeUnknown:
		case StunTypeFailure:
			return NAT_UNKNOWN;	

		case StunTypeOpen:
		case StunTypeConeNat:
			return NAT_OPEN_OR_FULL_CONE;	

		case StunTypeRestrictedNat:
			return NAT_IP_RESTRICTED;	

		case StunTypePortRestrictedNat:
			return NAT_IP_PORT_RESTRICTED;	

		case StunTypeSymNat:
			return NAT_SYMMETRIC_NAT;	

		case StunTypeBlocked:
		case StunTypeSymFirewall:
			return NAT_UDP_BLOCKED;

		default:
			BOOST_ASSERT(0);
			return NAT_UNKNOWN;
		}
	}
}

