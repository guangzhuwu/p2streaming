#include "p2s_shunt_control/shunt_control.h"

#if !defined(BOOST_WINDOWS_API)
#include <errno.h>
#include <unistd.h>
#endif

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include "shunt/typedef.h"
#include "shunt/shunt.h"
#include "app_common/shunt_db.h"
#include "app_common/process_killer.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#ifdef P2ENGINE_DEBUG
#define SHUNT_CONTROL_DBG(x) /*x*/
#else
#define SHUNT_CONTROL_DBG(x)
#endif
namespace fs=boost::filesystem;

namespace{
	using namespace p2control;
	typedef shunt_control::process_element		process_element;
	typedef shunt_control::process_set			process_set;
	typedef control_base::config_map_type config_map_type;

	static bool start_process(process_element& pelem, 
		int alive_alarm_port, std::string& errorMsg)
	{
		std::string appParam="p2s_shunt.exe";
		get_config_value("app.sub_process", appParam, errorMsg);
		if(appParam.empty()) appParam="p2s_shunt.exe";

#ifdef BOOST_WINDOWS_API
		boost::format param("%s --id=%s --alive_alarm_port=%d");
		param%appParam%pelem.id_%alive_alarm_port;

		STARTUPINFO startUpInfo = { sizeof(STARTUPINFO), 
			NULL, "", NULL, 
			0, 0, 0, 0, 0, 0, 0, 
			STARTF_USESHOWWINDOW, 
			0, 0, NULL, 0, 0, 0}; 
		startUpInfo.wShowWindow = SW_HIDE;
		startUpInfo.lpDesktop = NULL;

		PROCESS_INFORMATION pinfo;
		if(CreateProcess(NULL, (LPSTR)param.str().c_str(), \
			NULL, NULL, false, NORMAL_PRIORITY_CLASS, \
			NULL, NULL, &startUpInfo, &pinfo)
			)
		{
			pelem.pid_=pinfo.dwProcessId;
			CloseHandle(pinfo.hProcess);
			CloseHandle(pinfo.hThread);
			p2engine::system_time::sleep_millisec(200);
			return true;
		}
		else
		{
			long nError = GetLastError();
			char pTemp[256];
			sprintf(pTemp, "Failed to start program '%s', error code=%d\n", 
				"p2s_shunt.exe", nError);

			errorMsg.clear();
			errorMsg.assign(&pTemp[0], strlen(pTemp));
			return false;
		}
#else
	pid_t pid=fork();
	if(pid==0){
		fs::path pathName=fs::current_path();
		pathName/=appParam;

		int ret=execl(pathName.string().c_str(), 
			appParam.c_str(), 
			(boost::format(" --id=%s")%pelem.id_).str().c_str(), 
			(boost::format(" --alive_alarm_port=%d")%alive_alarm_port).str().c_str(), 
			(const char*)NULL);
		if(-1==ret)
			std::cout<<strerror(errno)<<std::endl;
		exit(1);
	}
	else
	{
		return true;
	}
#endif
	}


	template<typename HandlerType>
	static void shunt_alive_check(process_set& process_info_vec, 
		int alive_alarm_port, HandlerType& handler)
	{
		std::string errorMsg;
		using namespace p2control;
		timestamp_t now=timestamp_now();	
	
		static std::string AppParam;
		if(AppParam.empty())
			get_config_value("app.sub_process", AppParam, errorMsg);

		pid_set shunts;
		find_process_ids(shunts, AppParam);
		BOOST_AUTO(&index, process_info_vec.id_index());
		BOOST_FOREACH(const process_element& elm, index)
		{
			if(shunts.end()==shunts.find(elm.pid_))
			{
				BOOST_ASSERT(!elm.id_.empty());
				if(start_process(const_cast<process_element&>(elm), 
					alive_alarm_port, errorMsg))
				{
					handler(elm);
				}
			}
		}
	}

