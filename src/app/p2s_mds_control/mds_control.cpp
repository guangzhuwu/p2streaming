#include "common/utility.h"
#include "p2s_mds_control/mds_control.h"
#include "app_common/app_common.h"
#include "libupnp/utility.h"
#include "libupnp/escape_string.hpp"

#include <p2engine/utf8.hpp>
#include <p2engine/push_warning_option.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <boost/filesystem/path.hpp>
#include <p2engine/pop_warning_option.hpp>

NAMESPACE_BEGIN (p2control);

namespace{

#ifdef P2ENGINE_DEBUG
#define CONTROL_DBG(x) /*x*/
#else
#define CONTROL_DBG(x)
#endif

#define RECOVER_SET_OF_TYPE(TYPE) \
	mds_set& recoverMds=is_vod_category(TYPE)?recover_vod_set_:recover_live_set_;
#define RUN_SET_OF_TYPE(TYPE) \
	mds_set& runningMds=is_vod_category(TYPE)?vod_mds_set_:live_mds_set_;

}
namespace fs=boost::filesystem;

mds_control::mds_control(io_service& ios)
	: control_base(ios)
{	
	set_obj_desc("mds_control");
}

mds_control::~mds_control()
{
	stop_check_timer();

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

size_t mds_control::max_process_cnt(distribution_type type, size_t channel_size)
{
	BOOST_ASSERT(config_param().max_channel_per_server_>=1);
	size_t n=is_vod_category(type)?config_param().max_channel_per_server_:1;
	return (channel_size+n-1)/n;
}

void mds_control::__start()
{
	std::string errorMsg;
	db_.reset(new mds_db(config_param().hostName, 
		config_param().userName, config_param().pwds, config_param().dbName));

	__set_operation_http_port(config_param().http_cmd_port_, errorMsg);

	start_check_timer();	
	start_sub_process_check_timer();
	start_wild_check_timer();
	recover_from_db();

	last_write_time_=timestamp_now();
	db_->get_regist_code(old_regist_code_, &errorMsg);
	BOOST_ASSERT(config_param().alive_alarm_port_);
}

void mds_control::__set_operation_http_port(uint32_t port, std::string& errorMsg)
{
	if (!db_->set_operation_http_port(port, &errorMsg))
	{
		LogInfo("function=start_cmd_accptor, control error, msg=%s", errorMsg.c_str());
		//system_time::sleep_millisec(50);//�ȴ�LogInfoд�����
		//exit(1);//���д��ʧ�ܣ�����ζ��mds�޷�ͨ�ţ��Ϳ�����ɲ�����������mds�����
		db_->log(errorMsg);
	}
}

void mds_control::__set_alive_alarm_port(uint32_t port, std::string& errorMsg)
{
}

bool mds_control::on_request_handler(const req_session& reqSess, std::string& errorMsg)
{
	try
	{
		errorMsg.clear();
		BOOST_AUTO(itr, reqSess.qmap.find("type"));
		if(itr != reqSess.qmap.end()
			&&service_type_valide(get_int_type(reqSess.qmap, "service_type")))
		{
			errorMsg.clear();

			std::string cmd_value=itr->second;
			if("add" == cmd_value&&!on_add_request(reqSess, errorMsg))
				return false;

			else if("del" == cmd_value&&!on_del_request(reqSess, errorMsg))
				return false;

			else if("start"==cmd_value&&!on_start_request(reqSess, errorMsg))
				return false;

			else if("stop"==cmd_value&&!on_stop_request(reqSess, errorMsg))
				return false;

			else if("reset"==cmd_value&&!on_reset_request(reqSess, errorMsg))
				return false;

			else if("tracker_cmd"==cmd_value&&!on_tracker_request(reqSess, errorMsg))
				return false;

			else if("update_reg_code"==cmd_value&&!on_reg_code_updated(reqSess, errorMsg))
				return false;

			else if("status"==cmd_value&&!on_status_request(reqSess, errorMsg))
				return false;
		}
		else
		{
			errorMsg = "request type is missing or service type is invalid!";
			std::cout<<errorMsg<<std::endl;
			return false;
		}
		return true;
	}
	catch(const std::exception& e)
	{
		errorMsg=e.what();
		return false;
	}
	catch(...){return false;}
}

bool mds_control::regist_code_config(const config_map_type& req, std::string& errorMsg)
{
	const std::string& regist_code=get(req, "regist_code");
	if(!regist_code.empty()&&old_regist_code_!=regist_code)
	{
		old_regist_code_=regist_code;
		return db_->set_regist_code(regist_code, &errorMsg);
	}
	errorMsg.clear();
	return true;
}

bool mds_control::on_reset_request(const req_session& reqSess, std::string& errorMsg)
{
	//���ֱ���㲥
	int serviceType  = get_int_type(reqSess.qmap, "service_type");
	if(serviceType==-1)
	{
		errorMsg="service type is required, reset for all type is not allowed!";
		return false;
	}

	//�����Ƶ��
	return do_add_or_reset_channel(reqSess, (distribution_type)serviceType, errorMsg);

}

bool mds_control::on_tracker_request(const req_session& reqSess, std::string& errorMsg)
{
	int serviceType  = get_int_type(reqSess.qmap, "service_type");
	if(serviceType==-1)
	{	
		errorMsg = "serviceType is missing, required!";
		return false;
	}

	//tracker �������ͣ����ӻ��Ǹı�
	const std::string& tracker_op = get(reqSess.qmap, "tracker_op");
	if(tracker_op.empty())
	{
		errorMsg = "tracker_op is missing, the procedure don't know what to do!";
		return false;
	}

	//tracker
	const std::string& tracker = get(reqSess.qmap, "tracker_address");
	if(tracker.empty())
	{
		errorMsg="tracker address is missing, required!";
		return false;
	}

	//�����change�� ���µ����ݿ�
	if(CMD_CHANGE_TRACKER==tracker_op)
	{
		channel_vec_type params;
		if(!db_->get_channels((distribution_type)serviceType, params, &errorMsg))
		{
			errorMsg = "no channel of this type was found!";
			return false;
		}

		BOOST_FOREACH(server_param_base& param, params)
		{
			param.tracker_ipport = tracker;
		}

		db_->set_tracker_endpoint((distribution_type)serviceType, tracker);
	}

	server_param_base param;
	param.tracker_ipport = tracker; 
	param.type=(distribution_type)serviceType;
	param.channel_link=get(reqSess.qmap, "link");

	channel_vec_type params(1, param); //TODO(֧���ض���Ƶ����������Ҫ����link);
	if(!param.channel_link.empty())
	{
		//�ҵ�link��Ӧ��mds_id
		BOOST_AUTO(itr, channel_set_.link_index().find(param.channel_link));
		if(itr!=channel_set_.link_index().end())
		{
			if(CMD_DEL_TRACKER!=tracker_op)
			{
				db_->add_tracker_endpoint(param);
			}
			else
			{
				db_->del_tracker_endpoint(param);
			}

			post_cmd_to_mds(reqSess.session_id, (*itr).mds_id, params, tracker_op);
		}
		else //��link ����û���ҵ�Ƶ��
		{
			BOOST_ASSERT(0&&"��Ӧ�ó��ֵ����");
			errorMsg="channel not found, this situation should never happened, but it happened now!";
			return false;
		}
	}
	else
	{
		mds_set& mds=current_server_set((distribution_type)serviceType);
		BOOST_AUTO(&index, mds.mds_id_index());
		BOOST_FOREACH(const mds_element& mds_elm, index) //����mds
		{
			params.clear();
			params.reserve(mds_elm.link_url_map.size());
			for (BOOST_AUTO(itr, mds_elm.link_url_map.begin());
				itr!=mds_elm.link_url_map.end();++itr)
			{
				server_param_base param;
				param.channel_link=itr->first;
				param.type=(distribution_type)serviceType;
				param.tracker_ipport = tracker; 
				params.push_back(param);

				if(CMD_DEL_TRACKER!=tracker_op)
				{
					db_->add_tracker_endpoint(param);
				}
				else
				{
					db_->del_tracker_endpoint(param);
				}

			}
			post_cmd_to_mds(reqSess.session_id, mds_elm.id, params, tracker_op);
		}
	}

	return true;
}

bool mds_control::on_reg_code_updated(const req_session& req, std::string& errorMsg)
{
	if(regist_code_config(req.qmap, errorMsg))
	{
		//update to database
		if(0==(live_mds_set_.mds_id_index().size()+vod_mds_set_.mds_id_index().size()))
		{
			c2s_cmd_reply_msg repMsg;
			repMsg.set_session_id(req.session_id);
			repMsg.set_code(0);
			repMsg.set_msg(errorMsg);
			repMsg.set_id(INTERNAL_CHANNEL_ID);
			repMsg.set_type(-1);
			on_recvd_cmd_reply(serialize(repMsg));
			return true;
		}
		
		for(BOOST_AUTO(itr, live_mds_set_.mds_id_index().begin());
			itr!=live_mds_set_.mds_id_index().end();++itr)
		{
			post_config_cmd_to_mds(req.session_id, LIVE_TYPE, 
				(*itr).id, old_regist_code_, CMD_RESET_REGCODE);
		}
		
		for(BOOST_AUTO(itr, vod_mds_set_.mds_id_index().begin());
			itr!=vod_mds_set_.mds_id_index().end();++itr)
		{
			post_config_cmd_to_mds(req.session_id, VOD_TYPE, 
				(*itr).id, old_regist_code_, CMD_RESET_REGCODE);
		}
		
		return true;
	}
	return false;
}

bool mds_control::on_status_request(const req_session& reqSess, std::string& errorMsg)
{
	errorMsg.clear();
	c2s_cmd_reply_msg repMsg;
	repMsg.set_session_id(reqSess.session_id);
	repMsg.set_code(0);
	repMsg.set_msg(errorMsg);
	repMsg.set_id(INTERNAL_CHANNEL_ID);
	repMsg.set_type(-1);
	on_recvd_cmd_reply(serialize(repMsg), config_param().auth_message_);
	DEBUG_SCOPE(
		std::cout<<"auth response: "<<config_param().auth_message_<<std::endl;
		);
	return true;
}

bool mds_control::on_stop_request(const req_session& reqSess, std::string& errorMsg)
{
	try{
		//���ֱ���㲥
		int serviceType  = get_int_type(reqSess.qmap, "service_type");
		if(-1 == serviceType)
		{
			std::string errorMsg2;
			bool rst=do_stop_channel(reqSess, VOD_TYPE, errorMsg);
			rst=rst&&do_stop_channel(reqSess, LIVE_TYPE, errorMsg2);
			if (!rst)
			{
				errorMsg+="\n";
				errorMsg+=errorMsg2;
			}
			return rst;
		}
		return do_stop_channel(reqSess, (distribution_type)serviceType, errorMsg);
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

bool mds_control::do_add_or_reset_channel(const req_session& reqSess, 
	distribution_type type, 
	std::string& errorMsg)
{
	std::string channel_link = get(reqSess.qmap, "link");
	if(!channel_link.empty())
		return __do_add_or_reset_channel(reqSess, type, errorMsg);

	return process_all(reqSess.session_id, type, errorMsg, 
		boost::bind(&this_type::__do_reset_mds, SHARED_OBJ_FROM_THIS, reqSess, _1, _2));

}

bool mds_control::do_start_channel(const req_session& reqSess, 
	distribution_type type, 
	std::string& errorMsg)
{
	std::string channel_link = get(reqSess.qmap, "link");
	if(!channel_link.empty())//һ������Ƶ��
		return __do_start_channel(reqSess, type, channel_link, errorMsg);

	return process_all(reqSess.session_id, type, errorMsg, 
		boost::bind(&this_type::__do_start_mds, SHARED_OBJ_FROM_THIS, reqSess, _1, _2));
}


bool mds_control::do_stop_channel(const req_session& reqSess, 
	distribution_type type, 
	std::string& errorMsg)
{
	std::string channel_link = get(reqSess.qmap, "link");
	if(!channel_link.empty())//һ������Ƶ��
		return __do_stop_channel(reqSess, type, channel_link, errorMsg);

	return process_all(reqSess.session_id, type, errorMsg, 
		boost::bind(&this_type::__do_stop_mds, SHARED_OBJ_FROM_THIS, reqSess, _1, _2));
}

bool mds_control::__do_reset_mds(const req_session& reqSess, 
	const mds_element& elm, 
	std::string& errorMsg)
{
	BOOST_ASSERT(!elm.link_url_map.empty());
	try
	{
		distribution_type type=(distribution_type)elm.type;
		if(-1==type)
			return false;

		std::string error_msg;
		if(-1!=elm.id)
		{
			node_map_t sess;
			channel_vec_type& params=sess.node_map[elm.id];
			params.reserve(elm.link_url_map.size());

			BOOST_AUTO(itr, elm.link_url_map.begin());
			for(;itr!=elm.link_url_map.end();++itr)
			{
				server_param_base param;
				param.type = type;
				param.channel_link = itr->first;

				db_->start_channel(param);
				params.push_back(param);
				suspended_channels_.erase(param.channel_link);
			}

			sess.session_id=reqSess.session_id;
			process_mapped_channel(sess, type, CMD_STOP_CHANNEL);
			return true;
		}
		else
		{
			errorMsg="channel not found!";
			return false;
		}
	}
	catch(const std::exception& e)
	{
		errorMsg=e.what();
		return false;
	}
	catch(...){return false;}
}

bool mds_control::__do_add_or_reset_channel(const req_session& reqSess, 
	distribution_type type, std::string& errorMsg)
{
	std::string channel_link = get(reqSess.qmap, "link");
	BOOST_ASSERT(-1 != type);
	if(channel_link.empty())
	{
		errorMsg="channel link is missing!\n";
		LogError(errorMsg.c_str());
		db_->log(errorMsg);
		return false;
	}

	std::string cmdValue=get(reqSess.qmap, "type");

	try{
		server_param_base param;
		param.channel_link = channel_link;
		param.type=type;

		//������set�в���channel_link���о���reset��û�о���add��update
		int mds_id = channel_belong_to(param.type, param.channel_link);
		node_map_t sess;
		sess.session_id=reqSess.session_id;
		if("reset"==cmdValue
			&&mds_id!=-1) //reset
		{
			sess.node_map[mds_id] = channel_vec_type(1, param);
			process_mapped_channel(sess, type, CMD_STOP_CHANNEL);
			return true;
		}

		//////////////////////////////////////////////////////////////////////////
		errorMsg.clear();
		BOOST_ASSERT("add"==cmdValue);
		if(mds_id!=-1)
		{
			errorMsg="channel[link: "+param.channel_link+"] already exist!\n";
			LogError(errorMsg.c_str());
			db_->log(errorMsg);
			return false;
		}

		std::string  str;;
		if(!(str=get(reqSess.qmap, "channel_name")).empty())
			param.name = fs::path(convert_to_wstring(str));
		if(!(str=get(reqSess.qmap, "media_directory")).empty())
			param.media_directory = fs::path(convert_to_wstring(str));

		error_code ec;
		if(is_vod_category(type)
			&&!fs::exists(param.media_directory, ec)
			)
		{
			errorMsg=get(reqSess.qmap, "media_directory")+" is not exist!";
			return false;
		}

		if(ec)
		{
			errorMsg=get(reqSess.qmap, "media_directory")+" is not exist!";
			return false;
		}

		if (param.channel_uuid.empty())
		{
			if (is_live_category(type))
				param.channel_uuid=param.channel_link;
			else
			{
				param.channel_uuid=get_file_md5(param.media_directory);
				server_param_base checkParam;
				checkParam.channel_uuid=param.channel_uuid;
				checkParam.channel_link=param.channel_link;
				checkParam.type=param.type;
				db_->get_channel(checkParam, &errorMsg);
				if(!checkParam.media_directory.empty())
				{
					errorMsg="file "+checkParam.media_directory.string()+" is used by other channel!";
					return false;
				}

				param.film_length=get_length(param.media_directory);
				param.film_duration=get_duration(param.media_directory);
			}
		}

		//shift live 
		if(!(str=get(reqSess.qmap, "cache_dir")).empty())
		{
			param.live_cache_dir = fs::path(convert_to_wstring(str));
			param.enable_live_cache=true;
			int max_len = get_int_type(reqSess.qmap, "max_len");
			int max_duration = get_int_type(reqSess.qmap, "max_duration");
			param.max_length_per_file=max_len==-1?MAX_LEN_PER_FILE : max_len;
			param.max_duration=max_duration==-1?MAX_DURATION:max_duration;
		}
		else
		{
			param.enable_live_cache=false;
		}

		if(!((str=get(reqSess.qmap, "private_key")).empty()))
			param.channel_key = get(reqSess.qmap, "private_key");
		if(!((str=get(reqSess.qmap, channel_in_addr_key)).empty()))
			param.internal_ipport =str;
		if(!((str=get(reqSess.qmap, channel_ex_addr_key)).empty()))
			param.external_ipport = str;
		if(!((str=get(reqSess.qmap, channel_tracker_addr_key)).empty()))
			param.tracker_ipport = str;

		if(!((str=get(reqSess.qmap, channel_stream_recv_url_key)).empty()))
			param.stream_recv_url =str;
		else if(is_live_category(type)
			&&!((str=get(reqSess.qmap, channel_stream_recv_port_key)).empty()))
			param.stream_recv_url=str;

		if(param.tracker_ipport.empty())
		{
			db_->del_channel(param);

			errorMsg="tracker address is empty, required!";
			std::cout<<errorMsg<<std::endl;
			return false;
		}

		suspended_channels_.erase(channel_link);
		db_->set_channel(param);

		BOOST_ASSERT(channel_set_.link_index().find(channel_link)==channel_set_.end());
		//���stream recv port �Ƿ��Ѿ���ռ��
		if(is_live_category(type))
		{
			//��socket���Զ˿��Ƿ������
			error_code ec;
			uri u(param.stream_recv_url, ec);
			
			if(!ec&&u.protocol()!="http" //httpЭ���recv_urlֻ��ȥ��������˿�
				&&!is_port_usable()(get_io_service(), param.stream_recv_url, ec))
			{
				db_->del_channel(param);
				errorMsg="stream recv port have been used!";
				std::cout<<errorMsg<<std::endl;
				return false;
			}
		}

		int maxProcessCnt = max_process_cnt(type, current_channel_cnt(type)+1);
		sess.node_map = hash_node_map(maxProcessCnt, channel_vec_type(1, param));

		create_mds_or_set_channel(sess, type, errorMsg);

		LogDebug("\non set channel:"
			"\n channel link=%s; "
			" type=%d"
			" tracker=%s"
			" stream_recv_url=%s"

			, param.channel_link.c_str()
			, param.type
			, param.tracker_ipport.c_str()
			, param.stream_recv_url.c_str());

		db_->start_channel(param);

		return true;
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

bool mds_control::__do_start_mds(const req_session& reqSess, 
	const mds_element& elm, std::string& errorMsg)
{
	BOOST_ASSERT(!elm.link_url_map.empty());
	try{
		distribution_type type=(distribution_type)elm.type;
		if(-1==type)
			return false;

		std::string error_msg;
		if(-1!=elm.id)
		{
			node_map_t sess;
			channel_vec_type& params=sess.node_map[elm.id];
			params.reserve(elm.link_url_map.size());

			BOOST_AUTO(itr, elm.link_url_map.begin());
			for(;itr!=elm.link_url_map.end();++itr)
			{
				server_param_base param;
				param.type = type;
				param.channel_link = itr->first;

				db_->start_channel(param);
				params.push_back(param);
				suspended_channels_.erase(param.channel_link);
			}

			sess.session_id=reqSess.session_id;
			process_mapped_channel(sess, type, CMD_START_CHANNEL);
			return true;
		}
		else
		{
			errorMsg="channel not found!";
			return false;
		}
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

bool mds_control::__do_start_channel(const req_session& reqSess, 
	distribution_type type, 
	const std::string& channel_link, 
	std::string& errorMsg)
{
	//BOOST_ASSERT(!channel_link.empty());
	if(channel_link.empty())
	{
		errorMsg="channel link is missing!";
		return false;
	}
	try{
		int mds_id=channel_belong_to(type, channel_link);
		if(-1!=mds_id)
		{
			server_param_base param;
			param.type = type;
			param.channel_link = channel_link;
			node_map_t sess;
			sess.session_id=reqSess.session_id;

			db_->start_channel(param);

			sess.node_map[mds_id] = channel_vec_type(1, param);
			suspended_channels_.erase(channel_link);

			process_mapped_channel(sess, type, CMD_START_CHANNEL);
			return true;
		}
		else
		{
			errorMsg="channel not found!";
			return false;
		}
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

bool mds_control::__do_stop_mds(const req_session& reqSess, 
	const mds_element& elm, 
	std::string& errorMsg)
{
	BOOST_ASSERT(!elm.link_url_map.empty());
	try{
		distribution_type type=(distribution_type)elm.type;
		if(-1==type)
			return false;

		std::string error_msg;
		if(-1!=elm.id)
		{
			node_map_t sess;
			channel_vec_type& params=sess.node_map[elm.id];
			params.reserve(elm.link_url_map.size());

			BOOST_AUTO(itr, elm.link_url_map.begin());
			for(;itr!=elm.link_url_map.end();++itr)
			{
				server_param_base param;
				param.type = type;
				param.channel_link = itr->first;

				db_->stop_channel(param);

				params.push_back(param);
				suspended_channels_.insert(param.channel_link);
			}

			sess.session_id=reqSess.session_id;
			process_mapped_channel(sess, type, CMD_STOP_CHANNEL);
			return true;
		}
		else
		{
			errorMsg="channel not found!";
			return false;
		}
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

bool mds_control::__do_stop_channel(const req_session& reqSess, 
	distribution_type type, 
	const std::string& channel_link, 
	std::string& errorMsg)
{
	BOOST_ASSERT(!channel_link.empty());
	if(channel_link.empty())
	{
		errorMsg="channel link is missing!";
		return false;
	}

	try{
		if(-1==type)
			return false;

		std::string error_msg;
		int mds_id=channel_belong_to(type, channel_link);
		if(-1!=mds_id)
		{
			server_param_base param;
			param.type = type;
			param.channel_link = channel_link;
			node_map_t sess;
			sess.session_id=reqSess.session_id;

			db_->stop_channel(param);

			sess.node_map[mds_id] = channel_vec_type(1, param);
			suspended_channels_.insert(channel_link);

			process_mapped_channel(sess, type, CMD_STOP_CHANNEL);
			return true;
		}
		else
		{
			errorMsg="channel not found!";
			return false;
		}
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

int mds_control::channel_belong_to(distribution_type type, const std::string& channel_link)
{
	//������set�в���channel_link���о���reset��û�о���add
	int mds_id = -1;
	BOOST_AUTO(itr, channel_set_.link_index().find(channel_link)); 
	if (channel_set_.link_index().end()!=itr)
	{
		mds_id=itr->mds_id;
	}
	
	CONTROL_DBG(
		//����������channel�����mds�в���Ҳ���ҵ������ֲ��ҷ�ʽ���һ��һ�¡�
		if(is_live_category(type))
		{
			int mds_id_from_mds = -1;
			mds_set& mds=current_server_set(type);

			BOOST_AUTO(&index, mds.mds_id_index());
			BOOST_FOREACH(const mds_element& mds_elm, index)
			{
				if(mds_elm.link_url_map_.end()!=mds_elm.link_url_map_.find(channel_link))
				{
					mds_id_from_mds = mds_elm.server_id_;
					break;
				}
			}
			BOOST_ASSERT(mds_id==mds_id_from_mds);
		}
		);

		return mds_id;
}

void mds_control::close_mds_process(distribution_type type)
{
	//ͨ���������رս��̣���¼��mds�Ľ���id������ֱ���͵㲥��

	//��ȡ��������Ϊprocess_name�Ľ���
	pid_set outProcessIDsContainer;      
	find_process_ids(outProcessIDsContainer, config_param().child_process_name_); 

	mds_set& mds = current_server_set(type);

	BOOST_AUTO(&index, mds.mds_id_index());
	BOOST_FOREACH(const mds_element& mds_elm, index)
	{
		if(outProcessIDsContainer.end()!=outProcessIDsContainer.find(mds_elm.pid))
			kill_process_by_id(mds_elm.pid, true);
	}

	mds.clear();
}

void mds_control::recover_from_db()
{
	bool retVod=recover_servers(VOD_TYPE);
	if(!retVod)
		start_recover_timer(VOD_TYPE);

	bool retLive=recover_servers(LIVE_TYPE);
	if(retVod&&!retLive)
		start_recover_timer(LIVE_TYPE);

	if(0==retVod+retLive)
		exit(0);
}

void mds_control::start_recover_timer(distribution_type type)
{
	if(recover_timer_)
	{
		recover_timer_->cancel();
		recover_timer_.reset();
	}

	recover_timer_=timer::create(get_io_service());
	recover_timer_->set_obj_desc("mds_control::recover_timer_");
	recover_timer_->register_time_handler(boost::bind(&this_type::recover_servers, this, type));
	recover_timer_->async_wait(seconds(2));
}

bool mds_control::recover_servers(distribution_type type)
{
	try{
		if(recover_timer_)
		{
			recover_timer_->cancel();
			recover_timer_.reset();
		}

		std::vector<server_param_base> params;
		std::string errorMsg;

		if(!get_channels(type, params, errorMsg)) //�����ݿ��ȡƵ��
		{
			LogError(errorMsg.c_str());
			db_->log(errorMsg);
			CONTROL_DBG(
				std::cout<<errorMsg<<std::endl;
			);
			return false;
		}

		if(params.empty()) return true;

		//��ȡƵ����������mds��map��������һ�ű���Ϊ�˼������е�db
		std::map<std::string, int> mds_hashed_chnls;
		if(!db_->get_hashed_channels(type, mds_hashed_chnls, &errorMsg))//db�Ѿ��������쳣
		{
			CONTROL_DBG(
				std::cout<<errorMsg<<"\n";
			);
			LogError(errorMsg.c_str());
			db_->log(errorMsg);
			return false;
		}
		//ȫ������
		BOOST_FOREACH(const server_param_base& param, params)
		{
			try
			{
				db_->start_channel(param);
			}
			catch (...)
			{
				return false;
			}
		}

		node_map_type hashed_chnls;
		if(mds_hashed_chnls.empty())
		{
			/************************************************************************/
			/* ���µ�ý���ļ�������server�У��п�������ԭ��server��                 */
			/* Ҳ�������µ�server��ȡ����hash�Ľ��                                 */
			/************************************************************************/
			int max_process = max_process_cnt(type, params.size());
			hashed_chnls = hash_node_map(max_process, params);

			//���д��db
			for (BOOST_AUTO(itr, hashed_chnls.begin());
				itr!=hashed_chnls.end();++itr)
			{
				db_->add_hashed_channel(itr->first, itr->second);
			}
		}
		else
		{
			for (size_t i=0;i<params.size();++i) 
			{
				int mdsID=mds_hashed_chnls[params[i].channel_link];
				std::vector<server_param_base>& chnls=hashed_chnls[mdsID];
				chnls.push_back(params[i]);
			}
		}

		BOOST_FOREACH(const node_map_type::value_type& val, hashed_chnls)
		{
			try
			{
				RECOVER_SET_OF_TYPE(type)
					add_or_update_mds_set(val.first, recoverMds, type, 0);

				add_channels_to_mds(val.first, recoverMds, val.second);
			}
			catch(std::exception& e)
			{
				std::cout<<e.what()<<std::endl;
				return false;
			}
			catch (...)
			{
				return false;
			}
		}
		return true;
	}
	catch(std::exception& e)
	{
		LogInfo("on recover_server type=%d, error=%s", type, e.what());
		db_->log(e.what());
		return false;
	}
	catch(...){return false;}
}

void mds_control::add_or_update_mds_set(
	int serverID, mds_set& mds, int type, int pid)
{
	BOOST_AUTO(itr, mds.mds_id_index().find(serverID));
	if(itr!=mds.mds_id_index().end())
	{
		//ͨ��ɾ�������²��������£���Ϊprocess_id_Ҳ��һ��key
		mds_element elm(serverID, pid);
		elm.copy_non_index_data_from(*itr);
		elm.type = type;
		elm.last_report_time=timestamp_now();

		mds.mds_id_index().erase(itr);
		mds.mds_id_index().insert(elm);
	}
	else
	{
		mds_element elm(serverID, pid==0?serverID:pid);//��ʱpid����֪����ʹ��serverID���棬֪��ʱ���¡�
		elm.type = type;
		elm.last_report_time=timestamp_now();

		size_t last_cnt = mds.mds_id_index().size();
		mds.mds_id_index().insert(elm);
		BOOST_ASSERT(mds.mds_id_index().size()==last_cnt+1);
	}

}
void mds_control::add_channels_to_mds(int serverID, mds_set& mds, 
	const channel_vec_type& params)
{
	BOOST_AUTO(itr, mds.mds_id_index().find(serverID));

	DEBUG_SCOPE(
		if(itr==mds.mds_id_index().end())
		{
			BOOST_ASSERT(0&&"�Ѿ���ӹ���");
		}
		);

		mds_element& elm = const_cast<mds_element&>(*itr);
		BOOST_FOREACH(const server_param_base& param, params)
		{
			insert_channel(param, elm);
		}
}	

bool mds_control::service_type_valide(int serviceType)
{
	if(serviceType>=-1&&serviceType<=VOD_TYPE)
		return true;

	return false;
}

bool mds_control::on_add_request(const req_session& reqSess, std::string& errorMsg)
{
	regist_code_config(reqSess.qmap, errorMsg);
	errorMsg.clear();

	//���ֱ���㲥
	int serviceType  = get_int_type(reqSess.qmap, "service_type");
	if(-1==serviceType)
	{
		errorMsg="serviceType is missing, required!";
		return false;
	}
	return do_add_or_reset_channel(reqSess, (distribution_type)serviceType, errorMsg);
}

bool mds_control::on_del_request(const req_session& reqSess, std::string& errorMsg)
{
	try
	{
		int type = get_int_type(reqSess.qmap, "service_type");
		server_param_base param;
		param.type = (distribution_type)type;
		param.channel_link = get(reqSess.qmap, "link");

		int mds_id = channel_belong_to(param.type, param.channel_link);
		if(-1!=mds_id)
		{
			node_map_t sess;
			sess.session_id=reqSess.session_id;
			sess.node_map[mds_id] = channel_vec_type(1, param);
			suspended_channels_.erase(param.channel_link);

			process_mapped_channel(sess, (distribution_type)type, CMD_DEL_CHANNEL);

			db_->del_channel(param);
		}
		else
		{
			c2s_cmd_reply_msg msg;
			msg.set_code(0);
			msg.set_session_id(reqSess.session_id);
			reply_http_request(msg);
		}
		return true;
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}


bool mds_control::on_start_request(const req_session& reqSess, std::string& errorMsg)
{
	try
	{
		int type = get_int_type(reqSess.qmap, "service_type");
		if(-1==type)
		{
			if(do_start_channel(reqSess, VOD_TYPE, errorMsg)
				||do_start_channel(reqSess, LIVE_TYPE, errorMsg)
				)
				return true;
		}
		else
		{
			return do_start_channel(reqSess, (distribution_type)type, errorMsg);
		}
		return false;
	}
	catch(std::exception& e)
	{
		LogInfo("control error, msg=%s", e.what());
		errorMsg = e.what();
		db_->log(errorMsg);
		return false;
	}
	catch(...){return false;}
}

void mds_control::create_mds_or_set_channel(const node_map_t& node_map, 
	distribution_type type, 
	std::string& errorMsg)
{
	BOOST_FOREACH(const node_map_type::value_type& val, node_map.node_map)
	{
		db_->add_hashed_channel(val.first, val.second, &errorMsg);
		{
		/*	reliable_db_operation(boost::bind(
				&this_type::__db__del_channels, this, val.second)
				);*/
		}

		if(!mds_exist(type, val.first))
		{
			channel_vec_t channels;
			channels.session_id=node_map.session_id;
			channels.chnls=val.second;
			create_mds_process(config_param().child_process_name_, 
				type, val.first, channels);
		}
		else
		{
			post_cmd_to_mds(node_map.session_id, 
				val.first, val.second, CMD_ADD_CHANNEL); 
		}	
	}
}
void mds_control::process_node_map(const node_map_t& sess, 
	distribution_type type, 
	const char* cmd_str)
{
	if(sess.node_map.empty())
		return;

	BOOST_FOREACH(const node_map_type::value_type& val, sess.node_map)
	{
		post_cmd_to_mds(sess.session_id, val.first, val.second, cmd_str);
	}

	CONTROL_DBG(
		std::cout<<"cmd="<<cmd_str
		<<"type="<<type
		<<"server_cnt="<<sess.node_map.size()
		<<" To shared memory"<<std::endl;
	);
}

void mds_control::post_cmd_to_mds(const std::string& sessionID, int serverID, 
	const channel_vec_type& channels, const cms_cmd_type& mdsCmd, bool isReply)
{
	if(channels.empty())
		return;

	int type= channels[0].type;
	BOOST_ASSERT(serverID>=0);

	mds_cmd_msg msg;
	msg.set_cmd(mdsCmd);
	msg.set_session_id(sessionID);
	msg.set_is_login_reply(isReply);

	if(CMD_ADD_TRACKER==mdsCmd
		||CMD_DEL_TRACKER==mdsCmd
		||CMD_CHANGE_TRACKER==mdsCmd)
	{
		msg.set_config_val(channels[0].tracker_ipport.c_str(), 
			channels[0].tracker_ipport.length());
	}
	else
	{
		std::string errorMsg;
		std::vector<std::string> tracker_edps;
		BOOST_FOREACH(const server_param_base& param, channels)
		{
			if(CMD_ADD_CHANNEL==mdsCmd)
			{
				ctrl2m_create_channel_msg* chnl_info=msg.add_channel_info();

				chnl_info->set_name(param.name.string());
				chnl_info->set_type(param.type);
				chnl_info->set_channel_link(param.channel_link);
				chnl_info->set_channel_uuid(param.channel_uuid);
				chnl_info->set_internal_address(param.internal_ipport);
				chnl_info->set_external_address(param.external_ipport);
				chnl_info->add_tracker_address(param.tracker_ipport);
				chnl_info->set_channel_key(param.channel_key);

				errorMsg.clear();
				tracker_edps.clear();
				if(!db_->get_tracker_endpoint(param, tracker_edps, &errorMsg))
				{
					get_io_service().post(boost::bind(&this_type::delay_add_tracker, 
						SHARED_OBJ_FROM_THIS, serverID, param, 10));
				}

				for (size_t i=0;i<tracker_edps.size();++i)
					chnl_info->add_tracker_address(tracker_edps[i]);

				if(is_vod_category(type))
				{
					chnl_info->set_media_directory(convert_from_wstring(param.media_directory.wstring()));
					chnl_info->set_duration(param.film_duration);
					chnl_info->set_length(param.film_length);
				}
				else
				{
					chnl_info->set_stream_recv_url(param.stream_recv_url);
					chnl_info->set_enable_live_cache(param.enable_live_cache);
					if(param.enable_live_cache)
					{
						chnl_info->set_live_cache_dir(convert_from_wstring(param.live_cache_dir.wstring()));
						chnl_info->set_max_duration(param.max_duration);
						chnl_info->set_max_length_per_file(param.max_length_per_file);
					}
				}

				modify_channel_owner(param, serverID, true);
			}
			else if(CMD_DEL_CHANNEL==mdsCmd)
			{
				modify_channel_owner(param, serverID, false);
			}
			msg.add_channel_ids(param.channel_link.c_str(), 
								param.channel_link.length());
			CONTROL_DBG(
				std::cout<<"post cmd="<<msg.cmd()
				<<" type="<<type
				<<" id="<<serverID
				<<" link="<<param.channel_link<<std::endl;
			);
		}
	}
	std::cout<<"post cmd="<<msg.cmd()
		<<" type="<<type
		<<" id="<<serverID<<std::endl;

	interprocess_server_->post_cmd(type, boost::lexical_cast<std::string>(serverID) , serialize(msg));
	waiting_chnls_.get(type).erase(serverID);
}

void mds_control::post_config_cmd_to_mds(
	const std::string& sessionID, int type, int serverID, 
	const std::string& configValue, const cms_cmd_type& cmd)
{
	BOOST_ASSERT(serverID>=0);

	mds_cmd_msg msg;
	msg.set_cmd(cmd);
	msg.set_session_id(sessionID);
	msg.set_is_login_reply(false);
	msg.set_config_val(configValue);

	interprocess_server_->post_cmd(type, boost::lexical_cast<std::string>(serverID) , serialize(msg));
	waiting_chnls_.get(type).erase(serverID);
}

void mds_control::modify_channel_owner(const server_param_base& param, 
	int	mds_id, 
	bool add)
{
	distribution_type type=param.type;
	if(mds_id<0||type<0||param.channel_link.empty())
		return;

	mds_set& all_mds=current_server_set(type);

	BOOST_AUTO(itr, all_mds.mds_id_index().find(mds_id));
	if(all_mds.mds_id_index().end()==itr) //û�ҵ����mds
	{
		mds_element elm(mds_id, mds_id);
		elm.type=type;
		all_mds.mds_id_index().insert(elm);

		modify_channel_owner(param, mds_id, add);
		return;
	}

	mds_element& mds_elm = const_cast<mds_element&>(*itr);
	BOOST_AUTO(chn_itr, mds_elm.link_url_map.find(param.channel_link));
	if(add && mds_elm.link_url_map.end()==chn_itr)
	{
		insert_channel(param, mds_elm);
	}
	else if(!add && mds_elm.link_url_map.end()!=chn_itr)
	{
		erase_channel(type, param.channel_link, mds_id);
	}
}

void mds_control::remove_mds_idles(distribution_type type)
{
	pid_set pids;
	bool pids_ok=false;

	mds_set& mds = current_server_set(type);
	BOOST_AUTO(&index, mds.mds_id_index());
	BOOST_FOREACH(const mds_element& mds_elm, index)
	{
		if(!mds_elm.link_url_map.empty())
			continue;

		if (!pids_ok)
		{
			find_process_ids(pids, config_param().child_process_name_);
			pids_ok=true;
		}
		if (pids.find(mds_elm.pid)!=pids.end())
			kill_process_by_id(mds_elm.pid);

		mds.mds_id_index().erase(mds_elm.id);
		break;
	}
}

void mds_control::start_check_timer()
{
	if(alive_timer_)
		return;

	alive_timer_ = timer::create(get_io_service());
	alive_timer_->set_obj_desc("p2control::mds_control::alive_timer_");
	alive_timer_->register_time_handler(boost::bind(&this_type::on_check_timer, this));

	pid_set pids;
	find_process_ids(pids, config_param().child_process_name_);
	int delayTime=pids.empty()?3:10;
	alive_timer_->async_keep_waiting(seconds(delayTime), milliseconds(3000));
}

void mds_control::stop_check_timer()
{
	if(alive_timer_)
	{
		alive_timer_->cancel();
		alive_timer_.reset();
	}
}

void mds_control::on_wild_sub_process_timer()
{
	pid_set pids;
	find_process_ids(pids, config_param().child_process_name_);
	BOOST_AUTO(&vodProcessIndex, vod_mds_set_.process_id_index());
	BOOST_AUTO(&liveProcessIndex, live_mds_set_.process_id_index());

	BOOST_FOREACH(pid_t pid, pids)
	{
		if(vodProcessIndex.find(pid)!=vodProcessIndex.end()
			||liveProcessIndex.find(pid)!=liveProcessIndex.end()
			)
		{
			continue;
		}
		//���ܸմ����껹û���ü�����mds_set�ͱ�wild_mds_timer��鵽���ر�
		//���ﲻֱ�ӹرգ�������һ�μ����ȷ���Ƿ�Ҫɾ��

		//�����wild_mds_set_
		try_kill_wild_sub_process(pid);
	}
}


mds_control::mds_set& mds_control::current_server_set(distribution_type type)
{
	if(is_vod_category(type))
		return vod_mds_set_;

	return live_mds_set_;
}

size_t mds_control::current_channel_cnt(distribution_type type)
{
	size_t cnt=0;
	mds_set& mds = current_server_set(type);
	BOOST_AUTO(&index, mds.mds_id_index());
	BOOST_FOREACH(const mds_element& mds_elm, index)
	{
		cnt+=mds_elm.link_url_map.size();
	}
	return cnt;
}

bool mds_control::mds_exist(distribution_type type, int mds_id)
{
	mds_set& mds = current_server_set(type);
	return mds.mds_id_index().end()!=mds.mds_id_index().find(mds_id);
}

void mds_control::create_mds_process(const std::string& pszFile, distribution_type type, 
	int serverID, const channel_vec_t& chns, int deadline)
{
	//����serverID-mds
	timestamp_t now = timestamp_now();
	bool createOk=true;
#ifdef WIN32
	std::string param = pszFile;
	param.append(" --server_id=");
	param.append(boost::lexical_cast<std::string>(serverID));
	param.append(" --type=");
	param.append(boost::lexical_cast<std::string>(type));
	param.append(" --alive_alarm_port=");
	param.append(boost::lexical_cast<std::string>(config_param().alive_alarm_port_));
	param.append(" --register_code=");
	param.append(old_regist_code_.c_str(), old_regist_code_.length());

	STARTUPINFO si; //һЩ�ر���������
	memset(&si, 0, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESHOWWINDOW;
#ifdef P2ENGINE_DEBUG
	si.wShowWindow = SW_SHOW;
#else
	si.wShowWindow = SW_HIDE;
#endif
	PROCESS_INFORMATION pi; //�ر��������ý���

	if(!CreateProcess(NULL, (LPSTR)param.c_str(), 
		NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) 
		createOk=false;
	
	/* Add by Martin on 2012-08-20 */
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );



	/*if(!PROCESS_SUCCEEDED(WinExec(param.c_str(), SW_HIDE)))
	createOk=false;*/
#else
	pid_t processId =::fork();
	if( processId < 0 )
	{
		createOk=false;
	}
	if( processId == 0 )
	{
		std::string server_id_arg(" --server_id=");
		server_id_arg+=boost::lexical_cast<std::string>(serverID);
		std::string type_arg(" --type=");
		type_arg+=boost::lexical_cast<std::string>(type);
		std::string db_file_arg(" --db_file=");
		db_file_arg+=db_->db_name();

		const char* argv[32];
		int i = 0;
		argv[i++] = pszFile.c_str();
		argv[i++] = server_id_arg.c_str();
		argv[i++] = type_arg.c_str();
		argv[i++] = db_file_arg.c_str();
		argv[i++] = NULL;
		execv("", argv );
		exit(1);
	}
#endif

	if(createOk)
	{
		RUN_SET_OF_TYPE(type);
		add_or_update_mds_set(serverID, runningMds, type, pi.dwProcessId);//���½���id
		waiting_chnls_.get(type).insert(std::make_pair(serverID, chns)); //mds���������ӵ�controlʱ����Ƶ����ȥ

		CONTROL_DBG(
			std::cout<<"CCCCCCCCCCCCCCC----mds id="<<serverID<<" type="<<type<<" channel_cnt="<<chns.chnls.size()<<"\n";
		LogDebug("\n mds_id=%d, type=%d, channel_cnt=%d", serverID, type, chns.chnls.size());
		);
	}
	else
	{
		CONTROL_DBG(
			std::cout<<"create process mds failed!";
		);
		LogInfo("create process %s failed, reason is %d", pszFile.c_str(), GetLastError());
		db_->log("create process failed");
		if(deadline>0)
			get_io_service().post(boost::bind(&this_type::create_mds_process, SHARED_OBJ_FROM_THIS, 
			pszFile, type, serverID, chns, --deadline));
	}
}

void mds_control::on_check_timer()
{
	get_io_service().post(boost::bind(&this_type::mds_alive_check, 
		SHARED_OBJ_FROM_THIS, LIVE_TYPE));
	get_io_service().post(boost::bind(&this_type::mds_alive_check, 
		SHARED_OBJ_FROM_THIS, VOD_TYPE));
}	

void mds_control::on_recvd_error_message(const std::string& msg)
{
	if(msg.empty()) return;

	std::cout<<"-----------recvd client error message: "<<msg<<std::endl;
	db_->log(msg);
}

void mds_control::on_recvd_alarm_message(const safe_buffer& buf)
{
	try
	{
		alive::mds_Alive msg;
		if(!parser(buf, msg))
			return;

		//����link�ҵ�mdsID
		BOOST_AUTO(itr, channel_set_.link_index().find(msg.id()));
		BOOST_ASSERT(itr!=channel_set_.link_index().end());
		if(itr==channel_set_.link_index().end())
			return;

		timestamp_t now=timestamp_now();
		//��recover��msg������link��mds�ƶ���running��
		channel_element& elm=const_cast<channel_element&>(*itr);
		elm.last_alive_time_=now;

		RECOVER_SET_OF_TYPE(msg.type());

		BOOST_AUTO(mdsItr, recoverMds.mds_id_index().find(elm.mds_id));
		if(mdsItr!=recoverMds.mds_id_index().end()) //
		{
			RUN_SET_OF_TYPE(msg.type());
			BOOST_ASSERT(runningMds.mds_id_index().find(elm.mds_id)==runningMds.mds_id_index().end());//����runing��

			mds_element& mdsElm=const_cast<mds_element&>(*mdsItr);
			mdsElm.last_report_time=now;

			recoverMds.mds_id_index().erase(elm.mds_id);
		}
		else
		{
			RUN_SET_OF_TYPE(msg.type());
			BOOST_AUTO(mdsItr, runningMds.mds_id_index().find(elm.mds_id));
			BOOST_ASSERT(mdsItr!=runningMds.mds_id_index().end());//����runing��
			if(mdsItr!=runningMds.mds_id_index().end())
			{
				mds_element& mdsElm=const_cast<mds_element&>(*mdsItr);
				mdsElm.last_report_time=now;
			}
		}

		if(suspended_channels_.end()!=suspended_channels_.find(msg.id()))
			return;

		std::string errorMsg;
		distribution_type type=(distribution_type)msg.type();
		db_->set_channel(type, msg);

		alarm_msg_set_.push_back(msg);
		if(alarm_msg_set_.size()>5000
			||is_time_passed(60000, last_write_time_, now))
		{
			db_->report_channels(type, alarm_msg_set_);
			alarm_msg_set_.clear();
			last_write_time_=now;
		}

		/*DEBUG_SCOPE(
		std::cout<<"alive: "<<msg.id()<<std::endl;
		);*/
	}
	catch(const std::exception& e)
	{std::cout<<e.what()<<"\n";}
	catch(...){}
}

void mds_control::__on_client_login(message_socket_sptr conn, const safe_buffer& buf)
{
	//������id�����ʹ���Ƶ������
	alive_alarm_report_msg msg;
	if(!parser(buf, msg))
		return;

	BOOST_ASSERT(msg.type()>=0);
	BOOST_ASSERT(!msg.id().empty());

	//��ʱ��recoverһ�������
	RECOVER_SET_OF_TYPE(msg.type());

	//��recover��msg������link��mds�ƶ���running��
	BOOST_AUTO(mdsItr, recoverMds.mds_id_index().find(atoi(msg.id().c_str())));
	if(mdsItr!=recoverMds.mds_id_index().end()) //
	{
		RUN_SET_OF_TYPE(msg.type());
		mds_element& mdsElm=const_cast<mds_element&>(*mdsItr);
		BOOST_ASSERT(runningMds.mds_id_index().find(mdsElm.id)==runningMds.mds_id_index().end());//����runing��
		mdsElm.last_report_time=timestamp_now();

		mds_element newElm(mdsElm.id, msg.pid());
		newElm.copy_non_index_data_from(mdsElm);
		runningMds.mds_id_index().insert(newElm);

		//��ʱƵ������channel_set��
		channel_vec_t failedChannel;
		failedChannel.chnls.reserve(mdsElm.link_url_map.size());

		for (BOOST_AUTO(lpmItr, mdsElm.link_url_map.begin());
			lpmItr!=mdsElm.link_url_map.end();++lpmItr)
		{
			BOOST_AUTO(link, channel_set_.link_index().find(lpmItr->first));
			BOOST_ASSERT(link!=channel_set_.link_index().end());
			if(link!=channel_set_.link_index().end())
				failedChannel.chnls.push_back((*link).param);
		}
		waiting_chnls_.get(msg.type()).insert(std::make_pair(mdsElm.id, failedChannel));
		recoverMds.mds_id_index().erase(mdsElm.id);
	}
	else
	{
		RUN_SET_OF_TYPE(msg.type());
		int ID=atoi(msg.id().c_str());
		

		BOOST_AUTO(mdsItr, runningMds.mds_id_index().find(ID));
		BOOST_ASSERT(mdsItr!=runningMds.mds_id_index().end());//����runing��

		mds_element& mdsElm=const_cast<mds_element&>(*mdsItr);
		mdsElm.last_report_time=timestamp_now();
		if(mdsElm.pid!=msg.pid())
		{
			mds_element newElm(ID, msg.pid());
			newElm.copy_non_index_data_from(mdsElm);

			runningMds.mds_id_index().erase(ID);
			runningMds.mds_id_index().insert(newElm);
		}

		BOOST_ASSERT(runningMds.mds_id_index().end()!=runningMds.mds_id_index().find(ID));
	}
	//////////////////////////////////////////////////////////////////////////


	int serverID=atoi(msg.id().c_str());
	channel_vec_t& chnls=waiting_chnls_.get(msg.type())[serverID];
	if(chnls.chnls.empty()) //����������֮ǰ�Ѿ���mds������
		return;

	post_cmd_to_mds(chnls.session_id, serverID, chnls.chnls, CMD_ADD_CHANNEL, true);
}

void mds_control::on_client_login(message_socket_sptr conn, const safe_buffer& buf)
{
	__on_client_login(conn, buf);
}

void mds_control::on_client_dropped(const __id& ID)
{
	boost::format msg("XXXXXXXXXXXXXXXXXXXXXXXX---client dropped, id=%s, type=%d");
	msg%ID.id%ID.type;
	std::cout<<msg.str()<<std::endl;
	db_->log(msg.str());

	//�رյ��ߵĽ��̡���
	RUN_SET_OF_TYPE(ID.type);
	BOOST_AUTO(itr, runningMds.mds_id_index().find(atoi(ID.id.c_str())));
	if(itr!=runningMds.mds_id_index().end())
	{
		mds_element& elm = const_cast<mds_element&>(*itr);
		kill_process_by_id(elm.pid);

		//note �����Ҫ��mds���̹رպ�ȴ�һ��Intervalʱ��������mds��ȥ������Ĵ��롣
		//����̵�ʱ���ڻָ�Ƶ��
		channel_vec_type failedChannel;
		failedChannel.reserve(elm.link_url_map.size());

		for (BOOST_AUTO(lpmItr, elm.link_url_map.begin());
			lpmItr!=elm.link_url_map.end();++lpmItr)
		{
			BOOST_AUTO(link, channel_set_.link_index().find(lpmItr->first));
			BOOST_ASSERT(link!=channel_set_.link_index().end());

			failedChannel.push_back((*link).param);
		}

		channel_vec_t channels;
		channels.session_id=INTERNAL_SESSION_ID;
		channels.chnls=failedChannel;
		
		create_mds_process(config_param().child_process_name_, 
			(distribution_type)ID.type, atoi(ID.id.c_str()), channels);
		//note end
	}
}

void mds_control::mds_alive_check(distribution_type type)
{
	typedef std::map<int, channel_vec_type> mapped_vec_type;
	typedef mapped_vec_type::value_type    mappend_vec_value_type;
	mapped_vec_type failed_process;
	mapped_vec_type failed_channel;
	//�ҵ���mds
	timestamp_t now=timestamp_now();
	try{

		//��recover��mds
		RECOVER_SET_OF_TYPE(type);
		BOOST_FOREACH(const mds_element& elm, recoverMds)
		{
			channel_vec_type* failedChannel=NULL;
			for (BOOST_AUTO(lpmItr, elm.link_url_map.begin());
				lpmItr!=elm.link_url_map.end();++lpmItr)
			{
				if (!failedChannel)
					failedChannel=&failed_process[elm.id];

				DEBUG_SCOPE(
					std::cout<<"recover, link="<<lpmItr->first
					<<" stream_url="<<lpmItr->second<<std::endl;
				);
				BOOST_AUTO(link, channel_set_.link_index().find(lpmItr->first));
				BOOST_ASSERT(link!=channel_set_.link_index().end());

				failedChannel->push_back((*link).param);
			}
		}

		//��������ر�ʱon_client_dropped�ᴦ��
		int CHECK_INTERVAL=is_vod_category(type)?150000:30000;
		// ���Ƶ���Ƿ�����ȷ����ת
		for (BOOST_AUTO(itr, channel_set_.link_index().begin());
			itr!=channel_set_.link_index().end();++itr)
		{
			channel_element& elm=const_cast<channel_element&>(*itr);
			if(suspended_channels_.end()!=suspended_channels_.find(elm.link))
				continue;

			//��Ϊchannel_setû�����ֵ㲥��ֱ��
			if(elm.param.type==type
				&&is_time_passed(CHECK_INTERVAL, elm.last_alive_time_, now))
			{
				channel_vec_type* failedChannel=&failed_channel[elm.mds_id];
				failedChannel->push_back(elm.param);
				elm.last_alive_time_=now;
				DEBUG_SCOPE(
					std::cout<<"channel failed, link="<<elm.param.channel_link<<std::endl;
					);
			}
		}

		if (!failed_process.empty())
		{
			BOOST_FOREACH(const mappend_vec_value_type& val, failed_process)
			{
				channel_vec_t channels;
				channels.session_id=INTERNAL_SESSION_ID;
				channels.chnls=val.second;
				create_mds_process(config_param().child_process_name_, type, val.first, channels);
			}
		}
		recoverMds.mds_id_index().clear();

		//��ЩƵ��û�����������ʹ���
		if(!failed_channel.empty())
		{
			BOOST_FOREACH(const mappend_vec_value_type& val, failed_channel)
			{
				post_cmd_to_mds(INTERNAL_SESSION_ID, val.first, val.second, CMD_ADD_CHANNEL);
			}
		}
	}
	catch(...){
	}
}

bool mds_control::get_channels(distribution_type type, 
	std::vector<server_param_base>& params, 
	std::string& errorMsg)
{
	return ( db_->get_channels(type, params, &errorMsg) );
	/*
	BOOST_FOREACH(server_param_base& param, params)
	{
		param.type = type;
	}
	*/
}

void mds_control::insert_channel(const server_param_base& param, mds_element& mds)
{
	mds.insert(param.channel_link, param.stream_recv_url);
	channel_set_.link_index().insert(
		channel_element(mds.id, param)
		);
}
void mds_control::erase_channel(distribution_type type, const std::string& link, int mds_id)
{
	BOOST_ASSERT(channel_set_.link_index().find(link)==channel_set_.link_index().end()
		||channel_set_.link_index().find(link)->mds_id==mds_id
		);

	mds_set& mds=current_server_set((distribution_type)type);
	BOOST_AUTO(mds_itr, mds.mds_id_index().find(mds_id));
	BOOST_ASSERT(mds_itr!=mds.mds_id_index().end());
	mds_element& mds_elm = const_cast<mds_element&>(*mds_itr);
	mds_elm.erase(link);

	channel_set_.link_index().erase(link);
}

void mds_control::delay_add_tracker(int serverID, const server_param_base& param, int deadline)
{
	std::string errorMsg;
	std::vector<std::string> tracker_edps;
	BOOST_ASSERT(param.type>=0 && !param.channel_link.empty());
	if(!db_->get_tracker_endpoint(param, tracker_edps, &errorMsg)&&deadline>0)
	{
		if(!delay_timer_)
		{
			delay_timer_=timer::create(get_io_service());
			delay_timer_->set_obj_desc("mds_control::delay_timer_");
			delay_timer_->register_time_handler(boost::bind(&this_type::delay_add_tracker, 
				this, serverID, param, --deadline));
		}

		delay_timer_->async_wait(seconds(1));
		return;
	}

	for (size_t i=0;i<tracker_edps.size();++i)
	{
		server_param_base Param=param;
		Param.tracker_ipport = tracker_edps[i];

		channel_vec_type params(1, Param); 
		post_cmd_to_mds(INTERNAL_SESSION_ID, serverID, params, CMD_ADD_TRACKER);
	}
}

NAMESPACE_END(p2control);
