#include "p2s_mds/cmd_receiver.h"

#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>

#include <p2engine/utf8.hpp>

#include "app_common/app_common.h"
#include "p2s_mds/utility.h"

NAMESPACE_BEGIN(p2control);

//using namespace boost::interprocess;
using namespace p2engine;
using namespace p2common;
using namespace utility;
namespace fs=boost::filesystem;


#define NO_ERROR_STR ""
#ifdef _DEBUG
#define CMD_RECEIVER_DEBUG(x) /*x*/;
#else
#define CMD_RECEIVER_DEBUG(x) ;
#endif

mds_cmd_receiver::mds_cmd_receiver(io_service& ios)
	: basic_engine_object(ios)
	, init_state_(true)
	, last_check_alive_time_(timestamp_now())
	, last_process_time_(timestamp_now())
{
	set_obj_desc("p2s_mds_cmd");
}

mds_cmd_receiver::~mds_cmd_receiver()
{
	LogInfo("\ncmd receiver deconstructor!");
}

void mds_cmd_receiver::start(int server_id, int type)
{
	server_id_ = server_id;
	type_ = type;

	try{
		BOOST_ASSERT(alive_alarm_port_);
		interprocess_client_=interprocess_client::create(get_io_service(), 
			boost::lexical_cast<std::string>(server_id), 
			udp::endpoint(address_v4::loopback(), alive_alarm_port_), 
			type_);

		interprocess_client_->on_recvd_cmd_signal()==boost::bind(
			&this_type::process_cmd, this, _1, _2
			);
		interprocess_client_->start();
	}
	catch(...)
	{exit(0);}
}

bool mds_cmd_receiver::add_channel(const ctrl2m_create_channel_msg& msg, std::string& errorMsg)
{
	try{
		server_param_base param;
		param.channel_link=msg.channel_link();

		BOOST_AUTO(itr, servers_.find(param.channel_link));
		if(itr!=servers_.end()) 
			return true;

		std::vector<std::string> tracker_edps;
		param.channel_uuid=msg.channel_uuid();
		param.channel_key=msg.channel_key();
		param.internal_ipport=msg.internal_address();
		param.external_ipport=msg.external_address();
		param.type=(distribution_type)msg.type();
		param.name=fs::path(convert_to_wstring(msg.name()));
		param.tracker_ipport=msg.tracker_address(0);
		for (int i=1;i<msg.tracker_address_size();++i)
			tracker_edps.push_back(msg.tracker_address(i));

		if(is_vod_category(param.type))
		{
			param.media_directory=fs::path(convert_to_wstring(msg.media_directory()));
			if(!fs::exists(param.media_directory))
			{
				errorMsg=param.media_directory.string() + "is not exist!";
				LogError(errorMsg.c_str());

				std::cout<<"File is not exist, channel_link="<<param.channel_link
					<<" type="<<param.type
					<<" media_dir="<<param.media_directory.string()
					<<std::endl;
				return false;
			}
			param.film_duration=msg.duration();
			param.film_length=msg.length();
		}
		else
		{
			param.stream_recv_url=msg.stream_recv_url();
			param.enable_live_cache=msg.enable_live_cache();
			if(param.enable_live_cache)
			{
				param.live_cache_dir=msg.live_cache_dir();
				param.max_duration=msg.max_duration();
				param.max_length_per_file=msg.max_length_per_file();
			}
		}

		boost::shared_ptr<p2s_mds> server=create_and_start_mds(param, errorMsg);
		if(!server)
		{
			boost::format Msg("type=%d, "
				"link=%s, "
				"tracker=%s, "
				"stream_port=%d, "
				"ErrorMsg=%s");

			Msg%param.type
				%param.channel_link
				%param.tracker_ipport
				%param.stream_recv_url
				%errorMsg;

			errorMsg=Msg.str();
			LogError(Msg.str().c_str());
			std::cout<<errorMsg<<std::endl;
			return false;
		}

		//�����ݿ��ж�ȡ����tracker��ע��Ƶ��
		BOOST_FOREACH(std::string edp, tracker_edps)
		{
			if(edp==param.tracker_ipport)
				continue;
			DEBUG_SCOPE(
				std::cout<<"add tracker: "<<edp<<std::endl;
				);
			server->add_tracker(edp);
		}

		error_code ec;
		progress_alive_alarm::shared_ptr alarm=progress_alive_alarm::create(
			server, (is_live_category(param.type)?seconds(10):seconds(50)), 
			param.channel_link, 
			type_, 
			alive_alarm_port_
			);

		alarm->start(interprocess_client_, ec);

		servers_[param.channel_link]=server_elm(interprocess_client_, server, alarm);

		DEBUG_SCOPE(
			std::cout<<"~~~~~~~~~~~~~~~~-----\nstart channel link: "
			<<param.channel_link<<std::endl;
			std::cout<<"channel uuid: "
			<<param.channel_uuid<<std::endl;
		);
		return true;
	}
	catch(const std::exception& e)
	{errorMsg=e.what();return false;}
	catch(...)
	{errorMsg=std::string("add channel, unknown error");return false;}
}