	static p2control::enum_cmd get_cmd(const std::string& cmd)
	{
		if("add"==cmd)
			return p2control::CMD_ADD;
		else if("start"==cmd)
			return p2control::CMD_START;
		else if("restart"==cmd)
			return p2control::CMD_RESTART;
		else if("stop"==cmd)
			return p2control::CMD_STOP;
		else if("del"==cmd)
			return p2control::CMD_DEL;
		else 
			return p2control::CMD_UNDEF;
	}

	static const std::string& get(const config_map_type& req, const std::string& k)
	{
		BOOST_AUTO(itr, req.find(k));
		if(itr!=req.end())
			return itr->second;

		static std::string STATIC_STRING;
		return STATIC_STRING;
	}

	static bool parse_send_edp(const std::string& xml, 
		p2shunt::shunt_xml_param& param, std::string& errorMsg)
	{
		using boost::property_tree::ptree;
		try
		{
			//����ļ��Ƿ���� ��ֹread_xml�׳��쳣
			p2engine::error_code ec;
			if(!fs::exists(fs::path(xml), ec))
			{
				boost::format msgFmt("could not open xml file %s ");
				msgFmt%xml;
				errorMsg=msgFmt.str();

				return false;
			}

			ptree pt;
			boost::property_tree::read_xml(xml, pt);
			return p2shunt::p2sshunt::load_config(pt, param);
		}
		catch (std::exception& e)
		{
			std::cout<<e.what()<<std::endl;
			BOOST_ASSERT(0&&"xml parser errro!!");
			return false;
		}
	}

	template<typename setType, typename HandlerType>
	static bool __process_all(const setType& sets, HandlerType& handler, 
		std::string& errorMsg)
	{
		std::string error_msg;
		//handler ���sets����insert��erase�����������setҪ����һ�ݿ���
		setType set_bk=sets;

		BOOST_FOREACH(const setType::value_type& elm, set_bk)
		{
			if(!handler(elm.id_, error_msg))
			{
				errorMsg.append(" ");
				errorMsg.append(elm.id_.c_str(), elm.id_.length());
				errorMsg.append("error msg: ");
				errorMsg.append(error_msg.c_str(), error_msg.length());
			}
		}

		if(!errorMsg.empty())
			return false;

		return true;
	}

};


NAMESPACE_BEGIN(p2control);

using namespace p2engine;
using namespace p2common;
using namespace p2shunt;
using namespace alive;

shunt_control::shunt_control(io_service& ios)
: control_base(ios)
{
}
shunt_control::~shunt_control()
{
	if(recover_timer_)
	{
		recover_timer_->cancel();
		recover_timer_.reset();
	}

	if(delay_timer_)
	{
		delay_timer_->cancel();
		delay_timer_.reset();
	}
}

void shunt_control::__start()
{	
	std::string errorMsg;
	db_.reset(new shunt_db(config_param().hostName, 
		config_param().userName, 
		config_param().pwds, 
		config_param().dbName));
	if(!db_->set_operation_http_port(config_param().http_cmd_port_, errorMsg))
	{
		std::cout<<errorMsg<<std::endl;
		exit(-1);
	}

	start_sub_process_check_timer();
	start_wild_check_timer();

	recover_from_db();
}

void shunt_control::__set_operation_http_port(uint32_t port, std::string& errorMsg)
{
	db_->set_operation_http_port(port, errorMsg);
}
void shunt_control::__set_alive_alarm_port(uint32_t port, std::string& errorMsg)
{
	db_->set_alive_alarm_port(port, errorMsg);
}

