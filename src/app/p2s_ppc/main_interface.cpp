#include "p2s_ppc/main_interface.h"
#include "p2s_ppc/server.hpp"
#include "p2s_ppc/version.h"

#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>
#include <p2engine/pop_warning_option.hpp>

namespace po = boost::program_options;
using namespace ppc;
#ifdef __cplusplus
extern "C" {
#endif
	boost::mutex& s_mutex()
	{
		static boost::mutex _close_mutex;
		return _close_mutex;
	}

	io_service& s_io_service()
	{
		static io_service _io_service;
		return _io_service;
	}

	int start_service(int argc, char* argv[])
	{
		//typedef boost::posix_time::ptime ptime;
		//if (boost::date_time::microsec_clock<ptime>::universal_time()
		//	>boost::posix_time::time_from_string("2012-12-25 00:00:00.000")
		//	||
		//	boost::date_time::microsec_clock<ptime>::universal_time()
		//	<boost::posix_time::time_from_string("2012-04-24 00:00:00.000")
		//	)
		//{
		//	return 0;
		//}

		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h", "help messages")
			("version,v", "print version string. version "P2S_PPC_VERSION)
			("port", po::value<int>(), "set http acceptor port, default=9906")
			("delay", po::value<int>(), "set delay guarantee(ms), default=4000")
			("cache_directory", po::value<std::string>(), "play cache directory")
			("cache_file_size", po::value<size_t>(), "cache file size")
			("xorkey", po::value<int>(), "set xorkey, xorkey is used to decode channel_key using XOR")
			("ana_host", po::value<std::string>(), "set analytics server, default=analitics.568tv.hk:10000")
			("ext_ana_host", po::value<std::string>(), "set extra analytics server list, use ',' as separator, default=null")
			("operators", po::value<std::string>(), "set operators, default=P2S")
			;
		po::variables_map vm;
		try
		{
			po::store(po::parse_command_line(argc, argv, desc), vm);
			po::notify(vm);   
		}
		catch (std::exception* e)
		{
			std::cout<<e->what()<<std::endl;
			return -1;
		}
		catch (...)
		{
			std::cout<<"parse command line error!"<<std::endl;
			return -1;
		}

		if (vm.count("help")) {
			std::cout << desc << "\n";
			return -1;
		}
		else if (vm.count("version"))
		{
			std::cout <<"p2s_ppc, version "P2S_PPC_VERSION <<"\n";
			return -1;
		}

		try
		{
			int port=9906;
			std::string cache_dir = "";
			size_t cache_file_size = 2*1024*1024*1024LL-100*1024*1024LL;
			std::string pas_host = "";
			std::string ext_pas_host = "";
			std::string operators("P2S");

			if (vm.count("port")) 
				port=vm["port"].as<int>();
			if (vm.count("delay"))
				g_delay_guarantee=vm["delay"].as<int>();
			if (vm.count("xorkey"))
				g_xor_key=vm["xorkey"].as<int>();
			if(vm.count("cache_directory"))
				cache_dir = vm["cache_directory"].as<std::string>();
			if(vm.count("cache_file_size"))
				cache_file_size = vm["cache_file_size"].as<size_t>();
			if(g_back_fetch_duration&&g_delay_guarantee&&*g_back_fetch_duration<*g_delay_guarantee)
				*g_back_fetch_duration=*g_delay_guarantee;
			if(vm.count("ana_host"))
				pas_host = vm["ana_host"].as<std::string>();
			if(vm.count("ext_ana_host"))
				ext_pas_host = vm["ext_ana_host"].as<std::string>();
			if(vm.count("operators"))
				operators = vm["operators"].as<std::string>();
			ppc::set_operators(operators);

			boost::shared_ptr<p2sppc_server> s=p2sppc_server::create(s_io_service(), port, pas_host, ext_pas_host);
			if(!cache_dir.empty())
			{
				//·���ָ�������
				boost::trim_right_if(cache_dir,  std::bind2nd(std::equal_to<char>(), '\\'));
				//����ļ����Ƿ����
				try{
					boost::filesystem::path cachePath(cache_dir.c_str());
					if(!boost::filesystem::exists(cachePath))
						boost::filesystem::create_directories(cachePath);
				}
				catch (std::exception& ex)
				{
					std::cerr << "create_directories("<<cache_dir<<") exception:" <<ex.what()<< "\n";
				}
				catch (...)
				{
					std::cerr << "create_directories("<<cache_dir<<") exception."<< "\n";
				}
			}
			else
			{
				boost::filesystem::path cachePath = boost::filesystem::current_path();
				cache_dir = cachePath.string();
			}
#ifdef BOOST_WINDOWS_API
			cache_dir+="\\";
#else
			cache_dir+="/";
#endif
			std::cout<<"cache path: "<<cache_dir<<std::endl;
			s->set_cache_param(cache_dir, cache_file_size);
			s_io_service().post(boost::bind(&p2sppc_server::run, s));
			//boost::asio::signal_set signals(s_io_service(), SIGINT, SIGTERM);
			//signals.async_wait(boost::bind(&boost::asio::io_service::stop, &s_io_service()));
			s_io_service().run();

		}
		catch (std::exception& e)
		{
			std::cerr << "exception: " << e.what() << "\n";
		}
		catch (...)
		{
			std::cerr << "unknown exception: " << "\n";
		}
		return 0;
	}

	int stop_service(void)
	{
		s_io_service().stop();
		return 0;
	}

#ifdef __cplusplus
}
#endif