void mds_cmd_receiver::set_alive_alarm(int port, const std::string& reg_code)
{
	if (regist_code_!=reg_code||!auth_)
	{
		if (!auth_)
		{
			auth_=auth::create(get_io_service());
			auth_->on_error_signal()=boost::bind(
				&this_type::post_error_msg, this, _1);
			auth_->on_auth_signal() = boost::bind(
				&this_type::report_auth_message, this, _1);
			auth_->run(reg_code);
		}
		else
		{
			auth_->reset_regist_code(reg_code);
		}
	}
	if (alive_alarm_port_!=port&&port!=0)
	{
		struct reset_alarm
		{
			reset_alarm(int32_t& port)
				:port_(port)
			{}

			void operator ()(const std::pair<std::string, server_elm>& val)
			{
				if(!val.second.alarm)
					return;

				val.second.alarm->reset_alarm_port(port_);
			}
			int32_t& port_;
		};

		std::for_each(servers_.begin(), servers_.end(), reset_alarm(port));
	}
	regist_code_ = reg_code;
	alive_alarm_port_ = port;
}

bool mds_cmd_receiver::del_channel(const std::string& channel_link, std::string& errorMsg)
{
	CMD_RECEIVER_DEBUG(
		std::cout<<get_obj_desc()<<"~~~~~~~~~~~~~~~~-----delete channel: "<<channel_link<<std::endl;
	);

	BOOST_AUTO(itr, servers_.find(channel_link));
	if(itr != servers_.end())
	{
		itr->second.stop();
		servers_.erase(itr);
	}

	return true;
}

bool mds_cmd_receiver::start_channel(const std::string& channel_link, std::string& errorMsg)
{
	CMD_RECEIVER_DEBUG(
		std::cout<<get_obj_desc()<<"!!!!!!!!!!!!!=======================start channel: "<<channel_link<<std::endl;
	);

	BOOST_AUTO(itr, servers_.find(channel_link));
	if(itr == servers_.end())
	{
		BOOST_ASSERT(0&&"��������ǲ�Ӧ�ó��ֵ�");
		errorMsg="channel not found, link="+channel_link;
		return false;
	}
	else
	{
		error_code ec;
		itr->second.start(ec);
		suspended_channels_.erase(channel_link);

		return !ec;
	}
}

bool mds_cmd_receiver::stop_channel(const std::string& channel_link, std::string& errorMsg)
{
	CMD_RECEIVER_DEBUG(
		std::cout<<get_obj_desc()<<"!!!!!!!!!!!!!=======================stop channel: "<<channel_link<<std::endl;
	);

	BOOST_AUTO(itr, servers_.find(channel_link));
	if(itr == servers_.end())
	{
		errorMsg="channel not found, link="+channel_link;
		return true;
	}
	else
	{
		itr->second.stop();
		suspended_channels_.insert(channel_link);

		return true;
	}
}


