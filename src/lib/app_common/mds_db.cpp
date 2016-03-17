#include "app_common/mds_db.h"

#include <boost/format.hpp>

#include <p2engine/utf8.hpp>

#include "common/utility.h"


NAMESPACE_BEGIN(p2control);

//using namespace boost::interprocess;
using namespace p2engine;
using namespace p2common;

const char* const regist_code="regist_code";
const char* const alive_alarm_port="alive_alarm_port";
const char* const operation_http_port="operation_http_port";
const char* const max_channel_per_server="max_channel_per_server";
const char* const shift_live_server="shift_live";
const char* const tracker="tracker";
const char* const mds_chn_hash="mds_chn_hash";

int get_distribution_type(const std::string& typestr)
{
	if (boost::iequals(typestr, "interactive_live"))
		return INTERACTIVE_LIVE_TYPE;
	else if (boost::iequals(typestr, "live"))
		return LIVE_TYPE;
	else if (boost::iequals(typestr, "vod"))
		return VOD_TYPE;
	else if (boost::iequals(typestr, "bt"))
		return BT_TYPE;
	else
		return -1;
}
const char* get_distribution_string(int type)
{
	switch(type)
	{
	case INTERACTIVE_LIVE_TYPE:
		return "interactive_live";
	case LIVE_TYPE:
		return "live";		
	case VOD_TYPE:
		return "vod";
	case BT_TYPE:
		return "bt";
	default:
		return "unknown_server_type";
	}
}

mds_db::mds_db(const host_name_t&	hostName, 
	const user_name_t&	userName, 
	const password_t&	pwd, 
	const db_name_t&		dbName)
	:db_name_(dbName.value)
	, chn_hash_table_ok_(false)
	, tracker_table_ok_(false)
	, report_table_ok_(false)
	, config_table_ok_(false)
{
	try
	{
		//boost::format createSql("CREATE DATABASE IF NOT EXISTS %s");
		//createSql%dbName;
		boost::format connection_string(
			"mysql:host=%s;"
			"database=%s;"
			"user=%s;"
			"password=%s;"
			"set_charset_name=utf8"); 

		connection_string%hostName.value%dbName.value%userName.value%pwd.value;
		db_.reset(new cppdb::session(connection_string.str())); 


		__check_config_table();
		__check_server_table(LIVE_TYPE);
		__check_server_table(VOD_TYPE);
		__check_shitf_live_server_table();
		__check_report_table();
		__check_tracker_table();

	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		exit(-1);//connect failed or driver failed, exit, can't log!
	}
}

mds_db::~mds_db()
{
	log("mds_db::exit");
}

