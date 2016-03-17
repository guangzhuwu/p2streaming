#include "p2s_mds/auth.h"
#include "p2s_mds/version.h"
#include "p2s_mds/cmd_receiver.h"
#include "p2s_mds/utility.h"

#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>   
#include <boost/property_tree/xml_parser.hpp>
#include <p2engine/pop_warning_option.hpp>

#if defined (BOOST_MSVC) && defined(P2ENGINE_ENABLE_SSL)
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#	pragma comment(lib, "User32.lib")
#endif

namespace po = boost::program_options;
namespace cplxml = boost::property_tree::xml_parser ;
using namespace p2engine;
using namespace p2control;
using namespace utility;


#ifdef P2ENGINE_DEBUG
#define MDS_DEBUG(x) /*x*/
#else
#define MDS_DEBUG(x)
#endif

const std::string s_server_key = "server";

int main(int argc, char* argv[])
{
	int  g_server_id, g_type;
	boost::shared_ptr<mds_cmd_receiver> g_mds_ptr;
	try
	{
		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h", "help messages")
			("version,v", "version " P2S_MDS_VERSION)
			("server_id", po::value<int>(), "set id of mds(required)")
			("type", po::value<int>(), "set type of mds(required)")
			("alive_alarm_port", po::value<int>(), "set alive alarm port of mds(required)")
			("register_code", po::value<std::string>(), "set register code of mds(required)")
			;

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help")) 
		{
			std::cout << desc << "\n";
			return -1;
		}
		if (vm.count("version"))
		{
			std::cout <<"p2s_mds, version "P2S_MDS_VERSION <<"\n";
			return -1;
		}

		int server_id=-1;
		int type=-1;
		int alive_alarm_port = 0;
		std::string regist_code;
		if (vm.count("server_id"))
			server_id = vm["server_id"].as<int>();
		if (vm.count("type"))
			type = vm["type"].as<int>();
		if (vm.count("alive_alarm_port"))
			alive_alarm_port = vm["alive_alarm_port"].as<int>();
		if (vm.count("register_code"))
			regist_code = vm["register_code"].as<std::string>();
		
		g_server_id = server_id;
		g_type = type;

		//LogInfo("\n server_id=%d, type=%d, db_file=%s", server_id, type, db_file.c_str());

		if(server_id<0||type<0||alive_alarm_port<=0)
		{
			BOOST_ASSERT_MSG(0, "server id or service type invalide!");
			return -1;
		}
		
		if(regist_code.empty())
		{
			BOOST_ASSERT_MSG(0, "regist code is empty!");
			return -1;
		}

		MDS_DEBUG(
			std::cout<<"~~~~~~~~~~~~~~~~~~~~~~~~~server :"<<server_id<<" running..."<<std::endl;
			std::cout<<" regist code: "<<regist_code
				<<"\n ~~~~~~~~~~~~~~~~~~~~~~~~~~~"
				<<std::endl;
		);

		io_service ios;
		boost::shared_ptr<mds_cmd_receiver> cmd_recv_ptr = mds_cmd_receiver::create(ios);
		cmd_recv_ptr->set_alive_alarm(alive_alarm_port, regist_code);
		cmd_recv_ptr->start(server_id, type);
		g_mds_ptr = cmd_recv_ptr;
		ios.run();
	}
	catch (std::exception& e)
	{
		boost::format fmt("mds crashed, type=%d, server_id=%d, error=%s");
		fmt%g_type%g_server_id%e.what();

		std::cout<<fmt.str()<<std::endl;

		if(g_mds_ptr)
		{
			g_mds_ptr->post_error_msg(fmt.str());
		}
		LogError("mds crashed, type=%d, server_id=%d, error=%s", g_type, g_server_id, e.what());
	}
	catch (...)
	{
		LogError("mds crashed, type=%d, server_id=%d, error=unknown", g_type, g_server_id);
		return -1;
	}
	return 0;
};
