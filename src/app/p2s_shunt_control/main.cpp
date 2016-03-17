#include "p2s_shunt_control/version.h"
#include "p2s_shunt_control/shunt_control.h"
#include "app_common/app_common.h"

#include <p2engine/push_warning_option.hpp>
#include <vector>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/unordered_set.hpp>
#include <p2engine/pop_warning_option.hpp>

#include <p2engine/timer.hpp>

#ifdef _WIN32
#	include <Windows.h>
#endif

#ifdef BOOST_MSVC
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#	pragma comment(lib, "User32.lib")
#endif

using namespace  p2engine;
using namespace  p2control;
using namespace  p2common;
namespace po = boost::program_options;
namespace fs=boost::filesystem;

fs::path current_path()
{
#ifdef _WIN32
	std::vector<char> szPath;
	szPath.resize(1024);
	int nCount = GetModuleFileName(NULL, &szPath[0], MAX_PATH);
	return fs::path(std::string(&szPath[0], nCount)).parent_path();
#else
	return fs::current_path();
#endif
}

int main(int argc, char* argv[])
{
	try
	{
		clear_instance_exist();

		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h", "help messages")
			("version,v", "version " P2S_SHUNT_CONTROL_VERSION)
			("alive_alarm_port", po::value<int>(), "set alive alarm port(reserved)")
			("operation_http_port", po::value<int>(), "set cmd recv port(required)")
			("shm_name", po::value<std::string>(), "set shared memory name(optional default shuntShareMemory)")
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
			std::cout <<"p2s_shunt_control, version "P2S_SHUNT_CONTROL_VERSION <<"\n";
			return -1;
		}

		int alive_alarm_port=0;
		int cmd_recv_port = 0;
		std::string cmd_channel;
		std::string shm_name;

		if (vm.count("alive_alarm_port"))
			alive_alarm_port=vm["alive_alarm_port"].as<int>();
		else 
		{
			std::cout << desc << "\n";
			std::cerr<<"alive alarm port required!"<<std::endl;
			return -1;
		}

		if(vm.count("operation_http_port"))
			cmd_recv_port = vm["operation_http_port"].as<int>();
		else 
		{
			std::cout << desc << "\n";
			std::cerr<<"operation http port required!"<<std::endl;
			return -1;
		}
		fs::path cur_path = current_path();
#ifdef BOOST_WINDOWS_API
		//���õ�ǰ·����ϵͳ��������
		std::vector<char> path_buffer;
		path_buffer.resize(2048);
		int nCount = GetEnvironmentVariable("Path", &path_buffer[0], path_buffer.size());
		if(nCount>0)
		{
			std::string path;
			path.append(&path_buffer[0], nCount);
			path.append(";");
			
			if(!cur_path.empty())
			{
				path.append(cur_path.string().c_str(), cur_path.string().length());
				SetEnvironmentVariable("Path", path.c_str());
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
		get_config_value("database.databaseName", dbName.value, errorMsg);
		get_config_value("database.hostName", hostName.value, errorMsg);
		get_config_value("database.userName", userName.value, errorMsg);
		get_config_value("database.passWords", pwd.value, errorMsg);

		if(exe_file.empty())
			exe_file.assign("p2s_shunt.exe");

		control_base::shared_ptr shunt_supervise =  shunt_control::create(ios);
		shunt_supervise->start(dbName, hostName, userName, pwd, 
			exe_file.c_str(), alive_alarm_port, cmd_recv_port);
		ios.run();
	}
	catch(std::exception& e)
	{
		LogInfo("error, msg=%s", e.what());
		std::cout<<"p2s_shunt_control error: "<<e.what()<<std::endl;
		return -1;
	}
	catch (...)
	{
		LogInfo("shunt control error, msg=unknown error");
		return -1;
	}
	return 0;
}