bool mds_cmd_receiver::reset_channel(const std::string& channel_link, std::string& errorMsg)
{
	CMD_RECEIVER_DEBUG(
		std::cout<<get_obj_desc()<<"!!!!!!!!!!!!!=======================reset channel: "<<channel_link<<std::endl;
	);

	BOOST_AUTO(itr, servers_.find(channel_link));
	if(itr == servers_.end())
	{
		BOOST_ASSERT(0&&"��Ӧ�ó��ֵ����");
		errorMsg="channel is not running, the param may have errors!";
		return false;
	}
	else
	{
		LogInfo("mds do reset channel=%s", channel_link.c_str());
		error_code ec;
		itr->second.reset(ec);
		if(ec)
			errorMsg="unknown error!";
		return !ec;
	}
}

boost::shared_ptr<p2s_mds> mds_cmd_receiver::create_and_start_mds(
	const server_param_base& param, std::string& errorMsg)

{
	if(is_vod_category(param.type))
	{
		//����ļ��Ƿ���ڣ������ھͲ�Ҫ������
		if(!fs::exists(param.media_directory))
		{
			errorMsg = param.media_directory.string()+" is not exist!";
			return boost::shared_ptr<p2s_mds>();
		}
	}

	init_state_ = false;

	//std::string& external_addr = param.external_ipport;
	//std::string& internal_addr = param.internal_ipport;
	//internal_addr = zero_port(internal_addr);
	//if (external_addr.length()>0&& external_addr.find(':')!=std::string::npos)
	//	external_addr= zero_port(external_addr);
	//else
	//	external_addr=internal_addr;

	error_code ec;
	boost::shared_ptr<p2s_mds> server
		=p2s_mds::create(get_io_service(), param, param.stream_recv_url);
	server->start(ec);
	if (ec)
	{
		errorMsg = "mds start error: " + ec.message();
		return boost::shared_ptr<p2s_mds>();
	}

	return server;
}

void mds_cmd_receiver::process_cmd(message_socket*, const safe_buffer& buf)
{
	mds_cmd_msg msg;
	if(!parser(buf, msg))
	{
		interprocess_client_->reply_cmd(msg.cmd(), 1, "msg parse error!");
		return;
	}

	std::cout<<"--------cmd recver: "<<msg.cmd()<<" type="<<type_<<" id="<<server_id_<<std::endl;
		

	std::string sessionID=msg.session_id();
	std::string errorMsg;
	try
	{
		if(msg.has_config_val())
		{
			bool ret=config_handler(msg, errorMsg);
			interprocess_client_->reply_cmd(sessionID, !ret, errorMsg);
		}
		else
		{
			bool ret=channel_handler(msg, errorMsg);
			interprocess_client_->reply_cmd(sessionID, !ret, errorMsg);
		}
	}
	catch(const std::exception& e){
		interprocess_client_->reply_cmd(sessionID, 1, e.what());
	}
	catch(...)
	{
		interprocess_client_->reply_cmd(sessionID, 1, "unknown error!");
	}
}

bool mds_cmd_receiver::channel_handler(const mds_cmd_msg& msg, 
	std::string& errorMsg)
{
	std::string cmd=msg.cmd();
	for (int i=0; i<msg.channel_ids_size(); ++i)
	{
		std::string channel_link = msg.channel_ids(i);
		boost::replace_all(channel_link, "/", "\\");
		if (channel_link.empty())
			continue;

		if (cmd==CMD_ADD_CHANNEL)
		{
			//BOOST_ASSERT(msg.channel_ids_size()==msg.channel_ids_size());
			BOOST_ASSERT(msg.channel_info(i).tracker_address_size()>0);

			if(!add_channel(msg.channel_info(i), errorMsg))
				return false;
		}
		else if(cmd==CMD_DEL_CHANNEL)
		{
			if(!del_channel(channel_link, errorMsg))
				return false;
		}
		else if(cmd==CMD_START_CHANNEL)
		{
			if(!start_channel(channel_link, errorMsg))
				return false;
		}
		else if(cmd==CMD_STOP_CHANNEL)
		{
			if(!stop_channel(channel_link, errorMsg))
				return false;
		}
		else if(cmd==CMD_RESET_CHANNEL)
		{
			if(!reset_channel(channel_link, errorMsg))
				return false;
		}
		else 
		{
			errorMsg="unrecognized cmd: "+cmd;
			return false;
		}
	}
	return true;
}

