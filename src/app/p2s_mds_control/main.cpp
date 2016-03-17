#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <p2engine/pop_warning_option.hpp>
#include <p2engine/p2engine.hpp>

#include "app_common/app_common.h"
#include "p2s_mds_control/mds_control.h"
#include "p2s_mds_control/version.h"

#if defined (BOOST_MSVC) && defined(P2ENGINE_ENABLE_SSL)
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#	pragma comment(lib, "User32.lib")
#endif

namespace po = boost::program_options;
namespace fs=boost::filesystem;
using namespace p2control;
using namespace p2common;

/************************************************************************/
/*                                                                      */
/************************************************************************/

int main(int argc, char* argv[])
{
	try
	{
		clear_instance_exist();

		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h", "help messages")
			("version,v", "version " P2S_MDS_CONTROL_VERSION)
			("alive_alarm_port", po::value<int>(), "set alive alarm port(required)")
			("operation_http_port", po::value<int>(), "set cmd recv port(required)")
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
			std::cout <<"p2s_mds_control, version "P2S_MDS_CONTROL_VERSION <<"\n";
			return -1;
		}

		int alive_alarm_port=0;
		int cmd_recv_port = 0;
		std::string cmd_channel;

		if(vm.count("operation_http_port"))
		{
			cmd_recv_port = vm["operation_http_port"].as<int>();
			if( cmd_recv_port < 0 )
			{
				std::cout << desc << "\n";
				std::cerr<<"operation http port invalid!"<<std::endl;
				return -1;
			}
		}
		else 
		{
			std::cout << desc << "\n";
			std::cerr<<"operation http port required!"<<std::endl;
			return -1;
		}

		if(vm.count("alive_alarm_port"))
			alive_alarm_port = vm["alive_alarm_port"].as<int>();
		if(alive_alarm_port<=0)
			alive_alarm_port=8000;

		fs::path cur_path = utility::current_path();

#ifdef BOOST_WINDOWS_API
		//���õ�ǰ·����ϵͳ��������
		if ( !cur_path.empty() )
		{
			enum { max_env_length = 2048 };
			char path_buffer[ max_env_length ] = "";
			if ( GetEnvironmentVariable("Path", path_buffer, max_env_length) > 0 )
			{
				std::string path_buffer_str( path_buffer );
				path_buffer_str.append( ";" + cur_path.string() );
				SetEnvironmentVariable("Path", path_buffer_str.c_str());
			}
		}
		
#else
		std::string pathKey="PATH";
		std::string path=getenv(pathKey.c_str());
		path.append(":");
		path.append(cur_path.c_str(), cur_path.string().length());

		setenv(pathKey.c_str(), path.c_str(), true);
#endif
		io_service ios;
		std::string exe_file, errorMsg;
		p2control::db_name_t dbName;
		p2control::host_name_t hostName;
		p2control::user_name_t userName;
		p2control::password_t  pwd;

		get_config_value("app.sub_process", exe_file, errorMsg);
		if(exe_file.empty())
			exe_file.assign("p2s_mds_3.exe");

		if( !( get_config_value("database.databaseName", dbName.value, errorMsg) &&
				get_config_value("database.hostName", hostName.value, errorMsg) &&
				get_config_value("database.userName", userName.value, errorMsg) &&
				get_config_value("database.passWords", pwd.value, errorMsg) ) )
		{
			std::cerr << "parse config.ini failed" << std::endl;
			return -1;
		}

		control_base::shared_ptr mds_supervise =  mds_control::create(ios);
		
		mds_supervise->start(dbName, 
			hostName, userName, pwd, exe_file.c_str(), 
			alive_alarm_port, cmd_recv_port);
		
		ios.run();
	}
	catch(std::exception& e)
	{
		LogInfo("error, msg=%s", e.what());
		std::cout<<"p2s_mds_control error: "<<e.what()<<std::endl;
		
		return -1;
	}
	catch (...)
	{
		LogInfo("control error, msg=unknown error");
		return -1;
	}
	return 0;
}