template<typename ItemType>
bool mds_db::__get_config(ItemType& result, const std::string& id, 
	const std::string& functionName, std::string* errorMsg)
{
	if (errorMsg)
		errorMsg->clear();
	try
	{
		cppdb::result res=*db_<<(boost::format("SELECT %s FROM sys_config")%id).str();
		while (res.next()) { 
			result=res.get<ItemType>(id);
		}

		return true;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

template<typename Type>
bool mds_db::__set_config(const Type& result, const std::string& id, 
	const std::string& functionName, std::string* errorMsg)
{
	if (errorMsg)
		errorMsg->clear();
	try
	{
		__check_config_table();

		cppdb::result res=*db_<<(boost::format("select %s from sys_config")%id).str();
		if(res.next()
			&&res.get<Type>(id)==result)
		{
			return true;
		}

		cppdb::statement stmt=*db_<<(boost::format("UPDATE sys_config SET %s='%s'")%id%result).str();
		stmt.exec();
		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::get_regist_code(std::string& registCode, std::string* errorMsg)
{
	return __get_config(registCode, regist_code, "get_regist_code", errorMsg);	
}

bool  mds_db::set_regist_code(const std::string& codeValue, std::string* errorMsg)
{
	return __set_config(codeValue, regist_code, "get_regist_code", errorMsg);
}

bool mds_db::get_operation_http_port(int& port, std::string* errorMsg)
{
	return __get_config(port, operation_http_port, "get_operation_http_port", errorMsg);
}

bool mds_db::set_operation_http_port(const int port, std::string* errorMsg)
{
	return __set_config(port, operation_http_port, "set_operation_http_port", errorMsg);
}

bool mds_db::get_max_channel_per_server(int& cnt, std::string* errorMsg)
{
	return __get_config(cnt, max_channel_per_server, "get_max_channel_per_server", errorMsg);
}
bool mds_db::set_max_channel_per_server(int cnt, std::string* errorMsg)
{
	return __set_config(cnt, max_channel_per_server, "set_max_channel_per_server", errorMsg);
}

bool mds_db::set_channel(const server_param_base& param, std::string* errorMsg)
{
	return __update_or_insert_channel(param, "set_channel", errorMsg);
}
bool mds_db::set_channel(distribution_type type, const mds_Alive&msg, std::string* errorMsg)
{
	return __update_channel(type, msg, "set_channel", errorMsg);
}

bool mds_db::report_channels(distribution_type type, const std::list<mds_Alive>&msg, std::string* errorMsg)
{
	return __insert_report(type, msg, "report_channels", errorMsg);
}

bool mds_db::report_channel(distribution_type type, const mds_Alive&msg, std::string* errorMsg)
{
	return __insert_report(type, msg, "report_channel", errorMsg);
}
bool mds_db::add_channel(const server_param_base& param, std::string* errorMsg)
{
	return __update_or_insert_channel(param, "set_channel", errorMsg);
}

bool mds_db::start_channel(const server_param_base& param, std::string* errorMsg)
{
	return __start_or_stop_channel(param, "start_channel", true, errorMsg);
}
bool mds_db::stop_channel(const server_param_base& param, std::string* errorMsg)
{
	return __start_or_stop_channel(param, "stop_channel", false, errorMsg);
}

bool mds_db::del_channels(const std::vector<server_param_base>& params, std::string* errorMsg)
{
	int errorCnt=0;
	std::string emsg;
	for (size_t i=0;i<params.size();++i)
	{
		if(!del_channel(params[i], &emsg))
		{
			if (errorMsg)
				*errorMsg+=emsg+" ";
			++errorCnt;
		}
	}
	return errorCnt==0;
}

bool mds_db::del_channel(const server_param_base& param, std::string* errorMsg)
{
	try
	{
		const char* tableName=get_distribution_string(param.type);
		std::string channel_link=string_to_hex(param.channel_link);
		std::string channel_uuid=string_to_hex(param.channel_uuid);

		cppdb::statement stmt=*db_<<(boost::format("DELETE FROM %s WHERE channel_link='%s'")
			%tableName%channel_link).str();
		stmt.exec();

		bool retHashed=__del_hashed_channel(param, "del_channel", errorMsg);
		bool retShift=true;
		if(is_live_category(param.type))
			retShift=__del_shift_channel(param, "del_channel", errorMsg);

		return (stmt.affected()==1)&&retShift&&retHashed;
	}
	catch (std::exception& e)
	{
		set_and_log_message("del_channel", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("del_channel", NULL, errorMsg);
		return false;
	}
}

bool mds_db::__del_shift_channel(const server_param_base& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg)
		errorMsg->clear();

	try
	{
		const char* tableName=shift_live_server;
		std::string channel_link=string_to_hex(param.channel_link);

		cppdb::statement stmt=*db_<<(boost::format("DELETE FROM %s WHERE channel_link='%s'")
			%tableName%channel_link).str();
		stmt.exec();

		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

//兼容stream_recv_port版本
/*
#define GET_CHANNEL_COMPATIBLE_SQL(stream_recv_param, condition, condValue)\
	boost::format querySql("SELECT "	\
	"channel_link, "					\
	"channel_uuid, "					\
	"channel_key, "						\
	"name, "							\
	"path, "							\
	"duration, "						\
	"length, "							\
	stream_recv_param", "				\
	"external_address, "				\
	"internal_address, "				\
	"tracker_endpoint "					\
	" FROM %s "condition				\
	);									\
	querySql%condValue;					\
	cppdb::result res=(*db_<<querySql.str());		\
	;										
	*/

bool mds_db::get_channels(distribution_type type, 
	std::vector<server_param_base>& params, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		const char* table_name=get_distribution_string(type);

		std::string name, path;
		std::string channel_link, channel_uuid;
		server_param_base param;

		cppdb::result res = get_channel_compatible_sql( std::string("stream_recv_url"), std::string(table_name), std::string("") );
		//GET_CHANNEL_COMPATIBLE_SQL("stream_recv_url", "", tableName);
		while(res.next())
		{
			channel_link=res.get<std::string>("channel_link");
			channel_uuid=res.get<std::string>("channel_uuid");

			param.channel_key=res.get<std::string>("channel_key");
			name=res.get<std::string>("name");
			path=res.get<std::string>("path");

			param.film_duration=res.get<int64_t>("duration");
			param.film_length=res.get<int64_t>("length");
			param.stream_recv_url=res.get<std::string>("stream_recv_url");
			param.external_ipport=res.get<std::string>("external_address");
			param.internal_ipport=res.get<std::string>("internal_address");
			param.tracker_ipport=res.get<std::string>("tracker_endpoint");

			params.push_back(param);
			server_param_base& theParam=params.back();

			theParam.media_directory=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));
			theParam.name=boost::filesystem::path(convert_to_wstring(hex_to_string(name)));
			theParam.channel_link=hex_to_string(channel_link);
			theParam.channel_uuid=hex_to_string(channel_uuid);
			theParam.type=type;
		}
		if(is_live_category(type))
			return __get_shift_channels(params, "get_channels", errorMsg);

		return true;
	}
	catch (std::exception& e)
	{
		DEBUG_SCOPE(std::cout<<e.what()<<std::endl;);
		try{
			//兼容stream_recv_port版本
			const char* table_name=get_distribution_string(type);

			std::string name, path;
			std::string channel_link, channel_uuid;
			server_param_base param;
			int port=0;

			//GET_CHANNEL_COMPATIBLE_SQL("stream_recv_port", "", tableName);
			cppdb::result res = get_channel_compatible_sql( std::string("stream_recv_port"), std::string(table_name), std::string("") );
			while(res.next())
			{
				channel_link=res.get<std::string>("channel_link");
				channel_uuid=res.get<std::string>("channel_uuid");

				param.channel_key=res.get<std::string>("channel_key");
				name=res.get<std::string>("name");
				path=res.get<std::string>("path");

				param.film_duration=res.get<int64_t>("duration");
				param.film_length=res.get<int64_t>("length");
				param.external_ipport=res.get<std::string>("external_address");
				param.internal_ipport=res.get<std::string>("internal_address");
				param.tracker_ipport=res.get<std::string>("tracker_endpoint");
				port=res.get<int>("stream_recv_port");

				params.push_back(param);
				server_param_base& theParam=params.back();

				theParam.media_directory=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));
				theParam.name=boost::filesystem::path(convert_to_wstring(hex_to_string(name)));
				theParam.channel_link=hex_to_string(channel_link);
				theParam.channel_uuid=hex_to_string(channel_uuid);
				theParam.stream_recv_url="udp://0.0.0.0:"+boost::lexical_cast<std::string>(port);
				theParam.type=type;
			}
			if(is_live_category(type))
				return __get_shift_channels(params, "get_channels", errorMsg);
			return true;
		}
		catch (std::exception& e)
		{
			set_and_log_message("get_channels", &e, errorMsg);
			return false;
		}
		catch (...)
		{
			set_and_log_message("get_channels", NULL, errorMsg);
			return false;
		}
	}
	catch (...)
	{
		set_and_log_message("get_channels", NULL, errorMsg);
		return false;
	}
}

bool mds_db::__get_shift_channels(std::vector<server_param_base>& params, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		const char* tableName=shift_live_server;
		std::string path;
		std::string channel_link;

		cppdb::result res=*db_<<(boost::format("SELECT "
			"channel_link, "
			"enable, "
			"cache_dir, "
			"max_length_per_file, "
			"max_duration "
			" FROM %s "
			)%tableName).str();

		while(res.next())
		{
			channel_link=res.get<std::string>("channel_link");
			for (BOOST_AUTO(itr, params.begin());
				itr!=params.end();++itr)
			{
				if((*itr).channel_link!=hex_to_string(channel_link))
					continue;

				server_param_base& param_ref = const_cast<server_param_base&>(*itr);

				param_ref.enable_live_cache =res.get<int>("enable");
				param_ref.max_length_per_file = res.get<int64_t>("max_length_per_file");
				param_ref.max_duration=res.get<int64_t>("max_duration");
				path=res.get<std::string>("cache_dir");
				param_ref.live_cache_dir=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));

				break;
			}


		}
		return true;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::get_channel(server_param_base& param, std::string* errorMsg)
{
	return __get_channel(param, "get_channel", errorMsg);	
}