bool mds_cmd_receiver::config_handler(const mds_cmd_msg& msg, 
	std::string& errorMsg)
{
	std::string cmd=msg.cmd();
	BOOST_ASSERT(!msg.config_val().empty());
	std::vector<std::string> links;
	for (int i=0;i<msg.channel_ids_size();++i)
		links.push_back(msg.channel_ids(i));

	if(cmd==CMD_ADD_TRACKER)
	{
		return add_tracker(msg.config_val(), links, errorMsg);
	}
	else if(cmd==CMD_DEL_TRACKER)
	{
		return del_tracker(msg.config_val(), links, errorMsg);
	}
	else if(cmd==CMD_CHANGE_TRACKER)
	{
		return change_tracker(msg.config_val(), links, errorMsg);
	}
	else if(cmd==CMD_RESET_REGCODE)
	{
		return  reset_regist_code(msg.config_val(), errorMsg);
	}
	errorMsg="unknown cmd: "+cmd;
	return false;
}	

bool mds_cmd_receiver::add_tracker(const std::string& tracker_ipport, 
	const std::vector<std::string>& links, std::string& errorMsg)
{
	struct work_handler
	{
		static void add(const std::string& tracker_ipport, 
			pair_type& itr)
		{
			server_elm& server=itr.second;
			server.mds->add_tracker(tracker_ipport);
		}
	};

	return tracker_handlers(links, servers_, 
		boost::bind(&work_handler::add, tracker_ipport, _1)
		);
}

bool mds_cmd_receiver::del_tracker(const std::string& tracker_ipport, 
	const std::vector<std::string>& links, std::string& errorMsg)
{
	struct work_handler
	{
		static void del(const std::string& tracker_ipport, 
			pair_type& itr)
		{
			server_elm& server=itr.second;
			server.mds->del_tracker(tracker_ipport);
		}
	};

	return tracker_handlers(links, servers_, 
		boost::bind(&work_handler::del, tracker_ipport, _1)
		);
}

bool mds_cmd_receiver::change_tracker(const std::string& tracker_ipport, 
const std::vector<std::string>& links, std::string& errorMsg)
{
	struct work_handler
	{
		static void change(const std::string& tracker_ipport, 
			pair_type& itr)
		{
			server_elm& server=itr.second;
			server.mds->change_tracker(tracker_ipport);
		}
	};

	return tracker_handlers(links, servers_, 
		boost::bind(&work_handler::change, tracker_ipport, _1)
		);
}

bool mds_cmd_receiver::reset_regist_code(
	const std::string& reg_code, std::string& errorMsg)
{
	if(auth_)
	{
		auth_->reset_regist_code(reg_code);
		return true;
	}
	return false;
}

void mds_cmd_receiver::post_error_msg(const std::string& errorMsg)
{
	if(errorMsg.empty())
		return;

	std::cout<<errorMsg<<std::endl;
	try
	{
		c2s_cmd_reply_msg repMsg;
		repMsg.set_session_id(INTERNAL_SESSION_ID);
		repMsg.set_code(0);
		repMsg.set_msg(errorMsg);
		repMsg.set_id(boost::lexical_cast<std::string>(server_id_));
		repMsg.set_type(type_);

		interprocess_client_->send(serialize(repMsg), control_cmd_msg::cmd_reply);
	}
	catch (...)
	{}
}

void mds_cmd_receiver::report_auth_message(const std::string& message)
{
	if(message.empty()) return;
	try
	{
		DEBUG_SCOPE(std::cout<<"auth message: "<<message<<std::endl;);
		c2s_auth_msg auth_msg;
		auth_msg.set_message(message);
		interprocess_client_->send(serialize(auth_msg), control_cmd_msg::auth_message);
	}
	catch (...)
	{}
}

NAMESPACE_END(p2control);