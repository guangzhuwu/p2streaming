#include <p2engine/p2engine.hpp>
#include <p2engine/push_warning_option.hpp>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <p2engine/pop_warning_option.hpp>
#include "p2s_shunt/version.h"
#include "shunt/shunt.h"
#include "shunt/typedef.h"
#include "p2s_shunt/alive_alarm.h"
#include "app_common/app_common.h"

#ifdef BOOST_MSVC
#	pragma comment(lib, "libeay32MT")
#	pragma comment(lib, "ssleay32MT")
#	pragma comment(lib, "Gdi32")
#	pragma comment(lib, "User32")
#endif

namespace po = boost::program_options;
using namespace p2engine;

class shunt_server
	: public interprocess_client
{
	typedef shunt_server this_type;
	SHARED_ACCESS_DECLARE;
public:
	static boost::shared_ptr<shunt_server> create(io_service&ios, const std::string&id, 
		int guardPort)
	{
		return boost::shared_ptr<shunt_server>(new shunt_server(ios, id, guardPort), 
			shared_access_destroy<this_type>());
	}
public:
	shunt_server(io_service&ios, const std::string&id, 
		int alivePort)
		:interprocess_client(ios, id, udp::endpoint(address_v4::loopback(), alivePort), -1)
		, shunt_(ios)
		, started_(false)
	{
		on_recvd_cmd_signal()=boost::bind(
			&this_type::process_cmd, this, alivePort, _1, _2
			);
	}

	~shunt_server()
	{}

protected:
	void process_cmd(int alivePort, message_socket*, const safe_buffer& buf)
	{
		mds_cmd_msg msg;
		if(!parser(buf, msg))
		{
			reply_cmd(msg.cmd(), 1, "msg parse error!");
			return;
		}
		
		if(msg.cmd()!=CMD_ADD_CHANNEL)
		{
			reply_cmd(msg.cmd(), 1, "msg parse error!");
			BOOST_ASSERT("INVALID CMD");
			exit(-1);
		}

		BOOST_ASSERT(msg.has_shunt_info());
		BOOST_ASSERT(id()==msg.shunt_info().id());

		ctrl2s_create_channel_msg shunt_info=msg.shunt_info();
		p2shunt::shunt_xml_param param;
		param.id=shunt_info.id();
		param.receive_url=shunt_info.receive_url();
		for (int i=0;i<shunt_info.send_urls_size();++i)
		{
			param.send_urls.insert(shunt_info.send_urls(i));
		}
		if(!started_)
		{
			typedef void(p2sshunt::* func_type)(const p2shunt::shunt_xml_param&);
			get_io_service().post(
				boost::bind(static_cast<func_type>(&p2sshunt::run), &shunt_, param)
			);
			started_=true;
		}
		if(!alarm_)
		{
			alarm_=progress_alive_alarm::create(id(), shunt_, alivePort);
			get_io_service().post(boost::bind(&progress_alive_alarm::start, alarm_, SHARED_OBJ_FROM_THIS));
		}
		
		reply_cmd(msg.cmd(), 0, "");
	}
private:
	p2sshunt shunt_;
	progress_alive_alarm::shared_ptr alarm_;
	bool started_;
};

int main(int argc, char* argv[])
{
	//typedef boost::posix_time::ptime ptime;
	//if (boost::date_time::microsec_clock<ptime>::universal_time()
	//	>boost::posix_time::time_from_string("2013-01-01 00:00:00.000")
	//	||
	//	boost::date_time::microsec_clock<ptime>::universal_time()
	//	<boost::posix_time::time_from_string("2012-09-10 00:00:00.000")
	//	)
	//{
	//	return 0;
	//}

	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "help messages")
		("version,v", "version " BPI_SHUNT_VERSION)
		("config_file", po::value<std::string>(), "set config xml file path, default=shunt.xml")
		("alive_alarm_port", po::value<int>(), "set alive alarm port")
		("id", po::value<std::string>(), "set id of shunt")
		;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << "\n";
		return -1;
	}
	else if (vm.count("version"))
	{
		std::cout <<"p2s_shunt, version "BPI_SHUNT_VERSION <<"\n";
		return -1;
	}

	try
	{
		std::string xml="shunt.xml";
		int alivePort=8000;
		std::string id;


		if (vm.count("config_file")) 
			xml=vm["config_file"].as<std::string>();
		if (vm.count("id")) 
			id=vm["id"].as<std::string>();
		if (vm.count("alive_alarm_port"))
			alivePort=vm["alive_alarm_port"].as<int>();

		running_service<1> svc;
		interprocess_client::shared_ptr alive_alarm_;
		boost::shared_ptr<p2sshunt> shunt;
		progress_alive_alarm::shared_ptr  alarm;
		//����ʹ��db
		if(!id.empty())
		{
			alive_alarm_=shunt_server::create(svc.get_running_io_service(), id, alivePort);
			if(!alive_alarm_)
			{
				BOOST_ASSERT(0);
				exit(-1);
			}

			alive_alarm_->start();
		}
		else
		{
			shunt.reset(new p2sshunt((svc.get_running_io_service())));
			typedef void(p2sshunt::* func_type)(const std::string&);
			svc.get_running_io_service().post(
				boost::bind(static_cast<func_type>(&p2sshunt::run), shunt, xml)
				);

			alarm=progress_alive_alarm::create(id, *shunt, alivePort);
			svc.get_running_io_service().post(
				boost::bind(&progress_alive_alarm::start, alarm, interprocess_client::shared_ptr())
				);
		}
		
		for (;;)
		{
			system_time::sleep_millisec(100);
		}
	}
	catch (std::exception& e)
	{
		std::cout << "exception: " << e.what() << "\n";
		system_time::sleep_millisec(500);
	}
	catch (...)
	{
		std::cout << "unknown exception: " << "\n";
		system_time::sleep_millisec(500);
	}
	return 0;
}