bool mds_db::__get_channel(server_param_base& param, const std::string& functionName, 
	std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		const char* table_name=get_distribution_string(param.type);
		std::string channel_link=string_to_hex(param.channel_link);
		std::string channel_uuid=string_to_hex(param.channel_uuid);
		std::string name, path;

		std::string str_cond ("WHERE channel_link='" + channel_link +"' OR channel_uuid='" + channel_uuid + "'");

		cppdb::result res = get_channel_compatible_sql( std::string("stream_recv_url"), 
														std::string(table_name), 
														str_cond );
		
		/*
		GET_CHANNEL_COMPATIBLE_SQL("stream_recv_url", 
			"WHERE channel_link='%s' OR channel_uuid='%s'", tableName%channel_link%channel_uuid);
		*/
		if(res.next())
		{
			channel_link=res.get<std::string>("channel_link");
			channel_uuid=res.get<std::string>("channel_uuid");
			param.channel_key=res.get<std::string>("channel_key");
			name=res.get<std::string>("name");
			path=res.get<std::string>("path");

			param.film_duration=res.get<int64_t>("duration");
			param.film_length=res.get<int64_t>("length");
			param.stream_recv_url=res.get<std::string>("stream_recv_url");
			param.external_ipport=res.get<std::string>("external_address");
			param.internal_ipport=res.get<std::string>("internal_address");
			param.tracker_ipport=res.get<std::string>("tracker_endpoint");

			param.media_directory=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));
			param.name= boost::filesystem::path(convert_to_wstring(hex_to_string(name)));
			param.channel_link=hex_to_string(channel_link);
			param.channel_uuid=hex_to_string(channel_uuid);

			if(is_live_category(param.type))
				return __get_shift_channel(param, "get_channel", errorMsg);
		}
		
		return true;
	}
	catch (std::exception& e)
	{
		(void)(e);
		try{
			const char* table_name=get_distribution_string(param.type);
			std::string channel_link=string_to_hex(param.channel_link);
			std::string channel_uuid=string_to_hex(param.channel_uuid);
			std::string name, path;
			int port=0;

			std::string str_cond ("WHERE channel_link=" + channel_link +" OR channel_uuid=" + channel_uuid);

			cppdb::result res = get_channel_compatible_sql( std::string("stream_recv_port"), 
				std::string(table_name), 
				str_cond );

			/*
			GET_CHANNEL_COMPATIBLE_SQL("stream_recv_port", 
				"WHERE channel_link='%s' OR channel_uuid='%s'", 
				tableName%channel_link%channel_uuid);
			*/
			if(res.next())
			{
				channel_link=res.get<std::string>("channel_link");
				channel_uuid=res.get<std::string>("channel_uuid");
				param.channel_key=res.get<std::string>("channel_key");
				name=res.get<std::string>("name");
				path=res.get<std::string>("path");

				param.film_duration=res.get<int64_t>("duration");
				param.film_length=res.get<int64_t>("length");
				param.external_ipport=res.get<std::string>("external_address");
				param.internal_ipport=res.get<std::string>("internal_address");
				param.tracker_ipport=res.get<std::string>("tracker_endpoint");
				port=res.get<int>("stream_recv_port");

				param.media_directory=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));
				param.name= boost::filesystem::path(convert_to_wstring(hex_to_string(name)));
				param.channel_link=hex_to_string(channel_link);
				param.channel_uuid=hex_to_string(channel_uuid);
				param.stream_recv_url="udp://0.0.0.0:"+boost::lexical_cast<std::string>(port);

				if(is_live_category(param.type))
					return __get_shift_channel(param, "get_channel", errorMsg);
			}
			
			return true;
		}
		catch (std::exception& e)
		{
			set_and_log_message(functionName, &e, errorMsg);
			return false;
		}
		catch (...)
		{
			set_and_log_message(functionName, NULL, errorMsg);
			return false;
		}
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

