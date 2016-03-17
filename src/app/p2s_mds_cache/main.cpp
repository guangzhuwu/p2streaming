#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <p2engine/pop_warning_option.hpp>
#include "p2s_mds_cache/version.h"
#include "p2s_mds_cache/mds_cache_service.h"
#include "common/policy.h"
#include "common/const_define.h"

using namespace p2engine;
using namespace p2common;
using namespace p2cache;

namespace po = boost::program_options;
int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "help messages")
			("version,v", "P2S MDS CACHE version "P2S_MDS_CACHE_VERSION)
			("endpoint", po::value<std::string>(), "set service host port")
			("mlimit", po::value<uint64_t>(), "limit of memery, MB unit")
			;

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) 
		{
			std::cout << desc << "\n";
			return -1;
		}
		else if (vm.count("version"))
		{
			std::cout <<"p2s_mds_cache, version "P2S_MDS_CACHE_VERSION <<"\n";
			return -1;
		}

		std::string end_point;
		uint64_t  mlimit=1;

		if (vm.count("endpoint"))
			end_point=vm["endpoint"].as<std::string>();
		if (vm.count("mlimit"))
			mlimit=vm["mlimit"].as<uint64_t>();

		if(end_point.empty())
		{
			end_point="127.0.0.1:9876";
			std::cout<<"set "<<end_point<<" as default endpoint\n";
			
		}
		if(1==mlimit)
		{
			mlimit=mlimit<<12;
			std::cout<<"set memeory limit="<<mlimit<<" MB \n";
		}
		mlimit=mlimit<<20; //MB to byte

		io_service ios;
		std::string uuid_str="MEMORY_CACHED_SERVICE";
		std::string md5Uid= md5(uuid_str);
		peer_info local_info;
		local_info.set_peer_id(&md5Uid[0], md5Uid.size());
		local_info.set_nat_type(NAT_UNKNOWN);
		local_info.set_peer_type(SERVER);
		local_info.set_join_time((ptime_now()-min_time()).total_milliseconds());
		local_info.set_info_version(0);
		local_info.set_relative_playing_point(0);
		local_info.set_user_info(uuid_str);

		endpoint edp=endpoint_from_string<endpoint>(end_point);
		local_info.set_internal_ip(edp.address().to_v4().to_ulong());
		local_info.set_internal_udp_port(edp.port());
		local_info.set_internal_tcp_port(edp.port());
		local_info.set_external_ip(edp.address().to_v4().to_ulong());
		local_info.set_external_udp_port(edp.port());
		local_info.set_external_tcp_port(edp.port());

		boost::shared_ptr<mds_cache_service> svr=mds_cache_service::create(ios, mlimit/PIECE_SIZE);
		svr->start(local_info);
		ios.run();
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		return -1;
	}
	catch (...)
	{
		std::cout<<"unknown exception"<<std::endl;
		return -1;
	}
	return 0;
};
