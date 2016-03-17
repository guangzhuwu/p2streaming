#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <p2engine/pop_warning_option.hpp>

#include "tracker/tracker_service_logic.h"
#include "p2s_tracker/version.h"
//#include "app_common/level_db.h"

#ifdef BOOST_MSVC
#	pragma comment(lib, "libeay32MT.lib")
#	pragma comment(lib, "ssleay32MT.lib")
#	pragma comment(lib, "Gdi32.lib")
#	pragma comment(lib, "User32.lib")
#endif

namespace app_common{}
namespace p2tracker
{
	using namespace app_common;

	inline std::string generat_key(const std::string& id, const std::string& Key)
	{
		boost::format keyFmt("%s_%s_%d");
		keyFmt%id%Key%time(NULL);
		return keyFmt.str();
	}

	class p2s_tracker
		:public tracker_service_logic_base
	{
		typedef p2s_tracker this_type;
		SHARED_ACCESS_DECLARE;

	protected:
		p2s_tracker(io_service& svc, const std::string& db_file)
			:tracker_service_logic_base(svc)
		{
			//db_.reset(new level_db(db_file));
		}
		virtual ~p2s_tracker(){};

	public:
		static shared_ptr create(io_service& svc, const std::string& db_file)
		{
			return shared_ptr(new this_type(svc, db_file)
				, shared_access_destroy<this_type>());
		}

	public:
		//������Ϣ����
		virtual void register_message_handler(message_socket*)
		{}
		virtual void known_offline(peer*)
		{}
		virtual bool permit_relay(peer*, const relay_msg&)
		{return true;}
		virtual void recvd_peer_info_report(peer* p, const p2ts_quality_report_msg& msg)
		{
			DEBUG_SCOPE(
				
			peer_info& info=p->m_peer_info;
			
			//д�벥������ ������
			std::string errorMsg;
			write(generat_key(info.peer_id(), "play_quality"), 
				boost::lexical_cast<std::string>(msg.playing_quality()), 
				errorMsg);
			
			write(generat_key(info.peer_id(), "uplink_lostrate"), 
				boost::lexical_cast<std::string>(msg.uplink_lostrate()), 
				errorMsg);
			
			write(generat_key(info.peer_id(), "downlink_lostrate"), 
				boost::lexical_cast<std::string>(msg.downlink_lostrate()), 
				errorMsg);
			);
			DEBUG_SCOPE(
				std::cout<<"write to db:\n"
				<<"("<<generat_key(hex_to_string(info.peer_id()), "play_quality")<<"---"<<msg.playing_quality()<<")\n"
				<<"("<<generat_key(hex_to_string(info.peer_id()), "uplink_lostrate")<<"---"<<msg.uplink_lostrate()<<")\n"
				<<"("<<generat_key(hex_to_string(info.peer_id()), "downlink_lostrate")<<"---"<<msg.downlink_lostrate()<<")\n"
				<<std::endl;
				);
		}
	private:
		void write(const std::string& Key, const std::string& Value, std::string& errorMsg)
		{
			//if(!db_->put(Key, Value, errorMsg))
			{
				//BOOST_ASSERT(0);
				std::cout<<errorMsg<<std::endl;
			}
		}

	private:
		//std::auto_ptr<level_db> db_;
	};

}

namespace po = boost::program_options;
using namespace p2tracker;
int main(int argc, char* argv[])
{
	try
	{
		po::options_description desc("Allowed options");
		desc.add_options()
			("help", "help messages")
			("version,v", "P2S P2S tracker version "P2S_TRACKER_VERSION)
			("type", po::value<int>(), "set type of tracker, INTERACTIVE_LIVE_TYPE(0)/LIVE_TYPE(1)/VOD_TYPE(2)/BT_TYPE(3)")
			("internal_address", po::value<std::string>(), "set internal_address of tracker")
			("external_address", po::value<std::string>(), "set external_address of tracker")
			("private_key", po::value<std::string>(), "set private key of tracker")
			("db_file", po::value<std::string>(), "set record db file path")
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
			std::cout <<"p2s_tracker, version "P2S_TRACKER_VERSION <<"\n";
			return -1;
		}

		int type=-1;
		std::string internal_addr;
		std::string external_addr;
		std::string privateKey;
		std::string db_file="tracker_db";

		if (vm.count("type"))
			type=vm["type"].as<int>();
		if (vm.count("internal_address"))
			internal_addr=vm["internal_address"].as<std::string>();
		if (vm.count("external_address"))
			external_addr=vm["external_address"].as<std::string>();
		if (vm.count("private_key"))
			privateKey=vm["private_key"].as<std::string>();
		if (vm.count("db_file"))
			db_file=vm["db_file"].as<std::string>();

		io_service ios;
		boost::shared_ptr<p2s_tracker> tracker;
		if (type>=0&&internal_addr.length()>0)
		{
			std::string::size_type pos=internal_addr.find(':');
			if (pos==std::string::npos)
				return -1;

			tracker_param_base param;
			param.type=(distribution_type)type;
			param.internal_ipport=internal_addr;
			if (external_addr.length()>0&&external_addr.find(':')!=std::string::npos)
				param.external_ipport=external_addr;
			else
				param.external_ipport=internal_addr;
			param.aaa_key=privateKey;

			tracker=p2s_tracker::create(ios, db_file);
			tracker->start(param);
		}
		else
		{
			return -1;
		}

		ios.run();
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		//system_time::sleep(seconds(10));
		return -1;
	}
	catch (...)
	{
		std::cout<<"unknown exception"<<std::endl;
		//system_time::sleep(seconds(10));
		return -1;
	}
	return 0;
};