void shunt_control::__recover_from_db(int delayTime)
{
	try
	{
		std::string errorMsg;
		shunt_db::param_vector params;
		if(!db_->get_channels(params, errorMsg))
		{
			if(!recover_timer_)
			{
				recover_timer_=rough_timer::create(get_io_service());
				recover_timer_->set_obj_desc("shunt_control::recover_timer_");
				recover_timer_->register_time_handler(boost::bind(
					&this_type::__recover_from_db, this, delayTime
					);
			}
			recover_timer_->async_wait(milliseconds(DELAY_INTERVAL));
			return;
		}

		BOOST_FOREACH(const shunt_xml_param& param, params)
		{
			//��shared memory�д���param��Ӧ��shunt
			process_element process_Elm(param);
			recover_process_.id_index().insert(process_Elm);
		}

		if(!delay_timer_)
		{
			delay_timer_=rough_timer::create(get_io_service());
			delay_timer_->set_obj_desc("shunt_control::delay_timer_");
			delay_timer_->register_time_handler(boost::bind(&this_type::delay_start, this);
		}
		delay_timer_->async_wait(seconds(delayTime));
	}
	catch(...)
	{
		if(!recover_timer_)
		{
			recover_timer_=rough_timer::create(get_io_service());
			recover_timer_->set_obj_desc("shunt_control::recover_timer_");
			recover_timer_->register_time_handler(boost::bind(
				&this_type::__recover_from_db, this, delayTime
				);
		}
		recover_timer_->async_wait(milliseconds(DELAY_INTERVAL));
	}
}
void shunt_control::recover_from_db()
{
	pid_set pids;
	find_process_ids(pids, config_param().child_process_name_);

	int delay_time=pids.empty()?1:10;
	__recover_from_db(delay_time);
}

void shunt_control::delay_start()
{
	if(recover_process_.id_index().empty())
		return;

	std::string errorMsg;
	BOOST_AUTO(itr, recover_process_.id_index().begin());
	check_or_recover_shunt((*itr).id_, errorMsg);

	if(!recover_process_.id_index().empty())
		delay_start();
}

void shunt_control::on_sub_process_check_timer()
{
	shunt_alive_check(running_process_, 
		config_param().alive_alarm_port_, 
		boost::bind(&this_type::__update_shunt_pid, this, _1)
		);
}

void shunt_control::on_client_login(message_socket_sptr, const safe_buffer&buf)
{
	alive_alarm_report_msg loginMsg;
	if(!parser(buf, loginMsg))
		return;

	std::string id=loginMsg.id();
	on_know_new_shunt(id, loginMsg.pid());
}

void shunt_control::__delay_add_shunt(const std::string&id)
{
	shunt_xml_param* param=NULL;
	BOOST_AUTO(itr, running_process_.id_index().find(id));
	if(itr!=running_process_.id_index().end())
	{
		param=&const_cast<shunt_xml_param&>((*itr).param);
	}
	else
	{
		get_io_service().post(
			boost::bind(&this_type::__delay_add_shunt, SHARED_OBJ_FROM_THIS, id)
			);
		return;
	}

	mds_cmd_msg msg;
	msg.set_cmd(CMD_ADD_CHANNEL);
	msg.set_session_id(INTERNAL_SESSION_ID);
	msg.set_is_login_reply(true);
	ctrl2s_create_channel_msg* shunt_info=msg.mutable_shunt_info();
	shunt_info->set_id(id);
	shunt_info->set_receive_url(param->receive_url);
	BOOST_FOREACH(const std::string& u, param->send_urls)
	{
		shunt_info->add_send_urls(u);
	}

	interprocess_server_->post_cmd(id, serialize(msg));
}

void shunt_control::on_recvd_alarm_message(const safe_buffer& buf)
{
	alive::shunt_Alive msg;

	if(!parser(buf, msg))
		return;

	std::string id=msg.id();
	if(id.empty())
		return;

	BOOST_AUTO(itr, running_process_.id_index().find(id));
	if(itr!=running_process_.id_index().end())
	{
		shunt_id_t ID(id);
		__db__set_channel(shunt_xml_param(ID), msg);
	}
}

void shunt_control::on_know_new_shunt(const std::string& id, int32_t pid)
{
	BOOST_AUTO(itr2, suspended_process_.id_index().find(id));
	if(itr2!=suspended_process_.id_index().end())
		return;

	BOOST_AUTO(itr, recover_process_.id_index().find(id));
	if(itr!=recover_process_.id_index().end())
	{
		process_element& elm=const_cast<process_element&>(*itr);
		elm.pid_=pid;

		running_process_.id_index().insert(elm);
		recover_process_.id_index().erase(id);
	}
	
	itr=running_process_.id_index().find(id);
	DEBUG_SCOPE(
		BOOST_ASSERT(running_process_.id_index().end()!=itr);
	);

	process_element& elm=const_cast<process_element&>(*itr);
	elm.last_report_time_=timestamp_now();

	__delay_add_shunt(id);
}

void shunt_control::check_or_recover_shunt(const std::string& id, std::string& errorMsg)
{
	BOOST_AUTO(nItr, recover_process_.id_index().find(id));
	if(nItr!=recover_process_.id_index().end())
	{
		//��������
		if(__add_shunt(const_cast<process_element&>(*nItr), errorMsg))
			recover_process_.id_index().erase(nItr);
	}
}

void shunt_control::__update_shunt_pid(const process_element& elm)
{
	//���»���뵽���ݿ���
	shunt_Alive msg;
	msg.set_id(elm.id_);
	msg.set_kbps(0);
	msg.set_is_connected(false);
	msg.set_pid(elm.pid_);

	std::string errorMsg;
	shunt_id_t ID(elm.id_);
	__db__set_channel(shunt_xml_param(ID), msg);
}

void shunt_control::on_wild_sub_process_timer()
{
	pid_set pids;
	find_process_ids(pids, config_param().child_process_name_);

	bool isFind=false;
	BOOST_AUTO(&pidItr, running_process_.id_index());

	BOOST_FOREACH(pid_t pid, pids)
	{
		isFind = false;
		BOOST_FOREACH(const process_element& elm, pidItr)
		{
			if(elm.pid_==pid)
			{
				isFind=true;
				break;
			}
		}

		if(!isFind)
			try_kill_wild_sub_process(pid);
	}
}
bool shunt_control::on_request_handler(const req_session& reqSess, std::string& errorMsg)
{
	//��ӣ������� ֹͣ() ɾ��

	//ʵ����ֻ����������
	//��Ӻ�ɾ���漰���ݿ�
	//����ֹͣ���ı����ݿ���shunt��
	//cmd=add&config=shunt.xml
	errorMsg.clear();
	bool ret=false;
	std::string channel_link = get(reqSess.qmap, "id");
	switch (get_cmd(get(reqSess.qmap, "cmd")))
	{
	case CMD_ADD:
		{
			//������
			std::string xml_file = get(reqSess.qmap, "config");
			std::string Name=get(reqSess.qmap, "name");
			if(xml_file.empty())
			{
				errorMsg = "config file is missing!";
				return false;
			}
			if(Name.empty())
			{
				errorMsg="channel name is empty!";
				return false;
			}
			ret=__add_shunt(Name, xml_file, channel_link, errorMsg);			
		}
		break;
	case CMD_START:
		{
			//��suspend��ŵ�process_infos
			ret=__start_shunt(channel_link, errorMsg);
		}
		break;
	case CMD_RESTART:
		{
			ret=__restart_shunt(channel_link, errorMsg);
		}
		break;
	case  CMD_STOP:
		{
			ret=__stop_shunt(channel_link, errorMsg);
		}
		break;
	case CMD_DEL:
		{
			//ֱ�Ӵ�process_infos��ɾ��
			ret=__del_shunt(channel_link, errorMsg);
		}
		break;
	default:
		errorMsg = "unrecognized cmd!";
		ret=false;
		break;
	}
	if(ret) //shunt����ظ�cmd reply, �����Լ�����
	{
		c2s_cmd_reply_msg repMsg;
		repMsg.set_session_id(reqSess.session_id);
		repMsg.set_code(0);
		repMsg.set_msg(errorMsg);
		repMsg.set_id(channel_link);
		repMsg.set_type(-1);

		on_recvd_cmd_reply(serialize(repMsg));
		return true;
	}
	return false; //�����������ദ��
}

bool shunt_control::__add_shunt(process_element& elm, 
								std::string& errorMsg)
{
	if(!start_process(elm, config_param().alive_alarm_port_, errorMsg))
	{
		errorMsg = "process create failded!";
		return false;
	}

	size_t last_cnt = running_process_.id_index().size();
	running_process_.id_index().insert(elm);
	BOOST_ASSERT(running_process_.id_index().size()==last_cnt+1);

	return true;
}
bool shunt_control::__add_shunt(const std::string&Name, 
								const std::string& xml_file, 
								std::string& channel_link, 
								std::string& errorMsg)
{
	//��������
	shunt_id_t ID(channel_link);
	shunt_name_t NAME(Name);
	shunt_xml_param param(ID, NAME);
	if(!parse_send_edp(xml_file, param, errorMsg))
		return false;

	channel_link=param.id;
	BOOST_ASSERT(!param.id.empty());
	if(param.id.empty())
	{
		errorMsg="id parameter is empty, what to do?";
		return false;
	}

	bool isModify=false;
	process_element* elm=NULL;
	//�����ͬid�Ľ����Ƿ����
	BOOST_AUTO(rItr, running_process_.id_index().find(param.id));
	if(rItr!=running_process_.id_index().end())
	{
		elm=&const_cast<process_element&>(*rItr);
		isModify=true;
	}
	if(!elm)
	{
		BOOST_AUTO(rItr, suspended_process_.id_index().find(param.id));
		if(rItr!=suspended_process_.id_index().end())
		{
			elm=&const_cast<process_element&>(*rItr);
			isModify=true;
		}
	}

	bool recvNeedCheck=true;
	if(elm&&elm->param.receive_url==param.receive_url)
		recvNeedCheck=false;
	//recv 
	io_service& ios=get_io_service();
	error_code ec;
	std::string protocol=uri::from_string(param.receive_url, ec).protocol();
	if (ec)
	{
		errorMsg="uri("+param.receive_url+") is invalid.";
		return false;
	}
	if(recvNeedCheck&&!is_port_usable()(ios, param.receive_url, ec)
		&&"udp"==protocol)
	{
		errorMsg="port has being used by other process, check "+param.receive_url;
		return false;
	}
	
	//send
	BOOST_FOREACH(const std::string& u, param.send_urls)
	{
		if(elm&&elm->param.send_urls.find(u)!=elm->param.send_urls.end())
			continue;
		std::string protocol=uri::from_string(u, ec).protocol();
		if (ec)
		{
			errorMsg="uri("+u+") is invalid.";
			continue;
		}
		if(!is_port_usable()(ios, u, ec)&&"udp"!=protocol) 
		{
			errorMsg=" port has being used by other process, check "+u;
			return false;
		}
	}
	if(isModify)
		__del_shunt(param.id, errorMsg);

	process_element process_Elm(param);
	if(!__add_shunt(process_Elm, errorMsg))
	{
		if(elm)
			__add_shunt(*elm, errorMsg);
	}

	//���»���뵽���ݿ���
	shunt_Alive msg;
	msg.set_id(param.id);
	msg.set_kbps(0);
	msg.set_is_connected(false);
	msg.set_pid(process_Elm.pid_);
	__db__set_channel(param, msg);

	return true;
}

bool shunt_control::__start_shunt(const std::string& link, std::string& errorMsg)
{
	if(link.empty())
		return __start_all_shunt(errorMsg);

	BOOST_AUTO(itr, suspended_process_.id_index().find(link));
	if(itr!=suspended_process_.id_index().end())
	{
		running_process_.id_index().insert(*itr);
		suspended_process_.id_index().erase(itr);
		return true;
	}

	//���֮ǰ�Ѿ�������Ҳ����true
	itr=running_process_.id_index().find(link);
	if(itr!=running_process_.id_index().end())
		return true;

	BOOST_ASSERT(0&&"����ֱ���޸Ĺ����ݿ⣬����Ӧ�ó��ֵ����!");
	boost::format msgFmt("shunt with id %s is not found, or this shunt is running!");
	msgFmt%link;
	errorMsg=msgFmt.str();
	return false;
}

bool shunt_control::__restart_shunt(const std::string& link, std::string& errorMsg)
{
	if(link.empty())
		return __restart_all_shunt(errorMsg);

	bool retStop=__stop_shunt(link, errorMsg);
	bool retStart=__start_shunt(link, errorMsg);
	return retStop&&retStart;
}

bool shunt_control::__stop_shunt(const std::string& link, std::string& errorMsg)
{
	if(link.empty())
		return __stop_all_shunt(errorMsg);

	//��process_infos�ƶ���suspend
	BOOST_AUTO(itr, running_process_.id_index().find(link));
	if(itr!=running_process_.id_index().end())
	{
		process_element elm = *itr;

		//�رս���
		kill_process_by_id(elm.pid_);

		bool ret=__shunt_stopped(elm, errorMsg);

		suspended_process_.id_index().insert(*itr);
		running_process_.id_index().erase(itr);

		return ret;
	}
	
	//֮ǰ�Ѿ�ֹͣ����
	itr=suspended_process_.id_index().find(link);
	if(itr!=suspended_process_.id_index().end())
	{
		__shunt_stopped(*itr, errorMsg);
		return true;
	}

	BOOST_ASSERT(0&&"����ֱ���޸Ĺ����ݿ⣬����Ӧ�ó��ֵ����!");
	boost::format msgFmt("shunt with id %s is not found!");
	msgFmt%link;
	errorMsg=msgFmt.str();
	return false;
}
bool shunt_control::__del_shunt(const std::string& link, std::string& errorMsg)
{
	if(link.empty())
		return __del_all_shunt(errorMsg);

	__db__del_channel(link);

	BOOST_AUTO(itr, running_process_.id_index().find(link));
	BOOST_AUTO(sItr, suspended_process_.id_index().find(link));

	pid_t pid=0;
	if(itr==running_process_.id_index().end())
	{
		if(sItr==suspended_process_.id_index().end())
		{
			boost::format msgFmt("shunt with id %s was delete already!");
			msgFmt%link;
			errorMsg=msgFmt.str();
			return true;
		}
		else
		{
			pid=(*sItr).pid_;
			suspended_process_.id_index().erase(sItr);
		}
	}
	else
	{
		pid=(*itr).pid_;
		running_process_.id_index().erase(itr);
	}

	kill_process_by_id(pid);
	
	return true;
}

bool shunt_control::__start_all_shunt(std::string& errorMsg)
{
	return __process_all(suspended_process_, boost::bind(
		&this_type::__start_shunt, SHARED_OBJ_FROM_THIS, _1, _2), errorMsg);
}

bool shunt_control::__restart_all_shunt(std::string& errorMsg)
{
	bool stopRet=__stop_all_shunt(errorMsg);
	bool startRet=__start_all_shunt(errorMsg);
	return stopRet&&startRet;
}

bool shunt_control::__stop_all_shunt(std::string& errorMsg)
{
	return __process_all(running_process_, boost::bind(
		&this_type::__stop_shunt, SHARED_OBJ_FROM_THIS, _1, _2), errorMsg);

}

bool shunt_control::__del_all_shunt(std::string& errorMsg)
{
	bool runRet=__process_all(running_process_, boost::bind(
		&this_type::__del_shunt, SHARED_OBJ_FROM_THIS, _1, _2), errorMsg);

	bool susRet =__process_all(suspended_process_, boost::bind(
		&this_type::__del_shunt, SHARED_OBJ_FROM_THIS, _1, _2), errorMsg);

	return runRet&&susRet;
}

bool shunt_control::__shunt_stopped(const process_element& elm, std::string& errorMsg)
{
	//���ݿ�������״̬ת��false
	shunt_Alive msg;
	msg.set_id(elm.id_);
	msg.set_kbps(0);
	msg.set_is_connected(false);
	msg.set_pid(0);

	__db__set_channel(shunt_xml_param(), msg);
	return true;
}

bool shunt_control::__db__set_channel(const shunt_xml_param& param
	, const shunt_Alive&msg)
{
	std::string errorMsg;
	return db_->set_channel(param, msg, errorMsg);
}

bool shunt_control::__db__del_channel(const std::string& link)
{
	std::string errorMsg;
	return db_->del_channel(link, errorMsg);
}

NAMESPACE_END(p2control);