//#undef GET_CHANNEL_COMPATIBLE_SQL

bool mds_db::__get_shift_channel(server_param_base& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		const char* tableName=shift_live_server;
		std::string channel_link=string_to_hex(param.channel_link);
		std::string path;

		cppdb::result res=*db_<<(boost::format("SELECT "
			"enable, "
			"cache_dir, "
			"max_length_per_file, "
			"max_duration "
			" FROM %s WHERE channel_link='%s'"
			)%tableName%channel_link).str();
		while(res.next())
		{
			param.enable_live_cache =res.get<int>("enable");
			path = res.get<std::string>("cache_dir");

			param.max_length_per_file=res.get<int64_t>("max_length_per_file");
			param.max_duration=res.get<int64_t>("max_duration");
		}

		param.live_cache_dir=boost::filesystem::path(convert_to_wstring(hex_to_string(path)));

		return true;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::set_tracker_endpoint(distribution_type type, const std::string& val, 
	std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		__check_tracker_table();

		const char* tableName=tracker;

		cppdb::statement stmt=*db_<<(boost::format("UPDATE %s SET "
			"tracker_endpoint = '%s' ")%tableName%val).str();
		stmt.exec();
		return stmt.affected()>=0;
	}
	catch (std::exception& e)
	{
		set_and_log_message("set_tracker_endpoint", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("set_tracker_endpoint", NULL, errorMsg);
		return false;
	}
	return true;
}

bool mds_db::add_tracker_endpoint(const server_param_base& param, std::string* errorMsg)
{
	BOOST_ASSERT(!param.channel_link.empty());
	if(param.channel_link.empty())
	{
		*errorMsg="channel_link is missing, required!";	
		return false;
	}

	if(errorMsg) errorMsg->clear();
	try
	{
		__check_tracker_table();

		const char* tableName=tracker;

		//检查是否存在
		int cnt=-1;
		cppdb::statement stmt=*db_<<(boost::format("SELECT count(*) from %s"
			" where distribute_type='%s' AND channel_link='%s' AND end_point='%s'"
			)%tableName
			%get_distribution_string(param.type)
			%param.channel_link
			%param.tracker_ipport).str();
		cppdb::result res=stmt.query();
		if(res.next())
			res.fetch(0, cnt);

		if(cnt>0) return true;

		stmt.reset();
		stmt=*db_<<(boost::format("INSERT INTO %s (distribute_type, channel_link, end_point, add_time) "
			"values('%s', '%s', '%s', %s)"
			)%tableName
			%get_distribution_string(param.type)
			%param.channel_link
			%param.tracker_ipport
			%time(NULL)).str();
		stmt.exec();
		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message("add_tracker_endpoint", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("add_tracker_endpoint", NULL, errorMsg);
		return false;
	}
	return true;
}

bool mds_db::get_tracker_endpoint(const server_param_base& param, 
	std::vector<std::string>& edps, std::string* errorMsg)
{
	BOOST_ASSERT(!param.channel_link.empty());
	if(param.channel_link.empty())
	{
		if(errorMsg)
			*errorMsg="channel_link is missing, required!";	
		return false;
	}

	if(errorMsg) errorMsg->clear();
	try
	{
		const char* tableName=tracker;
		std::string tracker_edp;

		cppdb::result res=*db_<<(boost::format("SELECT "
			"end_point "
			" FROM %s WHERE channel_link='%s' AND distribute_type='%s'"
			)%tableName
			%param.channel_link
			%get_distribution_string(param.type)).str()
			;

		while(res.next())
		{
			tracker_edp=res.get<std::string>("end_point");
			edps.push_back(tracker_edp);
		}

		return true;
	}
	catch (std::exception& e)
	{
		DEBUG_SCOPE(std::cout<<e.what()<<std::endl;);
		try{
			const char* tableName=tracker;

			std::string tracker_edp;
			cppdb::result res=*db_<<(boost::format("SELECT "
				"end_point "
				" FROM %s WHERE distribute_type='%s'"
				)%tableName
				%get_distribution_string(param.type)).str();

			while(res.next())
			{
				tracker_edp=res.get<std::string>("end_point");
				edps.push_back(tracker_edp);
			}
			return true;
		}
		catch (std::exception& e)
		{
			set_and_log_message("get_tracker_endpoint", &e, errorMsg);
			return false;
		}
		catch (...)
		{
			set_and_log_message("get_tracker_endpoint", NULL, errorMsg);
			return false;
		}
	}
	catch (...)
	{
		set_and_log_message("get_tracker_endpoint", NULL, errorMsg);
		return false;
	}
}

bool mds_db::del_tracker_endpoint(const server_param_base& param, std::string* errorMsg)
{
	BOOST_ASSERT(!param.channel_link.empty());
	if(param.channel_link.empty())
	{
		if (errorMsg)
			*errorMsg="channel_link is missing, required!";	
		return false;
	}

	if(errorMsg) errorMsg->clear();
	try
	{
		const char* tableName=tracker;

		cppdb::statement stmt=*db_<<(boost::format("DELETE from %s"
			" where distribute_type='%s' AND end_point='%s'"
			)%tableName
			%get_distribution_string(param.type)
			%param.tracker_ipport).str();

		stmt.exec();

		return stmt.affected()>=1;
	}
	catch (std::exception& e)
	{
		set_and_log_message("del_tracker_endpoint", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("del_tracker_endpoint", NULL, errorMsg);
		return false;
	}
	return true;
}

bool mds_db::add_hashed_channel(int mdsID, const std::vector<server_param_base>& params, 
	std::string* errorMsg)
{	
	if(errorMsg) errorMsg->clear();
	try
	{
		__check_mds_chn_hash_table();
	}
	catch (std::exception& e)
	{
		set_and_log_message("add_hashed_channel", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("add_hashed_channel", NULL, errorMsg);
		return false;
	}

	int errorCnt=0;
	for (size_t i=0;i<params.size();++i)
	{
		if(!__add_hashed_channel(mdsID, params[i], "add_hashed_channel", errorMsg))
			errorCnt++;
	}
	return errorCnt==0;
}

bool mds_db::__add_hashed_channel(int mdsID, const server_param_base& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		const char* tableName=mds_chn_hash;

		//检查是否存在
		cppdb::statement stmt=*db_<<(boost::format("SELECT count(*) from %s"
			" where mds_id=%s AND channel_link='%s'"
			)%tableName
			%mdsID
			%param.channel_link).str();

		cppdb::result res=stmt.query();
		int cnt=-1;
		if(res.next())
			res.fetch(0, cnt);
		if(cnt==1)
			return true;

		stmt.reset();
		stmt=*db_<<(boost::format("INSERT INTO %s (type, mds_id, channel_link) "
			"values(%s, %s, '%s')"
			)%tableName
			%param.type
			%mdsID
			%param.channel_link).str();
		stmt.exec();

		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
	return true;
}

bool mds_db::__del_hashed_channel(const server_param_base&param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		const char* tableName=mds_chn_hash;
		__check_mds_chn_hash_table();

		cppdb::statement stmt=*db_<<(boost::format("delete from %s "
			"where type=%s AND channel_link='%s'"
			)%tableName
			%param.type
			%param.channel_link).str();

		stmt.exec();

		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
	return true;

}

bool mds_db::get_hashed_channels(distribution_type type, std::map<std::string, int>& mds_chnl_map, 
	std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	try
	{
		__check_mds_chn_hash_table();
		const char* tableName=mds_chn_hash;
		std::string channel_link;

		cppdb::result res=*db_<<(boost::format("SELECT "
			"mds_id, channel_link "
			" FROM %s where type=%s"
			)%tableName%type).str();

		while(res.next())
		{
			channel_link=res.get<std::string>("channel_link");
			mds_chnl_map.insert(std::make_pair(
				channel_link, 
				res.get<int>("mds_id")));
		}

		return true;
	}
	catch (std::exception& e)
	{
		set_and_log_message("get_hashed_channels", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("get_hashed_channels", NULL, errorMsg);
		return false;
	}
}

bool  mds_db::__start_or_stop_channel(const server_param_base& param, 
	const std::string& functionName, bool start, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		std::string statStr=start?"running":"stoped";

		const char* tableName=get_distribution_string(param.type);
		std::string channel_link=string_to_hex(param.channel_link);
		std::string channel_uuid=string_to_hex(param.channel_uuid);

		cppdb::result res=*db_<<(boost::format("SELECT stat FROM %s "
			" WHERE channel_link='%s' OR channel_uuid='%s' ")
			%tableName%channel_link%channel_uuid).str();
		if(res.next()&&statStr==res.get<std::string>("stat"))
			return true;

		int   stoppedValue=0;
		float stoppedValuef=0.0;

		cppdb::statement stmt=*db_<<(boost::format("UPDATE %s SET "
			"stat='%s', "
			"channel_bitrate=%s, "
			"out_kbps=%s, "
			"client_count=%s, "
			"efficient=%.2f, "
			"playing_quality=%.2f , "
			"global_rtl_lost_rate=%.2f"
			" WHERE channel_link='%s' OR channel_uuid='%s' ")
			%tableName
			%statStr%stoppedValue%stoppedValue%stoppedValue%stoppedValuef%stoppedValuef%stoppedValuef
			%channel_link%channel_uuid).str();
		stmt.exec();

		return stmt.affected()>=0;
	}
	catch (std::exception& e)
	{
		set_and_log_message("get_hashed_channels", &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message("get_hashed_channels", NULL, errorMsg);
		return false;
	}
}

bool mds_db::__update_or_insert_channel(const server_param_base& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();
	const char* tableName=get_distribution_string(param.type);

	try
	{
		//检查是不是正确updata
		cppdb::statement stmt=*db_<<(boost::format("SELECT count(*) FROM %s WHERE channel_uuid='%s' AND channel_link='%s'")
			%tableName%string_to_hex(param.channel_uuid)%string_to_hex(param.channel_link)).str();

		cppdb::result res=stmt.query();
		int cnt=-1;
		if(res.next())
			res.fetch(0, cnt);
		return insert_or_update_channel_compatible_sql(cnt, 
													std::string("stream_recv_url"), 
													std::string("'%s'"), 
													param);
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
		try{
			//检查是不是正确updata
			cppdb::statement stmt=*db_<<(boost::format("SELECT count(*) FROM %s WHERE channel_uuid='%s' AND channel_link='%s'")
				%tableName%string_to_hex(param.channel_uuid)%string_to_hex(param.channel_link)).str();

			cppdb::result res=stmt.query();
			int cnt=-1;
			if(res.next())
				res.fetch(0, cnt);
			return insert_or_update_channel_compatible_sql(cnt, 
															std::string("stream_recv_url"), 
															std::string("'%s'"), 
															param);
			if(is_live_category(param.type))
				return __update_or_insert_shift_channel(param, functionName, errorMsg);

			return true;
		}
		catch (std::exception& e)
		{
			set_and_log_message(functionName, &e, errorMsg);
			return false;
		}
		catch (...)
		{
			set_and_log_message(functionName, NULL, errorMsg);
			return false;
		}
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}


bool mds_db::__update_or_insert_shift_channel(const server_param_base& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		const char* tableName=shift_live_server;
		std::string channel_link=string_to_hex(param.channel_link);
		std::string path=string_to_hex(convert_from_wstring(param.live_cache_dir.wstring()));

		//检查是不是正确updata
		int cnt=-1;
		cppdb::statement stmt=*db_<<(boost::format("SELECT count(*) FROM %s WHERE channel_link='%s'")
			%tableName%channel_link).str();

		cppdb::result res=stmt.query();
		if(res.next())
			res.fetch(0, cnt);

		if (0==cnt)
		{
			boost::format insertSql("INSERT INTO %s " 
				"(channel_link, enable, cache_dir, max_length_per_file, max_duration) "
				" values('%s', %s, '%s', %s, %s)"
				);
			insertSql%tableName
				%channel_link
				%param.enable_live_cache
				%path
				%param.max_length_per_file
				%param.max_duration;
			stmt.reset();
			stmt=*db_<<insertSql.str();
			stmt.exec();

			return stmt.affected()==1;
		}
		else
		{
			boost::format updateSql("UPDATE %s SET "
				"enable = %s, "
				"cache_dir = '%s', "
				"max_length_per_file = %s, "
				"max_duration = %s "
				"WHERE channel_link = '%s'"
				);
			updateSql%tableName
				%param.enable_live_cache
				%path
				%param.max_length_per_file
				%param.max_duration
				%channel_link
				;
			stmt.reset();
			stmt=*db_<<updateSql.str();

			return stmt.affected()>=0;
		}
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::__update_channel(distribution_type type, const mds_Alive& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		const char* tableName=get_distribution_string(type);
		std::string channel_link=string_to_hex(param.id());

		//检查是不是正确updata
		boost::format updateSql("UPDATE %s SET "
			"channel_bitrate = %s, "
			"out_kbps = %s, "
			"client_count = %s, "
			"efficient = %.2f, "
			"playing_quality = %.2f, "
			"global_rtl_lost_rate = %.2f "
			"WHERE channel_link = '%s'"
			);
		updateSql%tableName
			%param.channel_bitrate()
			%param.out_kbps()
			%param.client_count()
			%param.p2p_efficient()
			%param.playing_quality()
			%param.global_remote_to_local_lost_rate()
			%channel_link
			;

		cppdb::statement stmt=*db_<<updateSql.str();
		stmt.exec();
		return stmt.affected()>=0;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
	return true;
}

bool mds_db::__insert_report(distribution_type type, const mds_Alive& param, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		__check_report_table();

		const char* tableName="report";
		std::string channel_link=string_to_hex(param.id());
		if(channel_link.empty())
		{
			throw(std::runtime_error("channel_link is empty!"));
		}

		boost::format insertSql("INSERT INTO %s " 
			"(channel_link, channel_bitrate, out_kbps, client_count, efficient, playing_quality, global_rtl_lost_rate, add_time) "
			" values('%s', %s, %s, %s, %.2f, %.2f, %.2f, %s)"
			);
		insertSql%tableName
			%channel_link
			%param.channel_bitrate()
			%param.out_kbps()
			%param.client_count()
			%param.p2p_efficient()
			%param.playing_quality()
			%param.global_remote_to_local_lost_rate()
			%time(NULL);

		cppdb::statement stmt=*db_<<insertSql.str();
		stmt.exec();
		return stmt.affected()==1;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::__insert_report(distribution_type type, const std::list<mds_Alive>& params, 
	const std::string& functionName, std::string* errorMsg)
{
	if(errorMsg) errorMsg->clear();

	try
	{
		__check_report_table();

		const char* tableName="report";
		std::string channel_link;
		cppdb::statement stmt;

		BOOST_FOREACH(const mds_Alive& param, params)
		{
			channel_link=string_to_hex(param.id());
			boost::format insertSql("INSERT INTO %s " 
				"(channel_link, channel_bitrate, out_kbps, client_count, efficient, playing_quality, global_rtl_lost_rate, add_time) "
				" values('%s', %s, %s, %s, %.2f, %.2f, %.2f, %s)"
				);
			insertSql%tableName
				%channel_link
				%param.channel_bitrate()
				%param.out_kbps()
				%param.client_count()
				%param.p2p_efficient()
				%param.playing_quality()
				%param.global_remote_to_local_lost_rate()
				%time(NULL);

			stmt=*db_<<insertSql.str();
			stmt.exec();
			stmt.reset();
		}
		return true;
	}
	catch (std::exception& e)
	{
		set_and_log_message(functionName, &e, errorMsg);
		return false;
	}
	catch (...)
	{
		set_and_log_message(functionName, NULL, errorMsg);
		return false;
	}
}

bool mds_db::log(const std::string& msg, const std::string& level)
{
	try{
		__check_log_table();

		const char* tableName="runlog";
		cppdb::statement stmt;

		boost::format insertSql("INSERT INTO %s " 
			"(level, content, time) "
			" values(\"%s\", \"%s\", %s)"
			);

		insertSql%tableName
			%level
			%msg
			%time(NULL);

		stmt=*db_<<insertSql.str();
		stmt.exec();
		return true;
	}
	catch(const std::exception& e)
	{
		std::cout<<"log error: "<<e.what()<<std::endl;
		return false;
	}
	catch(...){return false;}
}	

void  mds_db::__check_config_table()
{
	if( config_table_ok_ )
		return;
	//创建table
	boost::format cmd("CREATE TABLE IF NOT EXISTS sys_config("
		"id int(11) not null auto_increment, "
		"%s varchar(512) not null, "
		"%s int not null, "
		"%s int not null, "
		"%s int not null, "
		"primary key(id) )ENGINE=InnoDB DEFAULT CHARSET=utf8");
	cmd%regist_code%operation_http_port%alive_alarm_port%max_channel_per_server;

	cppdb::statement stmt=*db_<<cmd.str();
	stmt.exec();

	//插入一行default数据，方便以后使用updata更新
	stmt.reset();
	stmt=*db_<<"SELECT count(*) FROM sys_config";
	cppdb::result res=stmt.query();
	int cnt=-1;
	if(res.next())
		res.fetch(0, cnt);

	if(0==cnt)
	{
		stmt.reset();
		stmt=*db_<<"INSERT into sys_config values(0, '0', 0, 0, 100)";
		stmt.exec();
	}
	config_table_ok_=true;
}

void mds_db::__check_server_table(distribution_type type)
{
	//创建table
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS ")+get_distribution_string(type)+"("
		+"id int not null auto_increment, "
		+"channel_link varchar(128) UNIQUE NOT NULL, "
		+"channel_uuid varchar(128) UNIQUE NOT NULL, "
		+"channel_key varchar(64), "
		+"name varchar(128) NOT NULL, "
		+"path long, "
		+"duration int(32), "
		+"length int(32), "
		+"stream_recv_url  varchar(32) not null, "
		+"external_address varchar(32) not null, "
		+"internal_address varchar(32) not null, "
		+"tracker_endpoint varchar(32) not null, "
		+"stat varchar(16) not null default 'stoped', "
		+"channel_bitrate int(32) not null default 0, "
		+"out_kbps int(32) not null default 0, "
		+"client_count int(32) not null default 0, "
		+"efficient FLOAT(8, 2) not null default 0.0, "
		+"playing_quality FLOAT(8, 2) not null default 0.0, "
		+"global_rtl_lost_rate FLOAT(8, 2) not null default 0.0, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();
}

void mds_db::__check_shitf_live_server_table()
{
	if(report_table_ok_)
		return;
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS ") + std::string(shift_live_server) + "("
		+"id int not null auto_increment, "
		+"channel_link varchar(128) UNIQUE NOT NULL, "
		+"enable tinyint(1) NOT NULL default 0, "
		+"cache_dir long not null, "
		+"max_length_per_file int(32) not null default 0, "
		+"max_duration int(32) not null default 0, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();
}

void mds_db::__check_report_table()
{
	if(report_table_ok_)
		return;
	//创建table
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS report(")
		+"id int not null auto_increment, "
		+"channel_link varchar(128) NOT NULL, "
		+"channel_bitrate int(16) not null, "
		+"out_kbps int(16) not null, "
		+"client_count int(32) not null, "
		+"efficient FLOAT(8, 2) not null, "
		+"playing_quality FLOAT(8, 2) not null, "
		+"global_rtl_lost_rate FLOAT(8, 2) not null, "
		+"add_time long not null, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();

	//插入一行default数据，方便以后使用updata更新
	stmt.reset();
	stmt=*db_<<"SELECT count(*) FROM report";
	cppdb::result res=stmt.query();
	int cnt=-1;
	if(res.next())
		res.fetch(0, cnt);

	if(0==cnt)
	{
		stmt.reset();
		stmt=*db_<<"INSERT into report values(0, '0', 0, 0, 0, 0.0, 0.0, 0.0, 0)";
		stmt.exec();
	}
	report_table_ok_=true;
}

void mds_db::__check_tracker_table()
{
	if(tracker_table_ok_)
		return;
	//创建table
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS tracker(")
		+"id int not null auto_increment, "
		+"distribute_type varchar(16) NOT NULL, "
		+"channel_link varchar(128) NOT NULL, "
		+"end_point varchar(32) NOT NULL, "
		+"add_time long not null, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();
	tracker_table_ok_=true;
}

void mds_db::__check_mds_chn_hash_table()
{
	if(chn_hash_table_ok_)
		return;
	//创建table
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS mds_chn_hash(")
		+"id int auto_increment, "
		+"type int(8) NOT NULL, "
		+"mds_id int(8) NOT NULL, "
		+"channel_link varchar(128) UNIQUE NOT NULL, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();
	chn_hash_table_ok_=true;
}

void mds_db::__check_log_table()
{
	//创建table
	std::string cmd=std::string("CREATE TABLE IF NOT EXISTS runlog(")
		+"id int auto_increment, "
		+"level varchar(34) not null, "
		+"content varchar(255) not null, "
		+"time long not null, "
		+"primary key(id)"
		+")ENGINE=InnoDB DEFAULT CHARSET=utf8";

	cppdb::statement stmt=*db_<<cmd;
	stmt.exec();
}

void mds_db::set_and_log_message(const std::string &functionName, 
	const std::exception* e, std::string* errorMsg)
{
	std::string emsg;
	if (!errorMsg)
		errorMsg=&emsg;
	if (e)
		*errorMsg=functionName+" error! "+e->what();
	else
		*errorMsg=functionName+" error! unknown error.";
	log(*errorMsg);
}

NAMESPACE_END(p2control);
